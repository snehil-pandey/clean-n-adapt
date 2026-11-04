#include "common.hpp"

#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>

#include "platform.hpp"

namespace cna {

Ctx G;

// ---- strings -------------------------------------------------------------
std::string lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}
std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}
std::vector<std::string> split(const std::string& s, char d) {
  std::vector<std::string> out;
  size_t st = 0;
  for (;;) {
    size_t i = s.find(d, st);
    if (i == std::string::npos) { out.push_back(s.substr(st)); break; }
    out.push_back(s.substr(st, i - st));
    st = i + 1;
  }
  return out;
}
std::string human(uint64_t b) {
  const char* u[] = {"B", "KB", "MB", "GB", "TB"};
  double v = (double)b;
  int i = 0;
  while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
  char buf[32];
  std::snprintf(buf, sizeof buf, i ? "%.1f %s" : "%.0f %s", v, u[i]);
  return buf;
}
std::string commas(uint64_t n) {
  std::string s = std::to_string(n), o;
  int c = 0;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    if (c && c % 3 == 0) o += ',';
    o += *it;
    ++c;
  }
  std::reverse(o.begin(), o.end());
  return o;
}
bool parse_size(const std::string& in, uint64_t& out) {
  std::string s = trim(in);
  if (s.empty()) return false;
  char* end = nullptr;
  double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || v < 0) return false;
  std::string suf = lower(trim(end));
  if (!suf.empty() && suf.back() == 'b') suf.pop_back();
  double mul = 1;
  if (suf == "k") mul = 1024.0;
  else if (suf == "m") mul = 1024.0 * 1024;
  else if (suf == "g") mul = 1024.0 * 1024 * 1024;
  else if (suf == "t") mul = 1024.0 * 1024 * 1024 * 1024;
  else if (!suf.empty()) return false;
  out = (uint64_t)(v * mul);
  return true;
}
std::string jesc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n"; break;
      case '\r': o += "\\r"; break;
      case '\t': o += "\\t"; break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += (char)c;
    }
  }
  return o;
}
std::string esc_tsv(const std::string& s) {
  std::string o;
  for (char c : s) {
    if (c == '\t') o += "\\t";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\\') o += "\\\\";
    else o += c;
  }
  return o;
}
std::string unesc_tsv(const std::string& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      char n = s[++i];
      o += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    } else o += s[i];
  }
  return o;
}
std::string norm_alnum(const std::string& s) {
  std::string o;
  for (unsigned char c : s) if (std::isalnum(c)) o += (char)std::tolower(c);
  return o;
}
std::vector<std::string> tokens(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] { if (cur.size() >= 3) out.push_back(cur); cur.clear(); };
  for (unsigned char c : s) {
    if (std::isalnum(c)) cur += (char)std::tolower(c);
    else flush();
  }
  flush();
  return out;
}
bool wild_match(const char* p, const char* s) {
  const char *star = nullptr, *ss = s;
  while (*s) {
    if (*p == '?' || std::tolower((unsigned char)*p) == std::tolower((unsigned char)*s)) { ++p; ++s; }
    else if (*p == '*') { star = p++; ss = s; }
    else if (star) { p = star + 1; s = ++ss; }
    else return false;
  }
  while (*p == '*') ++p;
  return *p == 0;
}

