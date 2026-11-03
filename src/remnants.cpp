#include <fstream>

#include "cmds.hpp"
#include "model.hpp"

namespace cna {

// ---- installed-app inventory & ownership ---------------------------------
static const std::set<std::string>& stopwords() {
  static const std::set<std::string> s = {
      "the", "inc", "llc", "ltd", "corp", "corporation", "company", "software", "app", "apps", "application",
      "windows", "microsoft", "update", "updater", "launcher", "client", "installer", "setup", "data", "cache",
      "desktop", "studio", "game", "games", "tools", "tool", "helper", "service", "services", "common", "files",
      "program", "programs", "x64", "x86", "for", "and", "pro", "free", "lite", "sdk", "runtime", "redistributable",
      "plugin", "plugins", "user", "users", "local", "temp", "logs", "log", "crash", "crashes", "reports", "report",
      "settings", "config", "browser", "web", "media", "player", "system", "manager", "version", "edition"};
  return s;
}

std::vector<plat::App> load_apps(const std::string& file) {
  std::vector<plat::App> v;
  if (file.empty()) return plat::installed_apps();
  std::ifstream f(P(file));
  std::string line;
  while (std::getline(f, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    auto c = split(line, '|');
    plat::App a;
    a.name = c.size() > 0 ? c[0] : "";
    a.publisher = c.size() > 1 ? c[1] : "";
    a.install_loc = c.size() > 2 ? c[2] : "";
    a.source = "file";
    v.push_back(std::move(a));
  }
  return v;
}

void OwnerIndex::build(const std::vector<plat::App>& apps) {
  apps_.clear();
  for (auto& a : apps) {
    AppN n;
    n.a = a;
    auto add = [&](const std::string& s) {
      std::string k = norm_alnum(s);
      if (k.size() >= 3) n.names.insert(k);
      for (auto& t : tokens(s)) if (!stopwords().count(t)) n.toks.insert(t);
    };
    add(a.name); add(a.publisher); add(a.alias);
    auto words = tokens(a.name);
    if (!words.empty() && words[0].size() >= 4) n.names.insert(words[0]);
    if (words.size() >= 2) n.names.insert(words[0] + words[1]);
    if (!a.install_loc.empty()) {
      fs::path l = P(a.install_loc);
      add(u8(l.filename()));
      if (!is_protected(l)) n.loc = norm_key(l); // ignore over-broad install roots
    }
    apps_.push_back(std::move(n));
  }
}

const plat::App* OwnerIndex::find(const std::string& name, const fs::path& p) const {
  std::string nn = norm_alnum(name), pk = norm_key(p);
  auto under = [](const std::string& c, const std::string& par) {
    return c == par || (c.size() > par.size() && c.compare(0, par.size(), par) == 0 && c[par.size()] == '/');
  };
  for (auto& a : apps_) {
    if (!a.loc.empty() && (under(pk, a.loc) || under(a.loc, pk))) return &a.a;
    if (nn.size() >= 3 && a.names.count(nn)) return &a.a;
  }
  if (nn.size() >= 5)
    for (auto& a : apps_)
      for (auto& n : a.names)
        if (n.size() >= 5 && (nn.find(n) != std::string::npos || n.find(nn) != std::string::npos)) return &a.a;
  std::vector<std::string> toks;
  for (auto& t : tokens(name)) if (!stopwords().count(t)) toks.push_back(t);
  for (auto& a : apps_)
    for (auto& t : toks) if (a.toks.count(t)) return &a.a;
  return nullptr;
}

// ---- generic leftover scan -------------------------------------------------
bool system_folder_name(const std::string& l) {
  static const std::set<std::string> names = {
      "microsoft", "packages", "temp", "tmp", "programs", "comms", "connecteddevices", "d3dscache", "publishers",
      "virtualstore", "placeholdertilelogofolder", "history", "tempstate", "ssh", "common files", "windowsapps",
      "modifiablewindowsapps", "internet explorer", "package cache", "uninstall information", "dotnet", "msbuild",
      "reference assemblies", "microsoft shared", "intel", "nvidia", "nvidia corporation", "amd", "realtek",
      "application data", "desktop", "documents", "usoprivate", "usoshared", "crashdumps", "windowspowershell",
      "microsoft.net", "installshield installation information", "regid.1991-06.com.microsoft", "ms-appinstaller"};
  if (names.count(l)) return true;
  return l.rfind("windows", 0) == 0 || l.rfind("microsoft", 0) == 0 || l.rfind("$", 0) == 0;
}

// These are shared/system/vendor working directories. A missing uninstall-key match is not
// enough evidence to call them leftovers, and they must never enter the bulk quarantine path.
static bool known_non_remnant(const std::string& l, const char* root_kind) {
  static const std::set<std::string> names = {
      "softwaredistribution", "whesvc", "squirrelmachineinstalls", "squirreltemp", "common",
      "adobe", "kotlin", "crashrpt", "jedi", "ms-playwright-go", "ultralytics", "winutil"};
  if (names.count(l)) return true;
  // Shared package/program folders are not owned by one app. They need a dedicated maintenance
  // workflow, not a generic orphan detector.
  return std::string(root_kind) == "local-programs" && (l == "common" || l == "shared");
}

std::vector<Remnant> find_remnants(int min_score, const std::vector<plat::App>& apps, std::vector<std::string>& stale) {
  OwnerIndex idx;
  idx.build(apps);
  for (auto& a : apps)
    if (!a.install_loc.empty()) {
      std::error_code ec;
      if (!fs::exists(P(a.install_loc), ec)) stale.push_back((a.name.empty() ? a.alias : a.name) + "  (" + a.install_loc + ")");
    }

  struct Root { fs::path path; const char* kind; };
  std::vector<Root> roots;
  auto addroot = [&](const char* var, const char* kind, const char* sub = nullptr) {
    if (auto e = plat::env(var)) if (!e->empty()) roots.push_back({sub ? P(*e) / sub : P(*e), kind});
  };
  addroot("LOCALAPPDATA", "appdata-local");
  addroot("LOCALAPPDATA", "local-programs", "Programs");
  addroot("APPDATA", "appdata-roaming");
  addroot("PROGRAMDATA", "programdata");
  addroot("PROGRAMFILES", "program-files");
  addroot("PROGRAMFILES(X86)", "program-files");

  std::vector<Remnant> out;
  std::set<std::string> seen;
  for (auto& r : roots) {
    std::error_code ec;
    fs::directory_iterator it(r.path, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
      tick();
      const fs::path p = it->path();
      std::error_code e2;
      auto st = it->symlink_status(e2);
      if (e2 || !fs::is_directory(st) || st.type() == fs::file_type::symlink || plat::is_reparse_dir(p)) continue;
      std::string name = u8(p.filename());
      std::string lname = lower(name);
      if (system_folder_name(lname) || known_non_remnant(lname, r.kind) || is_excluded(p) || !seen.insert(norm_key(p)).second) continue;
      if (idx.find(name, p)) continue;

      TreeInfo ti = tree_info(p, 20000);
      // Empty folders have no recoverable space. Reporting them as 75-95 confidence removals
      // creates noise and makes the review screen look much less trustworthy.
      if (ti.bytes == 0) continue;
      Remnant rm;
      rm.path = u8(p);
      rm.kind = r.kind;
      rm.bytes = ti.bytes;
      rm.truncated = ti.truncated;
      int score = 55;
      std::string why = "no installed app matches \"" + name + "\"";
      std::error_code e3;
      double nd = ti.any ? age_days(ti.newest) : age_days(fs::last_write_time(p, e3));
      if (!ti.any) { score += 15; why += "; empty"; }
      if (nd > 365) { score += 25; why += "; untouched " + std::to_string((int)nd) + " days"; }
      else if (nd > 180) { score += 15; why += "; untouched " + std::to_string((int)nd) + " days"; }
      else if (nd > 90) score += 5;
      else if (nd < 30) { score -= 25; why += "; modified recently"; }
      if (ti.truncated) { score -= 20; why += "; large folder sampled only"; }
      if (std::string(r.kind) == "program-files") {
        if (ti.uninstaller) { score -= 5; rm.kind = "program-files-orphan"; why += "; has an uninstaller but no registry entry"; }
        else if (ti.exes > 0) { score -= 20; rm.kind = "unregistered-program"; why += "; contains executables (may be portable)"; }
      }
      rm.score = std::max(0, std::min(100, score));
      rm.newest_days = nd;
      rm.reason = why;
      if (rm.score >= min_score) out.push_back(std::move(rm));
    }
  }
  std::sort(out.begin(), out.end(), [](const Remnant& a, const Remnant& b) {
    return a.score != b.score ? a.score > b.score : a.bytes > b.bytes;
  });
  return out;
}

int remnants_quarantine(const std::vector<Remnant>& r, uint64_t& moved_bytes) {
  int failed = 0;
  for (auto& x : r) {
    std::string err;
    if (q_move(P(x.path), "remnant (score " + std::to_string(x.score) + "): " + x.reason, x.bytes, err).empty()) {
      std::fprintf(stderr, "skip %s: %s\n", x.path.c_str(), err.c_str());
      ++failed;
    } else moved_bytes += x.bytes;
  }
  return failed;
}

// ---- "what did <app> leave behind?" -----------------------------------------
static int app_search(const Args& a, const std::vector<plat::App>& apps) {
  std::string term = lower(a.get("app"));
  std::string nterm = norm_alnum(term);
  if (nterm.size() < 3) { std::fprintf(stderr, "--app needs at least 3 letters/digits\n"); return 64; }
  int depth = std::atoi(a.get("depth", "3").c_str());

  std::vector<std::pair<fs::path, int>> roots;
  auto addroot = [&](const char* var, int d, const char* sub = nullptr) {
    if (auto e = plat::env(var)) if (!e->empty()) roots.push_back({sub ? P(*e) / sub : P(*e), d});
  };
  addroot("LOCALAPPDATA", depth); addroot("APPDATA", depth); addroot("PROGRAMDATA", depth);
  addroot("PROGRAMFILES", depth); addroot("PROGRAMFILES(X86)", depth); addroot("TEMP", depth);
  addroot("USERPROFILE", 1);
  addroot("APPDATA", 3, "Microsoft/Windows/Start Menu/Programs");
  addroot("PROGRAMDATA", 3, "Microsoft/Windows/Start Menu/Programs");

  struct Hit {
    std::string path, category, action, reason;
    bool dir = false;
    uint64_t bytes = 0;
    float confidence = 0;
  };
  std::vector<Hit> hits;
  std::set<std::string> seen;
  for (auto& r : roots)
    walk(r.first, [&](const Entry& e) {
      if (is_excluded(e.path) || plat::is_cloud_placeholder(e.path) || (e.is_dir && plat::is_reparse_dir(e.path))) return false;
      std::string n = u8(e.path.filename());
      bool match = lower(n).find(term) != std::string::npos || norm_alnum(n).find(nterm) != std::string::npos;
      if (!match) return true;
      for (auto& h : hits) if (path_under(e.path, P(h.path))) return false; // nested in an earlier hit
      if (!seen.insert(norm_key(e.path)).second) return false;
      uint64_t b = e.size;
      if (e.is_dir) b = tree_info(e.path, 50000).bytes;
      Hit h;
      h.path = u8(e.path); h.dir = e.is_dir; h.bytes = b;
      hits.push_back(std::move(h));
      return false;
    }, r.second);

  auto reg = plat::registry_find(term);
  std::vector<std::string> still;
  for (auto& ap : apps)
    if (lower(ap.name).find(term) != std::string::npos || lower(ap.alias).find(term) != std::string::npos) still.push_back(ap.name.empty() ? ap.alias : ap.name);

  uint64_t total = 0;
  for (auto& h : hits) total += h.bytes;

  // A matching folder name alone is not enough. Describe each candidate from its
  // location, structure, file mix, deterministic safety rules, and local model.
  OwnerIndex owners;
  owners.build(apps);
  for (auto& h : hits) {
    fs::path p = P(h.path);
    Descriptor d = describe_path(p, &owners, 50000);
    Prediction pr = model().predict(d, true);
    Advice ad = advise(d, h.path, pr);
    TreeInfo ti = tree_info(p, 50000, true);
    std::set<std::string> top(d.children.begin(), d.children.end());
    RuleVerdict rv = rule_verdict(p, ti, top);
    h.confidence = ad.conf;
    if (sensitive_name(d)) h.category = "personal or sensitive data";
    else if (rv.kind != "unknown") h.category = rv.kind;
    else if (d.root_kind == "appdata-local" || d.root_kind == "appdata-roaming") h.category = "app settings or data";
    else if (d.root_kind == "program-files") h.category = "program files";
    else if (d.root_kind == "programdata") h.category = "shared application data";
    else h.category = "unknown application data";
    h.action = ad.action;
    h.reason = !ad.note.empty() ? ad.note : (!rv.summary.empty() ? rv.summary : "local model classification");
    if (d.root_kind == "windows" || system_folder_name(lower(u8(p.filename())))) {
      h.action = "keep";
      h.reason = "Windows 11 system or shared directory; it is not an application leftover";
    }
    if (!still.empty()) {
      h.action = "keep";
      h.reason = "the application is still registered; uninstall it with its official uninstaller first";
    }
  }

  uint64_t qb = 0;
  int qfail = 0;
  bool do_q = a.has("quarantine") && G.yes;
  if (do_q) {
    if (!still.empty()) { std::fprintf(stderr, "Refusing to quarantine: \"%s\" still appears installed. Use its uninstaller first.\n", still[0].c_str()); return 1; }
    std::set<std::string> selected;
    for (auto& p : a.all("select")) selected.insert(norm_key(P(p)));
    int moved_candidates = 0;
    for (auto& h : hits) {
      if (!selected.empty() && !selected.count(norm_key(P(h.path)))) continue;
      if (h.action == "keep") { std::fprintf(stderr, "keep %s: %s\n", h.path.c_str(), h.reason.c_str()); continue; }
      ++moved_candidates;
      std::string err;
      if (q_move(P(h.path), "leftover of " + term, h.bytes, err).empty()) { std::fprintf(stderr, "skip %s: %s\n", h.path.c_str(), err.c_str()); ++qfail; }
      else qb += h.bytes;
    }
    if (!selected.empty() && moved_candidates == 0) { std::fprintf(stderr, "none of the selected paths are eligible leftover candidates; inspect the report again\n"); return 1; }
  }

  if (G.json) {
    std::printf("{\"term\":\"%s\",\"still_installed\":[", jesc(term).c_str());
    for (size_t i = 0; i < still.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(still[i]).c_str());
    std::printf("],\"paths\":[");
    for (size_t i = 0; i < hits.size(); ++i)
      std::printf("%s{\"path\":\"%s\",\"dir\":%s,\"bytes\":%llu,\"category\":\"%s\",\"action\":\"%s\",\"confidence\":%.3f,\"reason\":\"%s\"}",
                  i ? "," : "", jesc(hits[i].path).c_str(), hits[i].dir ? "true" : "false", (unsigned long long)hits[i].bytes,
                  jesc(hits[i].category).c_str(), jesc(hits[i].action).c_str(), hits[i].confidence, jesc(hits[i].reason).c_str());
    std::printf("],\"registry\":[");
    for (size_t i = 0; i < reg.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(reg[i]).c_str());
    std::printf("],\"total_bytes\":%llu}\n", (unsigned long long)total);
    return qfail ? 2 : 0;
  }
  if (!still.empty()) std::printf("Note: \"%s\" still appears in the installed-apps list - uninstall it properly first.\n\n", still[0].c_str());
  std::printf("Files & folders matching \"%s\":\n", term.c_str());
  if (hits.empty()) std::printf("  (none)\n");
  for (auto& h : hits) std::printf("  %10s  [%s / %s / %.0f%%]  %s%s\n       %s\n", human(h.bytes).c_str(), h.category.c_str(), h.action.c_str(), h.confidence * 100, h.path.c_str(), h.dir ? "/" : "", h.reason.c_str());
  std::printf("\nRegistry (report only, never modified):\n");
  if (reg.empty()) std::printf("  (none%s)\n", plat::env("USERPROFILE") ? "" : " - registry search is Windows-only");
  for (auto& r : reg) std::printf("  %s\n", r.c_str());
  std::printf("\n%zu path(s), %s.\n", hits.size(), human(total).c_str());
  if (do_q) std::printf("Moved %s to quarantine. Undo with `cna quarantine`.\n", human(qb).c_str());
  else if (!hits.empty()) std::printf("Review the data types, then use --quarantine --yes --select PATH for only the items you do not want to keep. Moves are reversible.\n");
  return qfail ? 2 : 0;
}

static std::vector<const plat::App*> app_matches(const std::vector<plat::App>& apps, const std::string& term) {
  std::vector<const plat::App*> exact, loose;
  std::string key = norm_alnum(term);
  for (auto& a : apps) {
    std::string name = a.name.empty() ? a.alias : a.name;
    std::string nk = norm_alnum(name);
    if (nk == key && !key.empty()) exact.push_back(&a);
    else if (!key.empty() && (nk.find(key) != std::string::npos || norm_alnum(a.alias).find(key) != std::string::npos)) loose.push_back(&a);
  }
  return exact.empty() ? loose : exact;
}

static const char* app_source(const plat::App& a) {
  if (a.source == "appx") return "Microsoft Store";
  if (a.source == "hkcu") return "Current user";
  if (a.source == "hklm" || a.source == "hklm32") return "System";
  return a.source.c_str();
}

int cmd_apps(const Args& a) {
  std::string sub = a.pos.empty() ? "list" : lower(a.pos[0]);
  auto apps = load_apps(a.get("apps-file"));
  if (apps.empty()) { std::fprintf(stderr, "No installed-app inventory is available. On Windows, run this outside a restricted shell.\n"); return 1; }
  std::string term = a.get("app", a.get("search"));
  if (sub == "leftovers") {
    if (term.empty()) { std::fprintf(stderr, "apps leftovers requires --app NAME\n"); return 64; }
    return app_search(a, apps);
  }
  if (sub == "list") {
    std::set<std::string> seen;
    if (G.json) std::printf("{\"apps\":[");
    bool first = true;
    for (auto& app : apps) {
      std::string name = app.name.empty() ? app.alias : app.name;
      if (name.empty()) continue;
      if (!term.empty() && lower(name).find(lower(term)) == std::string::npos && lower(app.publisher).find(lower(term)) == std::string::npos) continue;
      std::string identity = norm_alnum(name) + "|" + norm_alnum(app.publisher) + "|" + norm_key(P(app.install_loc));
      if (!seen.insert(identity).second) continue;
      if (G.json) {
        std::printf("%s{\"name\":\"%s\",\"publisher\":\"%s\",\"version\":\"%s\",\"install_location\":\"%s\",\"source\":\"%s\",\"official_uninstaller\":%s}",
                    first ? "" : ",", jesc(name).c_str(), jesc(app.publisher).c_str(), jesc(app.version).c_str(), jesc(app.install_loc).c_str(),
                    jesc(app_source(app)).c_str(), app.uninstall.empty() ? "false" : "true");
      } else {
        std::printf("%s\n  %s%s%s\n  Source: %s  Official uninstaller: %s\n", name.c_str(), app.publisher.empty() ? "" : app.publisher.c_str(),
                    app.version.empty() ? "" : " ", app.version.empty() ? "" : app.version.c_str(), app_source(app), app.uninstall.empty() ? "no" : "yes");
      }
      first = false;
    }
    if (G.json) std::printf("]}\n");
    return 0;
  }
  if (sub != "remove") { std::fprintf(stderr, "apps supports list, remove, or leftovers\n"); return 64; }
  if (term.empty()) { std::fprintf(stderr, "apps remove requires --app NAME\n"); return 64; }
  auto found = app_matches(apps, term);
  if (found.empty()) { std::fprintf(stderr, "No installed application exactly matches \"%s\". Use `cna apps list --search ...` first.\n", term.c_str()); return 1; }
  if (found.size() != 1) {
    std::fprintf(stderr, "More than one application matches \"%s\"; use the exact display name:\n", term.c_str());
    for (auto* p : found) std::fprintf(stderr, "  %s\n", (p->name.empty() ? p->alias : p->name).c_str());
    return 1;
  }
  const plat::App& app = *found[0];
  std::string name = app.name.empty() ? app.alias : app.name;
  if (app.uninstall.empty()) { std::fprintf(stderr, "Refusing removal: %s has no registered official uninstaller. Nothing was changed.\n", name.c_str()); return 1; }
  if (!G.yes) {
    std::printf("Official uninstaller for %s:\n  %s\n\nPreview only. Re-run `cna apps remove --app \"%s\" --yes` to launch it. CNA will never delete its install folder directly.\n",
                name.c_str(), app.uninstall.c_str(), name.c_str());
    return 0;
  }
  std::string out;
  int rc = plat::run_command(app.uninstall, out);
  std::printf("Official uninstaller finished with exit code %d.\n", rc);
  if (!out.empty()) std::printf("%s\n", out.c_str());
  std::printf("No application data was removed by CNA. Review what remains with:\n  cna apps leftovers --app \"%s\"\n", name.c_str());
  return rc == 0 ? 0 : 2;
}

int cmd_remnants(const Args& a) {
  auto apps = load_apps(a.get("apps-file"));
  if (a.has("app")) return app_search(a, apps);
  if (apps.empty()) {
    std::fprintf(stderr, "No installed-app inventory available (Windows registry, or pass --apps-file FILE with lines name|publisher|install_dir).\n");
    return 1;
  }
  int min_score = std::atoi(a.get("min-score", "65").c_str());
  std::vector<std::string> stale;
  auto r = find_remnants(min_score, apps, stale);
  uint64_t total = 0;
  for (auto& x : r) total += x.bytes;

  uint64_t qb = 0;
  int qfail = 0;
  bool do_q = a.has("quarantine") && G.yes;
  if (do_q) {
    std::set<std::string> selected;
    for (auto& p : a.all("select")) selected.insert(norm_key(P(p)));
    std::vector<Remnant> chosen;
    for (auto& x : r)
      if (selected.empty() || selected.count(norm_key(P(x.path)))) chosen.push_back(x);
    if (!selected.empty() && chosen.empty()) {
      std::fprintf(stderr, "none of the selected paths are current leftover candidates; scan again\n");
      return 1;
    }
    qfail = remnants_quarantine(chosen, qb);
  }

  if (G.json) {
    std::printf("{\"apps_known\":%zu,\"remnants\":[", apps.size());
    for (size_t i = 0; i < r.size(); ++i)
      std::printf("%s{\"path\":\"%s\",\"kind\":\"%s\",\"score\":%d,\"bytes\":%llu,\"scan_complete\":%s,\"reason\":\"%s\"}", i ? "," : "", jesc(r[i].path).c_str(),
                   r[i].kind.c_str(), r[i].score, (unsigned long long)r[i].bytes, r[i].truncated ? "false" : "true", jesc(r[i].reason).c_str());
    std::printf("],\"stale_uninstall_entries\":[");
    for (size_t i = 0; i < stale.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(stale[i]).c_str());
    std::printf("],\"total_bytes\":%llu,\"quarantined_bytes\":%llu}\n", (unsigned long long)total, (unsigned long long)qb);
    return qfail ? 2 : 0;
  }
  std::printf("Compared against %zu known installed apps.\n\n", apps.size());
  if (r.empty()) std::printf("No likely remnants at score >= %d.\n", min_score);
  for (auto& x : r)
    std::printf("[%3d] %10s%s  %s\n       %s - %s\n", x.score, human(x.bytes).c_str(), x.truncated ? "+" : " ", x.path.c_str(), x.kind.c_str(), x.reason.c_str());
  if (!stale.empty()) {
    std::printf("\nStale uninstall entries (install folder is gone; report only):\n");
    for (auto& s : stale) std::printf("  %s\n", s.c_str());
  }
  std::printf("\n%zu candidate(s), %s.\n", r.size(), human(total).c_str());
  if (do_q) std::printf("Moved %s to quarantine. Undo with `cna quarantine`.\n", human(qb).c_str());
  else if (!r.empty()) std::printf("Review the list, then add --quarantine --yes (reversible). Use repeated --select PATH to move only chosen rows, or `cna explain <path>` on anything unsure.\n");
  return qfail ? 2 : 0;
}

} // namespace cna
