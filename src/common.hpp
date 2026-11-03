#pragma once
// cna - shared helpers. Everything here is portable C++17.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace cna {

#ifndef CNA_VERSION
#define CNA_VERSION "0.1.0" // normally injected from the VERSION file by the build
#endif
constexpr const char* VERSION = CNA_VERSION;

// Thrown when real process memory gets close to the configured cap.
struct CapExceeded : std::runtime_error {
  using std::runtime_error::runtime_error;
};

inline std::string u8(const fs::path& p) {
  auto s = p.u8string();
  return std::string(s.begin(), s.end());
}
inline fs::path P(const std::string& s) { return fs::u8path(s); }

struct Ctx {
  bool json = false, yes = false, quiet = false, verbose = false;
  size_t mem_cap = 125u * 1024 * 1024;
  fs::path state;                 // explicit --state override
  std::vector<fs::path> excludes; // never touched by any command
};
extern Ctx G;

// ---- strings -------------------------------------------------------------
std::string lower(std::string s);
std::string trim(const std::string& s);
std::vector<std::string> split(const std::string& s, char d);
std::string human(uint64_t bytes);
std::string commas(uint64_t n);
bool parse_size(const std::string& s, uint64_t& out);
std::string jesc(const std::string& s);
std::string esc_tsv(const std::string& s);
std::string unesc_tsv(const std::string& s);
std::string norm_alnum(const std::string& s);          // lowercase a-z0-9 only
std::vector<std::string> tokens(const std::string& s); // lowercase words, len>=3
bool wild_match(const char* pat, const char* s);       // case-insensitive * ?

// ---- paths / safety ------------------------------------------------------
std::string norm_key(const fs::path& p);
bool path_under(const fs::path& child, const fs::path& parent);
bool is_excluded(const fs::path& p);
bool is_protected(const fs::path& p); // roots we refuse to clean / move
std::vector<fs::path> protected_dirs();
fs::path home_dir();
fs::path state_dir();

// ---- time ----------------------------------------------------------------
double age_days(fs::file_time_type t);
std::time_t to_time_t(fs::file_time_type t);
std::string fmt_time(std::time_t t);

// ---- memory guard --------------------------------------------------------
void tick();       // cheap; checks real memory every ~2k calls
void check_mem();  // throws CapExceeded

// ---- bounded directory walker -------------------------------------------
struct Entry {
  fs::path path;
  bool is_dir;
  uint64_t size;
  fs::file_time_type mtime;
  int depth;
};
// Never follows symlinks/junctions. For directories the visitor returns
// false to prune. Streams entries; only the stack of pending dirs is kept.
void walk(const fs::path& root, const std::function<bool(const Entry&)>& visit,
          int max_depth = 64);

struct TreeInfo {
  uint64_t bytes = 0, files = 0, dirs = 0, exes = 0;
  bool truncated = false, uninstaller = false, any = false;
  fs::file_time_type newest{};
  std::map<std::string, std::pair<uint64_t, uint64_t>> ext; // ext -> (count, bytes)
};
TreeInfo tree_info(const fs::path& root, uint64_t max_entries = 0, bool collect_ext = false);

// ---- argument parsing ----------------------------------------------------
struct Args {
  std::vector<std::string> pos;
  std::map<std::string, std::vector<std::string>> opt;
  bool has(const std::string& k) const { return opt.count(k) > 0; }
  std::string get(const std::string& k, const std::string& d = "") const {
    auto it = opt.find(k);
    return (it == opt.end() || it->second.empty()) ? d : it->second.back();
  }
  std::vector<std::string> all(const std::string& k) const {
    auto it = opt.find(k);
    return it == opt.end() ? std::vector<std::string>{} : it->second;
  }
};
Args parse_args(const std::vector<std::string>& v, size_t from);

} // namespace cna