// ---- paths ---------------------------------------------------------------
std::string norm_key(const fs::path& p) {
  std::string s = lower(u8(p.lexically_normal()));
  for (auto& c : s) if (c == '\\') c = '/';
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  return s;
}
bool path_under(const fs::path& child, const fs::path& parent) {
  std::string c = norm_key(child), p = norm_key(parent);
  if (c == p) return true;
  if (p.empty()) return false;
  if (p.back() != '/') p += '/';
  return c.compare(0, p.size(), p) == 0;
}
bool is_excluded(const fs::path& p) {
  for (auto& e : G.excludes) if (path_under(p, e)) return true;
  return false;
}
fs::path home_dir() {
  if (auto e = plat::env("USERPROFILE")) return P(*e);
  if (auto e = plat::env("HOME")) return P(*e);
  return fs::path();
}
std::vector<fs::path> protected_dirs() {
  std::vector<fs::path> v;
  for (const char* n : {"USERPROFILE", "HOME", "APPDATA", "LOCALAPPDATA", "PROGRAMFILES", "PROGRAMFILES(X86)",
                        "PROGRAMDATA", "SystemRoot", "SystemDrive"}) {
    if (auto e = plat::env(n)) {
      if (e->empty()) continue;
      std::string s = *e;
      if (std::string(n) == "SystemDrive") s += "/";
      v.push_back(P(s));
    }
  }
  fs::path h = home_dir();
  if (!h.empty())
    for (const char* d : {"Desktop", "Documents", "Downloads", "Pictures", "Videos", "Music", "OneDrive"})
      v.push_back(h / d);
  return v;
}
bool is_protected(const fs::path& p) {
  fs::path rel = p.lexically_normal().relative_path();
  long depth = std::distance(rel.begin(), rel.end());
  if (depth < 2) return true; // drive roots, "C:\Users", etc.
  std::string k = norm_key(p);
  for (auto& d : protected_dirs()) {
    if (k == norm_key(d)) return true;
    if (path_under(d, p)) return true; // p is an ancestor of something precious
  }
  return false;
}
fs::path state_dir() {
  static fs::path cached;
  if (!cached.empty()) return cached;
  auto try_dir = [](const fs::path& d) -> bool {
    std::error_code ec;
    fs::create_directories(d, ec);
    if (ec) return false;
    fs::path probe = d / ".probe";
    { std::ofstream f(probe); if (!f) return false; }
    fs::remove(probe, ec);
    return true;
  };
  if (!G.state.empty() && try_dir(G.state)) return cached = G.state;
  if (auto e = plat::env("CNA_HOME")) { fs::path d = P(*e); if (try_dir(d)) return cached = d; }
  // Portable mode: a ".state" folder that already exists next to the exe is used (create it to opt in).
  // Otherwise data lives in %LOCALAPPDATA%\cna so it survives upgrades and uninstalls of the program folder.
  fs::path exe = plat::exe_path();
  if (!exe.empty()) {
    fs::path d = exe.parent_path() / ".state";
    std::error_code ec;
    if (fs::is_directory(d, ec) && try_dir(d)) return cached = d;
  }
  if (auto e = plat::env("LOCALAPPDATA")) { fs::path d = P(*e) / "cna"; if (try_dir(d)) return cached = d; }
  if (auto e = plat::env("XDG_STATE_HOME")) { fs::path d = P(*e) / "cna"; if (try_dir(d)) return cached = d; }
  fs::path d = home_dir() / ".local" / "state" / "cna";
  try_dir(d);
  return cached = d;
}

// ---- time ----------------------------------------------------------------
double age_days(fs::file_time_type t) {
  auto d = fs::file_time_type::clock::now() - t;
  return std::chrono::duration<double, std::ratio<86400>>(d).count();
}
std::time_t to_time_t(fs::file_time_type t) {
  auto delta = std::chrono::duration_cast<std::chrono::system_clock::duration>(fs::file_time_type::clock::now() - t);
  return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now() - delta);
}
std::string fmt_time(std::time_t t) {
  char buf[32];
  std::tm tmv{};
#ifdef _WIN32
  localtime_s(&tmv, &t);
#else
  localtime_r(&t, &tmv);
#endif
  std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &tmv);
  return buf;
}

// ---- memory guard --------------------------------------------------------
static unsigned long g_ticks = 0;
void check_mem() {
  size_t used = plat::private_bytes();
  if (used > (size_t)((double)G.mem_cap * 0.9))
    throw CapExceeded("memory budget reached (" + human(used) + " of " + human(G.mem_cap) +
                      "); narrow the scan roots or raise --min-size");
}
void tick() {
  if ((++g_ticks & 2047u) == 0) check_mem();
}

