#include <chrono>
#include <fstream>

#include "cmds.hpp"
#include "model.hpp"

// Remnants and duplicates are never hard-deleted by default: they are moved here
// first, with a manifest so they can be restored or purged later.
namespace cna {

static fs::path qdir() { return state_dir() / "quarantine"; }
static fs::path qindex() { return qdir() / "index.tsv"; }

struct QEntry { std::string id, orig, reason; std::time_t when = 0; uint64_t bytes = 0; };

static std::vector<QEntry> q_load() {
  std::vector<QEntry> v;
  std::ifstream f(qindex());
  std::string line;
  while (std::getline(f, line)) {
    auto c = split(line, '\t');
    if (c.size() < 5) continue;
    QEntry e;
    e.id = c[0];
    e.orig = unesc_tsv(c[1]);
    e.when = (std::time_t)std::strtoll(c[2].c_str(), nullptr, 10);
    e.bytes = std::strtoull(c[3].c_str(), nullptr, 10);
    e.reason = unesc_tsv(c[4]);
    v.push_back(std::move(e));
  }
  return v;
}
static void q_save(const std::vector<QEntry>& v) {
  std::error_code ec;
  fs::create_directories(qdir(), ec);
  std::ofstream f(qindex(), std::ios::trunc);
  for (auto& e : v)
    f << e.id << '\t' << esc_tsv(e.orig) << '\t' << (long long)e.when << '\t' << e.bytes << '\t' << esc_tsv(e.reason) << '\n';
}

static bool move_any(const fs::path& src, const fs::path& dst, std::string& err) {
  std::error_code ec;
  fs::rename(src, dst, ec);
  if (!ec) return true;
  // Cross-volume: copy, then remove the original only if the copy succeeded.
  std::error_code ec2;
  if (fs::is_directory(src, ec2))
    fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec2);
  else
    fs::copy_file(src, dst, fs::copy_options::none, ec2);
  if (ec2) { err = "copy failed: " + ec2.message(); std::error_code e3; fs::remove_all(dst, e3); return false; }
  fs::remove_all(src, ec2);
  if (ec2 || fs::exists(src)) {
    err = "copied but could not remove original (in use?)";
    std::error_code e3; fs::remove_all(dst, e3);
    return false;
  }
  return true;
}

std::string q_move(const fs::path& src, const std::string& reason, uint64_t bytes, std::string& err) {
  if (is_protected(src) || is_excluded(src)) { err = "protected path"; return ""; }
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  static int seq = 0;
  char idb[48];
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  std::strftime(idb, sizeof idb, "%Y%m%d-%H%M%S", &tmv);
  std::string id = std::string(idb) + "-" + std::to_string(++seq);
  fs::path dir = qdir() / id;
  std::error_code ec;
  fs::create_directories(dir, ec);
  if (ec) { err = "cannot create quarantine dir: " + ec.message(); return ""; }
  fs::path dst = dir / src.filename();
  if (!move_any(src, dst, err)) { fs::remove(dir, ec); return ""; }
  auto v = q_load();
  v.push_back({id, u8(src), reason, t, bytes});
  q_save(v);
  return id;
}

int q_purge(double older_than_days, bool all, uint64_t& freed) {
  auto v = q_load();
  std::vector<QEntry> keep;
  int n = 0;
  std::time_t now = std::time(nullptr);
  for (auto& e : v) {
    double age = (double)(now - e.when) / 86400.0;
    if (all || age >= older_than_days) {
      std::error_code ec;
      fs::remove_all(qdir() / e.id, ec);
      freed += e.bytes;
      ++n;
    } else keep.push_back(e);
  }
  q_save(keep);
  return n;
}

int cmd_quarantine(const Args& a) {
  std::string sub = a.pos.empty() ? "list" : a.pos[0];
  auto v = q_load();
  if (sub == "list") {
    uint64_t total = 0;
    for (auto& e : v) total += e.bytes;
    if (G.json) {
      std::printf("{\"entries\":[");
      for (size_t i = 0; i < v.size(); ++i)
        std::printf("%s{\"id\":\"%s\",\"original\":\"%s\",\"bytes\":%llu,\"when\":\"%s\",\"reason\":\"%s\"}", i ? "," : "",
                    v[i].id.c_str(), jesc(v[i].orig).c_str(), (unsigned long long)v[i].bytes,
                    fmt_time(v[i].when).c_str(), jesc(v[i].reason).c_str());
      std::printf("],\"total_bytes\":%llu}\n", (unsigned long long)total);
      return 0;
    }
    if (v.empty()) { std::printf("Quarantine is empty.\n"); return 0; }
    for (auto& e : v)
      std::printf("%-20s %10s  %s\n    from: %s\n    why:  %s\n", e.id.c_str(), human(e.bytes).c_str(),
                  fmt_time(e.when).c_str(), e.orig.c_str(), e.reason.c_str());
    std::printf("\n%zu item(s), %s held. Restore: cna quarantine restore <id>   Purge: cna quarantine purge --older-than 30 --yes\n",
                v.size(), human(total).c_str());
    return 0;
  }
  if (sub == "restore") {
    if (a.pos.size() < 2) { std::fprintf(stderr, "usage: cna quarantine restore <id>\n"); return 64; }
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (it->id != a.pos[1]) continue;
      fs::path orig = P(it->orig), src = qdir() / it->id / orig.filename();
      std::error_code ec;
      if (fs::exists(orig, ec)) { std::fprintf(stderr, "refusing: %s already exists\n", it->orig.c_str()); return 1; }
      fs::create_directories(orig.parent_path(), ec);
      std::string err;
      if (!move_any(src, orig, err)) { std::fprintf(stderr, "restore failed: %s\n", err.c_str()); return 1; }
      fs::remove_all(qdir() / it->id, ec);
      std::string why = it->reason;
      v.erase(it);
      q_save(v);
      std::printf("Restored %s\n", orig.string().c_str());
      if (why.rfind("advisor:", 0) == 0 || why.rfind("remnant", 0) == 0) { // you wanted it back: remember that
        std::string err;
        Descriptor d = describe_path(orig, nullptr, 60000);
        if (model_feedback(d, u8(orig), C_CAUTION, err)) std::printf("Noted: the model will treat folders like this one more carefully.\n");
      }
      return 0;
    }
    std::fprintf(stderr, "no such quarantine id\n");
    return 1;
  }
  if (sub == "purge") {
    if (!G.yes) { std::printf("Purge permanently deletes quarantined items. Re-run with --yes (use --older-than DAYS or --all).\n"); return 0; }
    uint64_t freed = 0;
    int n = q_purge(std::atof(a.get("older-than", "0").c_str()), a.has("all"), freed);
    std::printf("Purged %d item(s), freed %s.\n", n, human(freed).c_str());
    return 0;
  }
  std::fprintf(stderr, "usage: cna quarantine [list|restore <id>|purge]\n");
  return 64;
}

} // namespace cna
