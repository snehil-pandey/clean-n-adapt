#include "cmds.hpp"

#include <fstream>

namespace cna {

struct RuleDef {
  std::string id;
  unsigned mask;
  std::string pattern;
  bool admin;
  double min_age; // days; files newer than this are kept
  std::string note;
};

static std::vector<RuleDef> g_custom;

void add_custom_rule(const std::string& pattern) {
  g_custom.push_back({"custom", CUSTOM, pattern, false, 0, "user-defined rule"});
}

unsigned parse_profiles(const std::string& csv, std::string& bad) {
  unsigned m = 0;
  for (auto t : split(csv, ',')) {
    t = lower(trim(t));
    if (t.empty()) continue;
    if (t == "quick" || t == "safe") m |= QUICK;
    else if (t == "browser") m |= BROWSER;
    else if (t == "dev") m |= DEV;
    else if (t == "gaming") m |= GAMING;
    else if (t == "windows") m |= WINDOWS;
    else if (t == "apps") m |= APPS;
    else if (t == "custom") m |= CUSTOM;
    else if (t == "full" || t == "all") m |= FULL;
    else bad = t;
  }
  return m;
}

static std::vector<RuleDef> builtin_rules() {
  std::vector<RuleDef> r;
  auto add = [&](const std::string& id, unsigned m, const std::string& pat, double age = 0, bool admin = false,
                 const std::string& note = "rebuilt on demand") { r.push_back({id, m, pat, admin, age, note}); };

  // QUICK: user-level junk that is always safe to drop
  add("user-temp", QUICK, "${TEMP}", 1, false, "temp files (anything newer than 1 day is kept)");
  add("inet-cache", QUICK, "${LOCALAPPDATA}/Microsoft/Windows/INetCache", 1);
  add("thumbnails", QUICK, "${LOCALAPPDATA}/Microsoft/Windows/Explorer/thumbcache_*.db", 0, false,
      "Explorer thumbnail cache (locked files are skipped)");
  add("crash-dumps", QUICK, "${LOCALAPPDATA}/CrashDumps");
  add("wer-user", QUICK, "${LOCALAPPDATA}/Microsoft/Windows/WER", 0, false, "Windows Error Reporting queue");

  // BROWSER: Chromium family + Firefox
  struct B { const char* id; const char* root; };
  const B chromium[] = {{"chrome", "${LOCALAPPDATA}/Google/Chrome/User Data"},
                        {"edge", "${LOCALAPPDATA}/Microsoft/Edge/User Data"},
                        {"brave", "${LOCALAPPDATA}/BraveSoftware/Brave-Browser/User Data"},
                        {"vivaldi", "${LOCALAPPDATA}/Vivaldi/User Data"}};
  for (auto& b : chromium) {
    for (const char* sub : {"Cache", "Code Cache", "GPUCache", "Service Worker/CacheStorage"})
      add(std::string(b.id) + "-cache", BROWSER, std::string(b.root) + "/*/" + sub);
    for (const char* sub : {"ShaderCache", "GrShaderCache"})
      add(std::string(b.id) + "-cache", BROWSER, std::string(b.root) + "/" + sub);
  }
  add("firefox-cache", BROWSER, "${LOCALAPPDATA}/Mozilla/Firefox/Profiles/*/cache2");
  add("firefox-cache", BROWSER, "${LOCALAPPDATA}/Mozilla/Firefox/Profiles/*/startupCache");

  // DEV
  add("pip-cache", DEV, "${LOCALAPPDATA}/pip/Cache");
  add("npm-cache", DEV, "${LOCALAPPDATA}/npm-cache");
  add("npm-cache", DEV, "${APPDATA}/npm-cache");
  add("yarn-cache", DEV, "${LOCALAPPDATA}/Yarn/Cache");
  add("nuget-http", DEV, "${LOCALAPPDATA}/NuGet/v3-cache");
  add("gradle-cache", DEV, "${USERPROFILE}/.gradle/caches", 0, false, "re-downloaded on next build (slow first build)");
  add("cargo-registry", DEV, "${USERPROFILE}/.cargo/registry/cache", 0, false, "re-downloaded on next build");
  add("go-build", DEV, "${LOCALAPPDATA}/go-build");
  for (const char* sub : {"Cache", "CachedData", "Code Cache", "GPUCache"})
    add("vscode-cache", DEV, std::string("${APPDATA}/Code/") + sub);

  // GAMING (shader caches rebuild on next launch -> brief stutter)
  add("nvidia-shaders", GAMING, "${LOCALAPPDATA}/NVIDIA/DXCache", 0, false, "shaders recompile on next launch");
  add("nvidia-shaders", GAMING, "${LOCALAPPDATA}/NVIDIA/GLCache", 0, false, "shaders recompile on next launch");
  add("nvidia-shaders", GAMING, "${PROGRAMDATA}/NVIDIA Corporation/NV_Cache", 0, false, "shaders recompile on next launch");
  add("d3d-shaders", GAMING, "${LOCALAPPDATA}/D3DSCache", 0, false, "shaders recompile on next launch");
  add("amd-shaders", GAMING, "${LOCALAPPDATA}/AMD/DxCache", 0, false, "shaders recompile on next launch");
  add("steam-web", GAMING, "${PROGRAMFILES(X86)}/Steam/appcache/httpcache");
  add("steam-web", GAMING, "${LOCALAPPDATA}/Steam/htmlcache");
  add("epic-web", GAMING, "${LOCALAPPDATA}/EpicGamesLauncher/Saved/webcache*");

  // WINDOWS (needs elevation)
  add("win-temp", WINDOWS, "${SystemRoot}/Temp", 1, true, "system temp");
  add("win-update-dl", WINDOWS, "${SystemRoot}/SoftwareDistribution/Download", 0, true, "already-installed update payloads");
  add("wer-system", WINDOWS, "${PROGRAMDATA}/Microsoft/Windows/WER", 0, true);
  add("minidumps", WINDOWS, "${SystemRoot}/Minidump", 0, true);
  add("cbs-logs", WINDOWS, "${SystemRoot}/Logs/CBS", 7, true, "servicing logs older than 7 days");

  // APPS (Electron-style caches)
  for (const char* sub : {"Cache", "Code Cache", "GPUCache"}) {
    add("discord-cache", APPS, std::string("${APPDATA}/discord/") + sub);
    add("slack-cache", APPS, std::string("${APPDATA}/Slack/") + sub);
  }
  add("spotify-cache", APPS, "${LOCALAPPDATA}/Spotify/Data");
  add("spotify-cache", APPS, "${LOCALAPPDATA}/Spotify/Storage");
  return r;
}

// Expand ${VAR} and per-segment wildcards (* ?) into existing paths.
static std::vector<fs::path> expand_pattern(const std::string& pat) {
  std::string s;
  for (size_t i = 0; i < pat.size();) {
    if (pat.compare(i, 2, "${") == 0) {
      size_t e = pat.find('}', i);
      if (e == std::string::npos) return {};
      auto v = plat::env(pat.substr(i + 2, e - i - 2).c_str());
      if (!v || v->empty()) return {};
      s += *v;
      i = e + 1;
    } else s += pat[i++];
  }
  for (auto& c : s) if (c == '\\') c = '/';
  auto parts = split(s, '/');
  fs::path cur = (!s.empty() && s[0] == '/') ? fs::path("/") : fs::path();
  size_t idx = 0;
  for (; idx < parts.size(); ++idx) {
    const std::string& seg = parts[idx];
    if (seg.empty()) continue;
    if (seg.find_first_of("*?") != std::string::npos) break;
    if (cur.empty() && seg.back() == ':') cur = P(seg + "/");
    else cur /= P(seg);
  }
  std::error_code ec;
  if (cur.empty() || !fs::exists(cur, ec)) return {};
  std::vector<fs::path> level{cur};
  for (; idx < parts.size() && !level.empty(); ++idx) {
    const std::string& seg = parts[idx];
    if (seg.empty()) continue;
    bool wild = seg.find_first_of("*?") != std::string::npos;
    std::vector<fs::path> next;
    for (auto& d : level) {
      std::error_code e2;
      if (!wild) {
        fs::path c = d / P(seg);
        if (fs::exists(c, e2)) next.push_back(c);
      } else {
        for (fs::directory_iterator it(d, e2), end; !e2 && it != end; it.increment(e2))
          if (wild_match(seg.c_str(), u8(it->path().filename()).c_str())) next.push_back(it->path());
      }
    }
    level.swap(next);
  }
  return level;
}

struct Target { std::string rule, note; fs::path root; double min_age; };

// Discovery cache: this only accelerates previews.  Every --yes operation walks
// the target again, then applies the normal protected-path, link and lock checks.
// Entries are deliberately short-lived because a directory timestamp alone is
// not proof that every descendant is unchanged.
struct ScanCacheEntry {
  std::string rule, root;
  int64_t stamp = 0;
  double age = 0;
  Stats st;
  std::time_t saved = 0;
};

static fs::path scan_cache_path() { return state_dir() / "scan-cache.tsv"; }

static int64_t target_stamp(const fs::path& p) {
  std::error_code ec;
  auto t = fs::last_write_time(p, ec);
  return ec ? 0 : (int64_t)t.time_since_epoch().count();
}

static std::vector<ScanCacheEntry> load_scan_cache() {
  std::vector<ScanCacheEntry> out;
  std::ifstream f(scan_cache_path());
  std::string line;
  while (std::getline(f, line)) {
    auto v = split(line, '\t');
    if (v.size() != 9) continue;
    try {
      ScanCacheEntry e;
      e.rule = unesc_tsv(v[0]); e.root = unesc_tsv(v[1]); e.stamp = std::stoll(v[2]); e.age = std::stod(v[3]);
      e.st.files = std::stoull(v[4]); e.st.bytes = std::stoull(v[5]); e.st.failed = std::stoull(v[6]);
      e.st.failed_bytes = std::stoull(v[7]); e.saved = (std::time_t)std::stoll(v[8]);
      out.push_back(std::move(e));
    } catch (...) { /* Ignore a corrupt row; the target will be refreshed. */ }
  }
  return out;
}

static void save_scan_cache(const std::vector<ScanCacheEntry>& entries) {
  fs::path dst = scan_cache_path(), tmp = dst; tmp += ".tmp";
  std::ofstream f(tmp, std::ios::trunc);
  if (!f) return;
  for (const auto& e : entries)
    f << esc_tsv(e.rule) << '\t' << esc_tsv(e.root) << '\t' << e.stamp << '\t' << e.age << '\t'
      << e.st.files << '\t' << e.st.bytes << '\t' << e.st.failed << '\t' << e.st.failed_bytes << '\t' << e.saved << '\n';
  f.close();
  std::error_code ec;
  fs::rename(tmp, dst, ec);
  if (ec) { fs::remove(dst, ec); ec.clear(); fs::rename(tmp, dst, ec); }
}

static const ScanCacheEntry* cache_hit(const std::vector<ScanCacheEntry>& entries, const Target& t, double age) {
  const std::time_t now = std::time(nullptr);
  const std::string root = norm_key(t.root);
  const int64_t stamp = target_stamp(t.root);
  for (const auto& e : entries)
    if (e.rule == t.rule && e.root == root && e.stamp == stamp && e.age == age && !e.st.failed &&
        now >= e.saved && now - e.saved <= 300)
      return &e;
  return nullptr;
}

static std::vector<Target> resolve_targets(unsigned mask, std::vector<std::string>& skipped) {
  bool admin = plat::is_elevated();
  auto rules = builtin_rules();
  rules.insert(rules.end(), g_custom.begin(), g_custom.end());
  std::vector<Target> t;
  std::set<std::string> skip_set;
  for (auto& r : rules) {
    if (!(r.mask & mask)) continue;
    if (r.admin && !admin) { skip_set.insert(r.id); continue; }
    for (auto& p : expand_pattern(r.pattern)) {
      if (is_excluded(p)) continue;
      if (is_protected(p)) { // refuse early so it cannot shadow legitimate targets below it
        if (!G.quiet) std::fprintf(stderr, "refused (protected location): %s  [rule %s]\n", u8(p).c_str(), r.id.c_str());
        continue;
      }
      t.push_back({r.id, r.note, p, r.min_age});
    }
  }
  // Drop targets nested inside another target (avoids double counting / double deletion).
  std::vector<Target> kept;
  for (auto& x : t) {
    bool nested = false;
    for (auto& y : t)
      if (&x != &y && norm_key(x.root) != norm_key(y.root) && path_under(x.root, y.root)) { nested = true; break; }
    bool dup = false;
    for (auto& k : kept) if (norm_key(k.root) == norm_key(x.root)) { dup = true; break; }
    if (!nested && !dup) kept.push_back(x);
  }
  skipped.assign(skip_set.begin(), skip_set.end());
  return kept;
}

// Recursively clean a cache directory. Returns true if the directory ended up empty.
static bool clean_tree(const fs::path& dir, double min_age, bool apply, Stats& st, int depth) {
  std::error_code ec;
  bool empty = true;
  fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end;
  if (ec) return false;
  for (; it != end; it.increment(ec)) {
    if (ec) break;
    tick();
    const fs::path p = it->path();
    if (is_excluded(p)) { empty = false; continue; }
    std::error_code e2;
    auto sty = it->symlink_status(e2);
    if (e2) { empty = false; continue; }
    bool link = sty.type() == fs::file_type::symlink;
#ifdef _MSC_VER
    link = link || sty.type() == fs::file_type::junction;
#endif
    if (link) { empty = false; continue; } // never follow or delete links
    if (fs::is_directory(sty)) {
      if (plat::is_reparse_dir(p) || depth >= 48) { empty = false; continue; }
      bool sub_empty = clean_tree(p, min_age, apply, st, depth + 1);
      if (sub_empty && apply) { fs::remove(p, e2); if (e2) empty = false; }
      else empty = false;
    } else {
      uint64_t sz = it->file_size(e2);
      if (e2) { sz = 0; e2.clear(); }
      bool old_enough = min_age <= 0;
      if (!old_enough) {
        auto mt = it->last_write_time(e2);
        old_enough = !e2 && age_days(mt) >= min_age;
      }
      if (!old_enough) { empty = false; continue; }
      if (apply) {
        fs::remove(p, e2);
        if (e2) { st.failed++; st.failed_bytes += sz; empty = false; continue; } // in use: skip quietly
      }
      st.files++;
      st.bytes += sz;
    }
  }
  return empty;
}

static Stats process_target(const Target& t, double override_age, bool apply, std::string& warn) {
  Stats st;
  double age = override_age >= 0 ? override_age : t.min_age;
  std::error_code ec;
  if (is_protected(t.root)) { warn = "protected path, skipped"; return st; }
  auto sty = fs::symlink_status(t.root, ec);
  if (ec || sty.type() == fs::file_type::symlink || plat::is_reparse_dir(t.root)) { warn = "link, skipped"; return st; }
  if (fs::is_directory(sty)) {
    clean_tree(t.root, age, apply, st, 0);
  } else if (fs::is_regular_file(sty)) {
    uint64_t sz = fs::file_size(t.root, ec);
    bool ok = age <= 0 || age_days(fs::last_write_time(t.root, ec)) >= age;
    if (ok && !ec) {
      if (apply) { fs::remove(t.root, ec); if (ec) { st.failed++; st.failed_bytes += sz; return st; } }
      st.files = 1; st.bytes = sz;
    }
  }
  return st;
}

std::vector<RuleResult> run_clean(unsigned mask, double override_age, bool apply, std::vector<std::string>& skipped,
                                  const std::set<std::string>& only_rules) {
  std::vector<RuleResult> out;
  const bool use_cache = !apply && !G.refresh;
  std::vector<ScanCacheEntry> cache = use_cache ? load_scan_cache() : std::vector<ScanCacheEntry>{};
  std::vector<ScanCacheEntry> updated = cache;
  for (auto& t : resolve_targets(mask, skipped)) {
    if (!only_rules.empty() && !only_rules.count(t.rule)) continue;
    RuleResult rr;
    rr.rule = t.rule; rr.note = t.note; rr.path = u8(t.root);
    const double age = override_age >= 0 ? override_age : t.min_age;
    if (use_cache) {
      if (const auto* hit = cache_hit(cache, t, age)) { rr.st = hit->st; rr.cached = true; }
      else rr.st = process_target(t, override_age, false, rr.warn);
    } else rr.st = process_target(t, override_age, apply, rr.warn);
    if (!apply && !rr.cached && !rr.st.failed) {
      ScanCacheEntry e{t.rule, norm_key(t.root), target_stamp(t.root), age, rr.st, std::time(nullptr)};
      updated.erase(std::remove_if(updated.begin(), updated.end(), [&](const ScanCacheEntry& old) {
        return old.rule == e.rule && old.root == e.root && old.age == e.age;
      }), updated.end());
      updated.push_back(std::move(e));
    }
    out.push_back(std::move(rr));
  }
  if (!apply) {
    if (updated.size() > 4096) updated.erase(updated.begin(), updated.begin() + (long)(updated.size() - 4096));
    save_scan_cache(updated);
  }
  return out;
}

int cmd_clean(const Args& a, bool force_dry) {
  std::string bad;
  unsigned mask = parse_profiles(a.get("profile", "quick"), bad);
  if (!bad.empty() || !mask) {
    std::fprintf(stderr, "unknown profile '%s' (quick, browser, dev, gaming, windows, apps, custom, full)\n", bad.c_str());
    return 64;
  }
  double age = a.has("min-age") ? std::atof(a.get("min-age").c_str()) : -1;
  bool apply = !force_dry && !a.has("dry-run") && G.yes;
  std::vector<std::string> skipped;
  auto selected_rules = a.all("rule");
  std::set<std::string> only_rules(selected_rules.begin(), selected_rules.end());
  auto res = run_clean(mask, age, apply, skipped, only_rules);

  // aggregate by rule id, keeping first-seen order
  std::vector<std::string> order;
  std::map<std::string, RuleResult> agg;
  std::map<std::string, std::vector<std::string>> paths;
  for (auto& r : res) {
    if (!agg.count(r.rule)) { order.push_back(r.rule); agg[r.rule] = r; agg[r.rule].st = Stats{}; }
    auto& x = agg[r.rule];
    x.st.files += r.st.files; x.st.bytes += r.st.bytes; x.st.failed += r.st.failed; x.st.failed_bytes += r.st.failed_bytes;
    paths[r.rule].push_back(r.path);
  }
  Stats tot;
  uint64_t cache_hits = 0;
  for (auto& k : order) { tot.files += agg[k].st.files; tot.bytes += agg[k].st.bytes; tot.failed += agg[k].st.failed; }
  for (const auto& r : res) if (r.cached) ++cache_hits;

  if (G.json) {
    std::printf("{\"mode\":\"%s\",\"cache_hits\":%llu,\"rules_refreshed\":%llu,\"total_bytes\":%llu,\"total_files\":%llu,\"failed\":%llu,\"rules\":[", apply ? "clean" : "dry-run",
                (unsigned long long)cache_hits, (unsigned long long)(res.size() - cache_hits), (unsigned long long)tot.bytes,
                (unsigned long long)tot.files, (unsigned long long)tot.failed);
    for (size_t i = 0; i < order.size(); ++i) {
      auto& x = agg[order[i]];
      bool cached = true;
      for (const auto& r : res) if (r.rule == x.rule && !r.cached) { cached = false; break; }
      std::printf("%s{\"rule\":\"%s\",\"note\":\"%s\",\"cached\":%s,\"bytes\":%llu,\"files\":%llu,\"failed\":%llu,\"paths\":[", i ? "," : "", x.rule.c_str(), jesc(x.note).c_str(), cached ? "true" : "false",
                  (unsigned long long)x.st.bytes, (unsigned long long)x.st.files, (unsigned long long)x.st.failed);
      for (size_t j = 0; j < paths[order[i]].size(); ++j) std::printf("%s\"%s\"", j ? "," : "", jesc(paths[order[i]][j]).c_str());
      std::printf("]}");
    }
    std::printf("],\"skipped_needs_admin\":[");
    for (size_t i = 0; i < skipped.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", skipped[i].c_str());
    std::printf("]}\n");
    return tot.failed ? 2 : 0;
  }

  if (!G.quiet) {
    std::printf("%-18s %12s %10s\n", "RULE", apply ? "FREED" : "RECLAIMABLE", "FILES");
    for (auto& k : order) {
      auto& x = agg[k];
      if (!x.st.bytes && !x.st.files && !G.verbose) continue;
      std::printf("%-18s %12s %10s%s\n", k.c_str(), human(x.st.bytes).c_str(), commas(x.st.files).c_str(),
                  x.st.failed ? ("   (" + std::to_string(x.st.failed) + " in use, skipped)").c_str() : "");
      if (G.verbose) for (auto& p : paths[k]) std::printf("    %s\n", p.c_str());
    }
    std::printf("%-18s %12s %10s\n", "TOTAL", human(tot.bytes).c_str(), commas(tot.files).c_str());
    if (!skipped.empty()) {
      std::printf("Skipped (run as administrator): ");
      for (size_t i = 0; i < skipped.size(); ++i) std::printf("%s%s", i ? ", " : "", skipped[i].c_str());
      std::printf("\n");
    }
    if (!apply) std::printf("Preview only. Re-run `cna clean --profile %s --yes` to delete.\n", a.get("profile", "quick").c_str());
  }
  return tot.failed ? 2 : 0;
}

} // namespace cna