// ---- walker --------------------------------------------------------------
void walk(const fs::path& root, const std::function<bool(const Entry&)>& visit, int max_depth) {
  struct Frame { fs::path p; int depth; };
  std::vector<Frame> stack;
  stack.push_back({root, 0});
  while (!stack.empty()) {
    Frame f = std::move(stack.back());
    stack.pop_back();
    std::error_code ec;
    fs::directory_iterator it(f.p, fs::directory_options::skip_permission_denied, ec), end;
    if (ec) continue;
    for (; it != end; it.increment(ec)) {
      if (ec) break;
      tick();
      const fs::directory_entry& de = *it;
      const fs::path& p = de.path();
      if (is_excluded(p)) continue;
      std::error_code e2;
      auto st = de.symlink_status(e2);
      if (e2) continue;
      if (st.type() == fs::file_type::symlink) continue;
#ifdef _MSC_VER
      if (st.type() == fs::file_type::junction) continue;
#endif
      bool isdir = fs::is_directory(st);
      Entry en{p, isdir, 0, {}, f.depth + 1};
      if (isdir) {
        if (plat::is_reparse_dir(p)) continue;
        en.mtime = de.last_write_time(e2);
        if (e2) en.mtime = {};
      } else {
        if (!fs::is_regular_file(st)) continue;
        en.size = de.file_size(e2);
        if (e2) continue;
        en.mtime = de.last_write_time(e2);
        if (e2) en.mtime = {};
      }
      bool descend = visit(en);
      if (isdir && descend && f.depth + 1 < max_depth) stack.push_back({p, f.depth + 1});
    }
  }
}

namespace { struct StopWalk {}; }

TreeInfo tree_info(const fs::path& root, uint64_t max_entries, bool collect_ext) {
  TreeInfo t;
  uint64_t seen = 0;
  try {
    walk(root, [&](const Entry& e) {
      if (max_entries && ++seen > max_entries) { t.truncated = true; throw StopWalk{}; }
      if (e.is_dir) { t.dirs++; return true; }
      t.files++;
      t.bytes += e.size;
      if (!t.any || e.mtime > t.newest) { t.newest = e.mtime; t.any = true; }
      std::string n = lower(u8(e.path.filename()));
      auto dot = n.rfind('.');
      std::string ext = dot == std::string::npos ? "" : n.substr(dot);
      if (ext == ".exe" || ext == ".dll") t.exes++;
      if (ext == ".exe" && (n.rfind("unins", 0) == 0 || n.rfind("uninstall", 0) == 0)) t.uninstaller = true;
      if (collect_ext) { auto& x = t.ext[ext]; x.first++; x.second += e.size; }
      return true;
    });
  } catch (const StopWalk&) {}
  return t;
}

// ---- args ----------------------------------------------------------------
Args parse_args(const std::vector<std::string>& v, size_t from) {
  static const std::set<std::string> valued = {
      "profile", "min-age", "min-size", "max-size", "mem-cap", "apps-file", "min-score", "every-hours", "rule",
      "older-than", "config", "state", "limit", "of", "app", "depth", "root", "exclude", "select", "reason", "label", "preset", "only", "id", "packages-file"};
  Args a;
  for (size_t i = from; i < v.size(); ++i) {
    const std::string& s = v[i];
    if (s.rfind("--", 0) == 0 && s.size() > 2) {
      std::string k = s.substr(2), val;
      bool has_val = false;
      auto eq = k.find('=');
      if (eq != std::string::npos) { val = k.substr(eq + 1); k = k.substr(0, eq); has_val = true; }
      else if (valued.count(k) && i + 1 < v.size()) { val = v[++i]; has_val = true; }
      a.opt[k].push_back(has_val ? val : "");
    } else if (s == "-y") a.opt["yes"].push_back("");
    else if (s == "-q") a.opt["quiet"].push_back("");
    else if (s == "-v") a.opt["verbose"].push_back("");
    else if (s == "-h") a.opt["help"].push_back("");
    else a.pos.push_back(s);
  }
  return a;
}

} // namespace cna
