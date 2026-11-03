#include <fstream>

#include "cmds.hpp"

namespace cna {

struct Config {
  std::string profiles = "quick,browser,dev";
  double min_age_days = 3;
  bool clean = true;
  std::string remnants = "report"; // off | report | quarantine
  int remnants_min_score = 75;
  std::string dupes = "off";       // off | report | quarantine
  std::vector<std::string> dupes_roots;
  std::string dupes_min_size = "50M";
  int quarantine_keep_days = 30;
  int min_hours_between_runs = 12;
  int free_below_percent = 0;      // 0 = run regardless of free space
  size_t mem_cap_mb = 125;
  std::vector<std::string> excludes, custom_rules;
};

static const char* DEFAULT_CONF =
    "# cna automation config. Lines are `key = value`; '#' starts a comment.\n"
    "# Used by `cna auto run` (and the scheduled task created by `cna auto install`).\n"
    "\n"
    "# Cache cleaning. Profiles: quick, browser, dev, gaming, windows, apps, custom, full\n"
    "clean = true\n"
    "profiles = quick,browser,dev\n"
    "min_age_days = 3            # keep files modified within the last N days\n"
    "\n"
    "# Leftovers of uninstalled apps:  off | report | quarantine\n"
    "remnants = report\n"
    "remnants_min_score = 75     # 0-100, higher = more certain\n"
    "\n"
    "# Duplicate files:  off | report | quarantine   (roots separated by ';')\n"
    "dupes = off\n"
    "dupes_roots =\n"
    "dupes_min_size = 50M\n"
    "\n"
    "# Quarantined items older than this are permanently deleted on each run\n"
    "quarantine_keep_days = 30\n"
    "\n"
    "# Gating\n"
    "min_hours_between_runs = 12\n"
    "free_below_percent = 0      # only run when system-drive free space is below N% (0 = always)\n"
    "\n"
    "# Hard memory cap for the process\n"
    "mem_cap_mb = 125\n"
    "\n"
    "# Paths cna must never touch (repeatable)\n"
    "# exclude = C:/Users/me/AppData/Local/SomethingImportant\n"
    "\n"
    "# Extra cache folders to clean in profile 'custom' (repeatable, ${ENV} and * allowed)\n"
    "# custom_rule = ${LOCALAPPDATA}/MyApp/Cache\n";

static fs::path conf_path(const Args& a) {
  if (a.has("config")) return P(a.get("config"));
  return state_dir() / "cna.conf";
}

static bool load_config(const fs::path& path, Config& c, std::string& err) {
  std::ifstream f(path);
  if (!f) { err = "cannot read " + u8(path) + " (create one with `cna config init`)"; return false; }
  std::string line;
  int ln = 0;
  while (std::getline(f, line)) {
    ++ln;
    auto h = line.find('#');
    if (h != std::string::npos) line.resize(h);
    line = trim(line);
    if (line.empty()) continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) { err = "line " + std::to_string(ln) + ": expected key = value"; return false; }
    std::string k = lower(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
    auto b = [&](bool& dst) { std::string x = lower(v); dst = (x == "true" || x == "1" || x == "yes" || x == "on"); };
    if (k == "profiles") c.profiles = v;
    else if (k == "min_age_days") c.min_age_days = std::atof(v.c_str());
    else if (k == "clean") b(c.clean);
    else if (k == "remnants") c.remnants = lower(v);
    else if (k == "remnants_min_score") c.remnants_min_score = std::atoi(v.c_str());
    else if (k == "dupes") c.dupes = lower(v);
    else if (k == "dupes_roots") { for (auto& r : split(v, ';')) if (!trim(r).empty()) c.dupes_roots.push_back(trim(r)); }
    else if (k == "dupes_min_size") c.dupes_min_size = v;
    else if (k == "quarantine_keep_days") c.quarantine_keep_days = std::atoi(v.c_str());
    else if (k == "min_hours_between_runs") c.min_hours_between_runs = std::atoi(v.c_str());
    else if (k == "free_below_percent") c.free_below_percent = std::atoi(v.c_str());
    else if (k == "mem_cap_mb") c.mem_cap_mb = (size_t)std::atoi(v.c_str());
    else if (k == "exclude") c.excludes.push_back(v);
    else if (k == "custom_rule") c.custom_rules.push_back(v);
    else { err = "line " + std::to_string(ln) + ": unknown key '" + k + "'"; return false; }
  }
  for (const auto& m : {c.remnants, c.dupes})
    if (m != "off" && m != "report" && m != "quarantine") { err = "remnants/dupes must be off, report or quarantine"; return false; }
  return true;
}

static fs::path state_file() { return state_dir() / "state.txt"; }

static std::time_t last_run() {
  std::ifstream f(state_file());
  std::string line;
  while (std::getline(f, line))
    if (line.rfind("last_run=", 0) == 0) return (std::time_t)std::strtoll(line.c_str() + 9, nullptr, 10);
  return 0;
}

static void log_line(const std::string& msg) {
  fs::path lp = state_dir() / "cna.log";
  std::error_code ec;
  if (fs::exists(lp, ec) && fs::file_size(lp, ec) > 256 * 1024) {
    fs::path old = lp;
    old += ".1";
    fs::remove(old, ec);
    fs::rename(lp, old, ec);
  }
  std::ofstream f(lp, std::ios::app);
  f << fmt_time(std::time(nullptr)) << " | " << msg << '\n';
}

int cmd_config(const Args& a) {
  std::string sub = a.pos.empty() ? "path" : a.pos[0];
  fs::path cp = conf_path(a);
  if (sub == "path") { std::printf("%s\n", u8(cp).c_str()); return 0; }
  if (sub == "init") {
    std::error_code ec;
    if (fs::exists(cp, ec) && !a.has("force")) { std::fprintf(stderr, "%s exists (use --force to overwrite)\n", u8(cp).c_str()); return 1; }
    fs::create_directories(cp.parent_path(), ec);
    std::ofstream(cp, std::ios::trunc) << DEFAULT_CONF;
    std::printf("Wrote %s\n", u8(cp).c_str());
    return 0;
  }
  if (sub == "show") {
    std::ifstream f(cp);
    if (!f) { std::fprintf(stderr, "no config yet: run `cna config init`\n"); return 1; }
    std::string line;
    while (std::getline(f, line)) std::printf("%s\n", line.c_str());
    return 0;
  }
  std::fprintf(stderr, "usage: cna config [path|init|show]\n");
  return 64;
}

int cmd_auto(const Args& a) {
  std::string sub = a.pos.empty() ? "status" : a.pos[0];
  fs::path cp = conf_path(a);
  const std::string task = "cna-auto";

  if (sub == "install") {
    std::error_code ec;
    if (!fs::exists(cp, ec)) { fs::create_directories(cp.parent_path(), ec); std::ofstream(cp) << DEFAULT_CONF; std::printf("Created default config at %s\n", u8(cp).c_str()); }
    std::string msg;
    int rc = plat::schedule_create(task, u8(plat::exe_path()), std::atoi(a.get("every-hours", "6").c_str()), msg);
    std::printf("%s", msg.c_str());
    if (rc != 0) return 1;
    std::printf("Scheduled `cna auto run` every %s hour(s). Remove with `cna auto remove`.\n", a.get("every-hours", "6").c_str());
    return 0;
  }
  if (sub == "remove") {
    std::string msg;
    int rc = plat::schedule_delete(task, msg);
    std::printf("%s", msg.c_str());
    return rc == 0 ? 0 : 1;
  }
  if (sub == "status") {
    std::printf("Config:   %s%s\n", u8(cp).c_str(), fs::exists(cp) ? "" : "  (missing: `cna config init`)");
    std::time_t lr = last_run();
    std::printf("Last run: %s\n", lr ? fmt_time(lr).c_str() : "never");
    std::string msg;
    int rc = plat::schedule_query(task, msg);
    std::printf("Schedule: %s\n", rc == 0 ? "installed" : "not installed");
    return 0;
  }
  if (sub != "run") { std::fprintf(stderr, "usage: cna auto [run|install|remove|status] [--force] [--dry-run] [--every-hours N]\n"); return 64; }

  // ---- auto run ----
  Config c;
  std::string err;
  if (!load_config(cp, c, err)) { std::fprintf(stderr, "config error: %s\n", err.c_str()); return 1; }
  for (auto& e : c.excludes) G.excludes.push_back(P(e));
  for (auto& r : c.custom_rules) add_custom_rule(r);
  // The config may tighten the memory cap (never loosen the one given on the command line).
  if (c.mem_cap_mb >= 16 && c.mem_cap_mb * 1024 * 1024 < G.mem_cap) {
    G.mem_cap = c.mem_cap_mb * 1024 * 1024;
    std::string m;
    plat::apply_mem_cap(G.mem_cap, m); // best effort; the internal budget check applies regardless
  }

  bool force = a.has("force"), dry = a.has("dry-run");
  std::time_t now = std::time(nullptr), lr = last_run();
  if (!force && lr && c.min_hours_between_runs > 0 && (now - lr) < (std::time_t)c.min_hours_between_runs * 3600) {
    if (!G.quiet) std::printf("Skipped: last run was %lld minute(s) ago (min_hours_between_runs=%d). Use --force.\n", (long long)((now - lr) / 60), c.min_hours_between_runs);
    return 0;
  }
  if (!force && c.free_below_percent > 0) {
    std::error_code ec;
    fs::path drive = home_dir().root_path();
    auto sp = fs::space(drive.empty() ? fs::path("/") : drive, ec);
    if (!ec && sp.capacity) {
      double pct = 100.0 * (double)sp.available / (double)sp.capacity;
      if (pct >= c.free_below_percent) {
        if (!G.quiet) std::printf("Skipped: %.0f%% free is not below the %d%% trigger. Use --force.\n", pct, c.free_below_percent);
        return 0;
      }
    }
  }

  std::string summary;
  int rc = 0;
  uint64_t freed = 0;
  if (c.clean) {
    std::string bad;
    unsigned mask = parse_profiles(c.profiles, bad);
    if (!bad.empty() || !mask) { std::fprintf(stderr, "config: bad profile '%s'\n", bad.c_str()); return 1; }
    std::vector<std::string> skipped;
    auto res = run_clean(mask, c.min_age_days, !dry, skipped);
    uint64_t files = 0, failed = 0;
    for (auto& r : res) { freed += r.st.bytes; files += r.st.files; failed += r.st.failed; }
    summary += std::string(dry ? "would clean " : "cleaned ") + human(freed) + " (" + commas(files) + " files)";
    if (failed) { summary += ", " + std::to_string(failed) + " in use"; }
  }
  if (c.remnants != "off") {
    auto apps = plat::installed_apps();
    if (apps.empty()) summary += "; remnants: no app inventory";
    else {
      std::vector<std::string> stale;
      auto rem = find_remnants(c.remnants_min_score, apps, stale);
      uint64_t b = 0;
      for (auto& x : rem) b += x.bytes;
      summary += "; remnants: " + std::to_string(rem.size()) + " found (" + human(b) + ")";
      if (c.remnants == "quarantine" && !dry) {
        uint64_t mb = 0;
        if (remnants_quarantine(rem, mb)) rc = 2;
        summary += ", quarantined " + human(mb);
      }
    }
  }
  if (c.dupes != "off") {
    std::vector<fs::path> roots;
    for (auto& r : c.dupes_roots) roots.push_back(P(r));
    if (roots.empty()) roots = default_dupe_roots();
    uint64_t ms = 50u << 20;
    parse_size(c.dupes_min_size, ms);
    auto sets = find_dupes(roots, ms, 0, true);
    uint64_t w = 0;
    for (auto& s : sets) w += s.wasted;
    summary += "; dupes: " + std::to_string(sets.size()) + " sets (" + human(w) + ")";
    if (c.dupes == "quarantine" && !dry) {
      uint64_t mb = 0, mf = 0;
      if (dupes_quarantine(sets, mb, mf)) rc = 2;
      summary += ", quarantined " + human(mb);
    }
  }
  if (!dry && c.quarantine_keep_days > 0) {
    uint64_t pf = 0;
    int n = q_purge(c.quarantine_keep_days, false, pf);
    if (n) summary += "; purged " + std::to_string(n) + " old quarantine item(s) (" + human(pf) + ")";
  }

  if (!dry) {
    std::ofstream(state_file(), std::ios::trunc) << "last_run=" << (long long)now << "\nlast_freed=" << freed << "\n";
    log_line(summary);
  }
  if (!G.quiet && !G.json) std::printf("%s%s\n", dry ? "[dry-run] " : "", summary.c_str());
  if (G.json) std::printf("{\"dry_run\":%s,\"summary\":\"%s\"}\n", dry ? "true" : "false", jesc(summary).c_str());
  return rc;
}

} // namespace cna
