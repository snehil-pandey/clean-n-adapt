#include <functional>

#include "cmds.hpp"
#include "model.hpp"

// `cna recommend`: looks at the big folders in AppData / ProgramData / your profile, decides for each one
// "clean" (safe to move to quarantine), "review" (look first) or "keep", and explains why.
//   1. Deterministic rules (well-known folder names, marker files) are authoritative when they know the folder.
//   2. Otherwise the learned model proposes, and advise() applies the non-learned safety gate.
//   3. Anything applied goes to QUARANTINE, never straight to deletion.
namespace cna {

namespace {
struct Item {
  std::string path, name, action, cls, source, note;
  float conf = 0;
  uint64_t bytes = 0;
  bool truncated = false;
  std::vector<std::string> why;
};

Item evaluate(const fs::path& p, const OwnerIndex& owners, uint64_t max_entries) {
  Item it;
  it.path = u8(p);
  it.name = u8(p.filename());
  std::set<std::string> top;
  std::error_code ec;
  int n = 0;
  for (fs::directory_iterator d(p, fs::directory_options::skip_permission_denied, ec), end; !ec && d != end && n < 40; d.increment(ec), ++n)
    top.insert(lower(u8(d->path().filename())));
  TreeInfo ti = tree_info(p, max_entries, true);
  it.bytes = ti.bytes;
  it.truncated = ti.truncated;
  bool owned = owners.find(it.name, p) != nullptr;
  Descriptor d = make_descriptor(p, ti, top, owned);
  Prediction pr = model().predict(d, true);
  Advice adv = advise(d, it.path, pr);
  RuleVerdict rv = rule_verdict(p, ti, top);
  for (auto& w : pr.why) it.why.push_back(w.first);

  if (adv.note == "your earlier decision") {                 // the user's own word beats everything
    it.action = adv.action; it.cls = cls_name(adv.cls); it.conf = 1; it.source = "your decision"; it.note = adv.note;
  } else if (sensitive_name(d)) {                            // never-clean names beat rules and model
    it.action = "keep"; it.cls = "keep"; it.conf = 1; it.source = "safety rule"; it.note = adv.note;
  } else if (rv.safety != "unknown") {                       // known folder: rules are authoritative
    it.cls = rv.safety;
    it.conf = 1;
    it.source = "rules";
    it.note = rv.summary;
    it.action = rv.safety == "regenerable" ? "clean" : rv.safety == "keep" ? "keep" : "review";
    if (it.action == "clean" && pr.cls == C_KEEP && pr.conf() >= 0.9f) { it.action = "review"; it.note = "rules say regenerable but the model strongly disagrees"; }
    it.why.clear();
  } else {                                                   // unknown to the rules: model + gate
    it.action = adv.action; it.cls = cls_name(adv.cls); it.conf = adv.conf; it.source = "model"; it.note = adv.note;
  }
  return it;
}
} // namespace

int cmd_recommend(const Args& a) {
  uint64_t min_size = 5u << 20;
  if (a.has("min-size") && !parse_size(a.get("min-size"), min_size)) { std::fprintf(stderr, "bad --min-size\n"); return 64; }
  size_t limit = (size_t)std::atoi(a.get("limit", "25").c_str());
  auto apps = load_apps(a.get("apps-file"));
  OwnerIndex owners;
  owners.build(apps);

  std::vector<fs::path> roots;
  auto addroot = [&](const char* var) { if (auto e = plat::env(var)) if (!e->empty()) roots.push_back(P(*e)); };
  addroot("LOCALAPPDATA"); addroot("APPDATA"); addroot("PROGRAMDATA");
  for (auto& r : a.all("root")) roots.push_back(P(r));
  if (a.has("root")) roots.erase(roots.begin(), roots.end() - (long)a.all("root").size()); // explicit roots replace the defaults

  std::vector<Item> items;
  uint64_t scanned = 0;
  std::set<std::string> seen;
  // Walks down through "review" folders (app data roots, browser profiles ...) looking for clean-able folders
  // such as Cache / Logs / Crashpad. Returns how many bytes it suggested cleaning at or below `p`, so a parent is
  // reported with only its remainder (no double counting).
  const int kMaxDepth = 6;
  std::function<uint64_t(const fs::path&, int)> consider = [&](const fs::path& p, int depth) -> uint64_t {
    if (is_excluded(p) || is_protected(p) || !seen.insert(norm_key(p)).second) return 0;
    ++scanned;
    Item it = evaluate(p, owners, 60000);
    if (it.bytes < min_size) return 0;
    if (it.action == "clean") { items.push_back(it); return it.bytes; }
    if (it.action == "keep") return 0;
    uint64_t below = 0;   // bytes already reported by rows underneath this folder
    if (depth < kMaxDepth) {
      std::error_code ec;
      for (fs::directory_iterator d(p, fs::directory_options::skip_permission_denied, ec), end; !ec && d != end; d.increment(ec)) {
        tick();
        std::error_code e2;
        auto st = d->symlink_status(e2);
        if (e2 || !fs::is_directory(st) || st.type() == fs::file_type::symlink || plat::is_reparse_dir(d->path())) continue;
        below += consider(d->path(), depth + 1);
      }
    }
    // each byte is reported once, at the most specific folder
    uint64_t rest = it.bytes > below ? it.bytes - below : 0;
    if (rest >= min_size) { it.bytes = rest; items.push_back(it); return below + rest; }
    return below;
  };
  for (auto& r : roots) {
    std::error_code ec;
    for (fs::directory_iterator d(r, fs::directory_options::skip_permission_denied, ec), end; !ec && d != end; d.increment(ec)) {
      tick();
      std::error_code e2;
      auto st = d->symlink_status(e2);
      if (e2 || !fs::is_directory(st) || st.type() == fs::file_type::symlink || plat::is_reparse_dir(d->path())) continue;
      if (system_folder_name(lower(u8(d->path().filename())))) continue;
      consider(d->path(), 1);
    }
  }
  std::sort(items.begin(), items.end(), [](const Item& x, const Item& y) { return x.bytes > y.bytes; });

  uint64_t clean_b = 0, review_b = 0;
  for (auto& i : items) (i.action == "clean" ? clean_b : review_b) += i.bytes;

  // optional: apply the "clean" suggestions by moving them to quarantine
  uint64_t moved_b = 0;
  int moved = 0, failed = 0;
  bool apply = a.has("apply") && G.yes;
  std::set<std::string> selected;
  for (auto& p : a.all("select")) selected.insert(norm_key(P(p)));
  if (apply)
    for (auto& i : items) {
      if (i.action != "clean") continue;
      if (!selected.empty() && !selected.count(norm_key(P(i.path)))) continue;
      std::string err;
      std::string why = "advisor: " + i.source + (i.note.empty() ? "" : " - " + i.note);
      if (q_move(P(i.path), why, i.bytes, err).empty()) { std::fprintf(stderr, "skip %s: %s\n", i.path.c_str(), err.c_str()); ++failed; }
      else { moved_b += i.bytes; ++moved; }
    }

  if (G.json) {
    std::printf("{\"items\":[");
    for (size_t k = 0; k < items.size(); ++k) {
      auto& i = items[k];
      std::printf("%s{\"path\":\"%s\",\"name\":\"%s\",\"action\":\"%s\",\"class\":\"%s\",\"confidence\":%.3f,\"bytes\":%llu,\"source\":\"%s\",\"note\":\"%s\",\"because\":[",
                  k ? "," : "", jesc(i.path).c_str(), jesc(i.name).c_str(), i.action.c_str(), i.cls.c_str(), i.conf, (unsigned long long)i.bytes,
                  i.source.c_str(), jesc(i.note).c_str());
      for (size_t w = 0; w < i.why.size(); ++w) std::printf("%s\"%s\"", w ? "," : "", jesc(i.why[w]).c_str());
      std::printf("]}");
    }
    std::printf("],\"clean_bytes\":%llu,\"review_bytes\":%llu,\"folders_checked\":%llu,\"applied\":%s,\"quarantined\":%d,\"quarantined_bytes\":%llu,\"failed\":%d}\n",
                (unsigned long long)clean_b, (unsigned long long)review_b, (unsigned long long)scanned, apply ? "true" : "false", moved,
                (unsigned long long)moved_b, failed);
    return failed ? 2 : 0;
  }

  auto print_group = [&](const char* title, const char* act) {
    std::printf("\n%s\n", title);
    size_t shown = 0, total = 0;
    for (auto& i : items) if (i.action == act) ++total;
    for (auto& i : items) {
      if (i.action != std::string(act)) continue;
      if (shown++ >= limit) break;
      std::printf("  %10s%s  %s\n", human(i.bytes).c_str(), i.truncated ? "+" : " ", i.path.c_str());
      std::string why;
      for (size_t w = 0; w < i.why.size() && w < 2; ++w) why += (why.empty() ? "" : "; ") + i.why[w];
      std::printf("        %s %.0f%%%s%s%s\n", i.source.c_str(), i.conf * 100, why.empty() ? "" : "  because: ", why.c_str(), "");
      if (!i.note.empty()) std::printf("        note: %s\n", i.note.c_str());
    }
    if (total > limit) std::printf("  ... %zu more (use --limit N or --json)\n", total - limit);
    if (!total) std::printf("  (none)\n");
  };
  std::printf("Checked %llu folders (min size %s).\n", (unsigned long long)scanned, human(min_size).c_str());
  print_group("Suggested clean-ups (safe: moved to quarantine, restorable):", "clean");
  print_group("Worth a look (not cleaned automatically):", "review");
  std::printf("\nSuggested clean-ups: %s    To review: %s\n", human(clean_b).c_str(), human(review_b).c_str());
  if (apply) std::printf("Moved %d folder(s) (%s) to quarantine. Undo any with `cna quarantine restore <id>`; restoring teaches the model.\n", moved, human(moved_b).c_str());
  else if (clean_b) std::printf("Apply the clean-ups with: cna recommend --apply --yes     Teach the model: cna model feedback PATH --label regenerable|caution|keep\n");
  return failed ? 2 : 0;
}

} // namespace cna
