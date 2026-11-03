#include <fstream>

#include "cmds.hpp"

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
      if (system_folder_name(lower(name)) || is_excluded(p) || !seen.insert(norm_key(p)).second) continue;
      if (idx.find(name, p)) continue;

      TreeInfo ti = tree_info(p, 20000);
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

  struct Hit { std::string path; bool dir; uint64_t bytes; };
  std::vector<Hit> hits;
  std::set<std::string> seen;
  for (auto& r : roots)
    walk(r.first, [&](const Entry& e) {
      std::string n = u8(e.path.filename());
      bool match = lower(n).find(term) != std::string::npos || norm_alnum(n).find(nterm) != std::string::npos;
      if (!match) return true;
      for (auto& h : hits) if (path_under(e.path, P(h.path))) return false; // nested in an earlier hit
      if (!seen.insert(norm_key(e.path)).second) return false;
      uint64_t b = e.size;
      if (e.is_dir) b = tree_info(e.path, 50000).bytes;
      hits.push_back({u8(e.path), e.is_dir, b});
      return false;
    }, r.second);

  auto reg = plat::registry_find(term);
  std::vector<std::string> still;
  for (auto& ap : apps)
    if (lower(ap.name).find(term) != std::string::npos || lower(ap.alias).find(term) != std::string::npos) still.push_back(ap.name.empty() ? ap.alias : ap.name);

  uint64_t total = 0;
  for (auto& h : hits) total += h.bytes;

  uint64_t qb = 0;
  int qfail = 0;
  bool do_q = a.has("quarantine") && G.yes;
  if (do_q) {
    if (!still.empty()) { std::fprintf(stderr, "Refusing to quarantine: \"%s\" still appears installed. Use its uninstaller first.\n", still[0].c_str()); return 1; }
    for (auto& h : hits) {
      std::string err;
      if (q_move(P(h.path), "leftover of " + term, h.bytes, err).empty()) { std::fprintf(stderr, "skip %s: %s\n", h.path.c_str(), err.c_str()); ++qfail; }
      else qb += h.bytes;
    }
  }

  if (G.json) {
    std::printf("{\"term\":\"%s\",\"still_installed\":[", jesc(term).c_str());
    for (size_t i = 0; i < still.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(still[i]).c_str());
    std::printf("],\"paths\":[");
    for (size_t i = 0; i < hits.size(); ++i)
      std::printf("%s{\"path\":\"%s\",\"dir\":%s,\"bytes\":%llu}", i ? "," : "", jesc(hits[i].path).c_str(), hits[i].dir ? "true" : "false", (unsigned long long)hits[i].bytes);
    std::printf("],\"registry\":[");
    for (size_t i = 0; i < reg.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(reg[i]).c_str());
    std::printf("],\"total_bytes\":%llu}\n", (unsigned long long)total);
    return qfail ? 2 : 0;
  }
  if (!still.empty()) std::printf("Note: \"%s\" still appears in the installed-apps list - uninstall it properly first.\n\n", still[0].c_str());
  std::printf("Files & folders matching \"%s\":\n", term.c_str());
  if (hits.empty()) std::printf("  (none)\n");
  for (auto& h : hits) std::printf("  %10s  %s%s\n", human(h.bytes).c_str(), h.path.c_str(), h.dir ? "/" : "");
  std::printf("\nRegistry (report only, never modified):\n");
  if (reg.empty()) std::printf("  (none%s)\n", plat::env("USERPROFILE") ? "" : " - registry search is Windows-only");
  for (auto& r : reg) std::printf("  %s\n", r.c_str());
  std::printf("\n%zu path(s), %s.\n", hits.size(), human(total).c_str());
  if (do_q) std::printf("Moved %s to quarantine. Undo with `cna quarantine`.\n", human(qb).c_str());
  else if (!hits.empty()) std::printf("Add --quarantine --yes to move these to quarantine (reversible).\n");
  return qfail ? 2 : 0;
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
  if (do_q) qfail = remnants_quarantine(r, qb);

  if (G.json) {
    std::printf("{\"apps_known\":%zu,\"remnants\":[", apps.size());
    for (size_t i = 0; i < r.size(); ++i)
      std::printf("%s{\"path\":\"%s\",\"kind\":\"%s\",\"score\":%d,\"bytes\":%llu,\"reason\":\"%s\"}", i ? "," : "", jesc(r[i].path).c_str(),
                  r[i].kind.c_str(), r[i].score, (unsigned long long)r[i].bytes, jesc(r[i].reason).c_str());
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
  else if (!r.empty()) std::printf("Review the list, then add --quarantine --yes (reversible). Use `cna explain <path>` on anything unsure.\n");
  return qfail ? 2 : 0;
}

} // namespace cna
