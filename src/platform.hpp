#pragma once
// OS-specific pieces live behind this header (platform.cpp).
#include <optional>

#include "common.hpp"

namespace cna::plat {

struct App {
  std::string name, publisher, alias, install_loc, version, uninstall, quiet_uninstall, source;
};

struct FileId {
  uint64_t a = 0, b = 0;
  bool ok = false;
  bool operator==(const FileId& o) const { return ok && o.ok && a == o.a && b == o.b; }
};

std::optional<std::string> env(const char* name);
void console_init();
std::vector<std::string> utf8_args(int argc, char** argv);
fs::path exe_path();

size_t private_bytes();                               // committed/resident bytes of this process
bool apply_mem_cap(size_t bytes, std::string& msg);   // hard OS-level cap
bool is_elevated();

bool is_reparse_dir(const fs::path& p);               // junction / mount point
bool is_cloud_placeholder(const fs::path& p);         // OneDrive etc: reading would download
FileId file_id(const fs::path& p);                    // detects hard links

std::vector<App> installed_apps();                    // registry on Windows
std::vector<std::string> registry_find(const std::string& term_lower); // report-only

// ---- registry access (Windows: real registry; POSIX: a file-backed fake used for development/tests) ----
enum class RegType { Sz, Dword, Binary };
struct RegValue {
  bool exists = false;
  RegType type = RegType::Sz;
  std::string data; // Sz: UTF-8 text, Dword: decimal, Binary: lowercase hex
};
RegValue reg_get(const std::string& hive, const std::string& key, const std::string& name);
bool reg_set(const std::string& hive, const std::string& key, const std::string& name, RegType t,
             const std::string& data, std::string& err);
bool reg_delete_value(const std::string& hive, const std::string& key, const std::string& name, std::string& err);
std::vector<std::string> reg_values(const std::string& hive, const std::string& key);
bool reg_key_exists(const std::string& hive, const std::string& key);
std::vector<std::string> appx_full_names(); // installed Store packages, e.g. Microsoft.BingNews_4.55_x64__8wekyb3d8bbwe
int run_command(const std::string& cmdline, std::string& out); // Windows: via cmd /c with captured output

int schedule_create(const std::string& task, const std::string& exe, int every_hours, std::string& msg);
int schedule_delete(const std::string& task, std::string& msg);
int schedule_query(const std::string& task, std::string& msg);

} // namespace cna::plat
