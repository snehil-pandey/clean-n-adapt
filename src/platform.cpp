#include "platform.hpp"

#include <cstdlib>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#else
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cna::plat {

#ifdef _WIN32
// ===========================================================================
// Windows
// ===========================================================================
static std::wstring u2w(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
static std::string w2u(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

std::optional<std::string> env(const char* name) {
  std::wstring wn = u2w(name);
  DWORD n = GetEnvironmentVariableW(wn.c_str(), nullptr, 0);
  if (n == 0) return std::nullopt;
  std::wstring buf(n, L'\0');
  DWORD got = GetEnvironmentVariableW(wn.c_str(), &buf[0], n);
  buf.resize(got);
  return w2u(buf);
}

void console_init() {
  SetConsoleOutputCP(CP_UTF8);
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
    SetConsoleMode(h, mode | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/);
}

std::vector<std::string> utf8_args(int, char**) {
  int n = 0;
  LPWSTR* w = CommandLineToArgvW(GetCommandLineW(), &n);
  std::vector<std::string> out;
  for (int i = 0; w && i < n; ++i) out.push_back(w2u(w[i]));
  if (w) LocalFree(w);
  return out;
}

fs::path exe_path() {
  wchar_t buf[MAX_PATH * 4];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
  return n ? fs::path(std::wstring(buf, n)) : fs::path();
}

size_t private_bytes() {
  PROCESS_MEMORY_COUNTERS_EX pmc{};
  if (!GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) return 0;
  return (size_t)pmc.PrivateUsage;
}

bool apply_mem_cap(size_t bytes, std::string& msg) {
  // A job object with a per-process commit limit is enforced by the kernel:
  // an allocation beyond the cap fails (we turn that into a clean error).
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  if (!job) { msg = "CreateJobObject failed"; return false; }
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
  info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
  info.ProcessMemoryLimit = bytes;
  if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
    msg = "SetInformationJobObject failed"; CloseHandle(job); return false;
  }
  if (!AssignProcessToJobObject(job, GetCurrentProcess())) {
    msg = "AssignProcessToJobObject failed (already in a restrictive job?)"; CloseHandle(job); return false;
  }
  return true; // handle intentionally kept open for the process lifetime
}

bool is_elevated() {
  HANDLE tok = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return false;
  TOKEN_ELEVATION el{};
  DWORD sz = 0;
  bool ok = GetTokenInformation(tok, TokenElevation, &el, sizeof(el), &sz) && el.TokenIsElevated;
  CloseHandle(tok);
  return ok;
}

bool is_reparse_dir(const fs::path& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool is_cloud_placeholder(const fs::path& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  if (a == INVALID_FILE_ATTRIBUTES) return false;
  // OFFLINE | RECALL_ON_OPEN | RECALL_ON_DATA_ACCESS
  return (a & (0x1000 | 0x40000 | 0x400000)) != 0;
}

FileId file_id(const fs::path& p) {
  FileId id;
  HANDLE h = CreateFileW(p.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (h == INVALID_HANDLE_VALUE) return id;
  BY_HANDLE_FILE_INFORMATION bi{};
  if (GetFileInformationByHandle(h, &bi)) {
    id.a = bi.dwVolumeSerialNumber;
    id.b = ((uint64_t)bi.nFileIndexHigh << 32) | bi.nFileIndexLow;
    id.ok = true;
  }
  CloseHandle(h);
  return id;
}

static std::wstring reg_str(HKEY k, const wchar_t* name) {
  DWORD type = 0, sz = 0;
  if (RegQueryValueExW(k, name, nullptr, &type, nullptr, &sz) != ERROR_SUCCESS) return L"";
  if (type != REG_SZ && type != REG_EXPAND_SZ) return L"";
  std::wstring buf(sz / sizeof(wchar_t) + 1, L'\0');
  if (RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)&buf[0], &sz) != ERROR_SUCCESS) return L"";
  buf.resize(wcslen(buf.c_str()));
  return buf;
}

static void read_uninstall(HKEY root, const wchar_t* path, REGSAM extra, const char* src, std::vector<App>& out) {
  HKEY k;
  if (RegOpenKeyExW(root, path, 0, KEY_READ | extra, &k) != ERROR_SUCCESS) return;
  for (DWORD i = 0;; ++i) {
    wchar_t name[512];
    DWORD n = 512;
    LONG r = RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr);
    if (r == ERROR_NO_MORE_ITEMS) break;
    if (r != ERROR_SUCCESS) continue;
    HKEY sk;
    if (RegOpenKeyExW(k, name, 0, KEY_READ | extra, &sk) != ERROR_SUCCESS) continue;
    App a;
    a.name = w2u(reg_str(sk, L"DisplayName"));
    a.publisher = w2u(reg_str(sk, L"Publisher"));
    a.version = w2u(reg_str(sk, L"DisplayVersion"));
    a.uninstall = w2u(reg_str(sk, L"UninstallString"));
    a.quiet_uninstall = w2u(reg_str(sk, L"QuietUninstallString"));
    std::wstring loc = reg_str(sk, L"InstallLocation");
    if (loc.empty()) {
      std::wstring icon = reg_str(sk, L"DisplayIcon");
      if (!icon.empty()) {
        if (icon[0] == L'"') { size_t e = icon.find(L'"', 1); icon = icon.substr(1, e == std::wstring::npos ? e : e - 1); }
        size_t comma = icon.rfind(L',');
        if (comma != std::wstring::npos) icon.resize(comma);
        fs::path pp(icon);
        if (pp.has_parent_path() && pp.has_extension()) loc = pp.parent_path().wstring();
      }
    }
    while (!loc.empty() && (loc.back() == L'\\' || loc.back() == L'/' || loc.back() == L'"')) loc.pop_back();
    if (!loc.empty() && loc[0] == L'"') loc.erase(0, 1);
    a.install_loc = w2u(loc);
    std::string key = w2u(name);
    if (!key.empty() && key[0] != '{') a.alias = key;
    a.source = src;
    RegCloseKey(sk);
    if (!a.name.empty() || !a.alias.empty()) out.push_back(std::move(a));
  }
  RegCloseKey(k);
}

std::vector<App> installed_apps() {
  std::vector<App> out;
  const wchar_t* u = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
  read_uninstall(HKEY_LOCAL_MACHINE, u, KEY_WOW64_64KEY, "hklm", out);
  read_uninstall(HKEY_LOCAL_MACHINE, u, KEY_WOW64_32KEY, "hklm32", out);
  read_uninstall(HKEY_CURRENT_USER, u, 0, "hkcu", out);
  // Store (AppX) packages: names look like Vendor.App_1.2.3.4_x64__hash
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages",
                    0, KEY_READ, &k) == ERROR_SUCCESS) {
    for (DWORD i = 0;; ++i) {
      wchar_t name[512];
      DWORD n = 512;
      LONG r = RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr);
      if (r == ERROR_NO_MORE_ITEMS) break;
      if (r != ERROR_SUCCESS) continue;
      std::string s = w2u(name);
      App a;
      a.name = s.substr(0, s.find('_'));
      a.source = "appx";
      if (!a.name.empty()) out.push_back(std::move(a));
    }
    RegCloseKey(k);
  }
  return out;
}

static void find_in_key(HKEY root, const char* rootname, const std::wstring& path, REGSAM extra,
                        const std::string& term, int depth, std::vector<std::string>& out) {
  HKEY k;
  if (RegOpenKeyExW(root, path.c_str(), 0, KEY_READ | extra, &k) != ERROR_SUCCESS) return;
  for (DWORD i = 0;; ++i) {
    wchar_t name[512];
    DWORD n = 512;
    LONG r = RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr);
    if (r == ERROR_NO_MORE_ITEMS) break;
    if (r != ERROR_SUCCESS) continue;
    std::string nm = w2u(name);
    std::wstring sub = path.empty() ? std::wstring(name) : path + L"\\" + name;
    if (lower(nm).find(term) != std::string::npos)
      out.push_back(std::string(rootname) + "\\" + w2u(sub));
    else if (depth > 0)
      find_in_key(root, rootname, sub, extra, term, depth - 1, out);
  }
  RegCloseKey(k);
}

static void find_run_values(HKEY root, const char* rootname, const wchar_t* path, const std::string& term,
                            std::vector<std::string>& out) {
  HKEY k;
  if (RegOpenKeyExW(root, path, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
  for (DWORD i = 0;; ++i) {
    wchar_t name[512];
    DWORD n = 512;
    LONG r = RegEnumValueW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr);
    if (r == ERROR_NO_MORE_ITEMS) break;
    if (r != ERROR_SUCCESS) continue;
    std::string data = w2u(reg_str(k, name));
    if (lower(w2u(name)).find(term) != std::string::npos || lower(data).find(term) != std::string::npos)
      out.push_back(std::string(rootname) + "\\" + w2u(path) + "  [value] " + w2u(name) + " = " + data);
  }
  RegCloseKey(k);
}

std::vector<std::string> registry_find(const std::string& term) {
  std::vector<std::string> out;
  find_in_key(HKEY_CURRENT_USER, "HKCU", L"Software", 0, term, 1, out);
  find_in_key(HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE", KEY_WOW64_64KEY, term, 1, out);
  find_in_key(HKEY_LOCAL_MACHINE, "HKLM", L"SOFTWARE\\WOW6432Node", 0, term, 1, out);
  const wchar_t* run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
  find_run_values(HKEY_CURRENT_USER, "HKCU", run, term, out);
  find_run_values(HKEY_LOCAL_MACHINE, "HKLM", run, term, out);
  return out;
}

static int run_capture(const std::string& cmd, std::string& out) {
  std::wstring w = L"\"" + u2w(cmd) + L"\"";
  FILE* p = _wpopen(w.c_str(), L"r");
  if (!p) { out = "could not start schtasks"; return -1; }
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  return _pclose(p);
}

// ---- registry ----------------------------------------------------------------------------------
static HKEY hive_root(const std::string& h) { return h == "HKLM" ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER; }
static std::string to_hex(const BYTE* p, DWORD n) {
  static const char* d = "0123456789abcdef";
  std::string o;
  for (DWORD i = 0; i < n; ++i) { o += d[p[i] >> 4]; o += d[p[i] & 15]; }
  return o;
}
static std::vector<BYTE> from_hex(const std::string& s) {
  std::vector<BYTE> v;
  auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0; };
  for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back((BYTE)(nib(s[i]) * 16 + nib(s[i + 1])));
  return v;
}
RegValue reg_get(const std::string& hive, const std::string& key, const std::string& name) {
  RegValue v;
  HKEY k;
  if (RegOpenKeyExW(hive_root(hive), u2w(key).c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return v;
  DWORD type = 0, sz = 0;
  std::wstring wn = u2w(name);
  if (RegQueryValueExW(k, wn.c_str(), nullptr, &type, nullptr, &sz) == ERROR_SUCCESS) {
    std::vector<BYTE> buf(sz + 4, 0);
    DWORD sz2 = sz;
    if (RegQueryValueExW(k, wn.c_str(), nullptr, &type, buf.data(), &sz2) == ERROR_SUCCESS) {
      v.exists = true;
      if (type == REG_DWORD && sz2 >= 4) { v.type = RegType::Dword; v.data = std::to_string(*(DWORD*)buf.data()); }
      else if (type == REG_SZ || type == REG_EXPAND_SZ) { v.type = RegType::Sz; v.data = w2u(std::wstring((wchar_t*)buf.data())); }
      else { v.type = RegType::Binary; v.data = to_hex(buf.data(), sz2); }
    }
  }
  RegCloseKey(k);
  return v;
}
bool reg_set(const std::string& hive, const std::string& key, const std::string& name, RegType t, const std::string& data, std::string& err) {
  HKEY k;
  LONG r = RegCreateKeyExW(hive_root(hive), u2w(key).c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr);
  if (r != ERROR_SUCCESS) { err = r == ERROR_ACCESS_DENIED ? "access denied (administrator rights needed)" : "cannot open key (error " + std::to_string(r) + ")"; return false; }
  std::wstring wn = u2w(name);
  if (t == RegType::Dword) { DWORD d = (DWORD)std::strtoul(data.c_str(), nullptr, 10); r = RegSetValueExW(k, wn.c_str(), 0, REG_DWORD, (const BYTE*)&d, sizeof d); }
  else if (t == RegType::Sz) { std::wstring w = u2w(data); r = RegSetValueExW(k, wn.c_str(), 0, REG_SZ, (const BYTE*)w.c_str(), (DWORD)((w.size() + 1) * sizeof(wchar_t))); }
  else { auto b = from_hex(data); r = RegSetValueExW(k, wn.c_str(), 0, REG_BINARY, b.data(), (DWORD)b.size()); }
  RegCloseKey(k);
  if (r != ERROR_SUCCESS) { err = r == ERROR_ACCESS_DENIED ? "access denied (administrator rights needed)" : "write failed (error " + std::to_string(r) + ")"; return false; }
  return true;
}
bool reg_delete_value(const std::string& hive, const std::string& key, const std::string& name, std::string& err) {
  HKEY k;
  if (RegOpenKeyExW(hive_root(hive), u2w(key).c_str(), 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return true; // nothing to delete
  LONG r = RegDeleteValueW(k, u2w(name).c_str());
  RegCloseKey(k);
  if (r != ERROR_SUCCESS && r != ERROR_FILE_NOT_FOUND) { err = "delete failed (error " + std::to_string(r) + ")"; return false; }
  return true;
}
std::vector<std::string> reg_values(const std::string& hive, const std::string& key) {
  std::vector<std::string> out;
  HKEY k;
  if (RegOpenKeyExW(hive_root(hive), u2w(key).c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return out;
  for (DWORD i = 0;; ++i) {
    wchar_t name[1024];
    DWORD n = 1024;
    LONG r = RegEnumValueW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr);
    if (r == ERROR_NO_MORE_ITEMS) break;
    if (r == ERROR_SUCCESS) out.push_back(w2u(std::wstring(name, n)));
  }
  RegCloseKey(k);
  return out;
}
bool reg_key_exists(const std::string& hive, const std::string& key) {
  HKEY k;
  if (RegOpenKeyExW(hive_root(hive), u2w(key).c_str(), 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
  RegCloseKey(k);
  return true;
}
std::vector<std::string> appx_full_names() {
  std::vector<std::string> out;
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER,
                    L"Software\\Classes\\Local Settings\\Software\\Microsoft\\Windows\\CurrentVersion\\AppModel\\Repository\\Packages",
                    0, KEY_READ, &k) != ERROR_SUCCESS) return out;
  for (DWORD i = 0;; ++i) {
    wchar_t name[512];
    DWORD n = 512;
    LONG r = RegEnumKeyExW(k, i, name, &n, nullptr, nullptr, nullptr, nullptr);
    if (r == ERROR_NO_MORE_ITEMS) break;
    if (r == ERROR_SUCCESS) out.push_back(w2u(name));
  }
  RegCloseKey(k);
  return out;
}
int run_command(const std::string& cmdline, std::string& out) { return run_capture(cmdline, out); }

int schedule_create(const std::string& task, const std::string& exe, int every_hours, std::string& msg) {
  std::string sc = every_hours >= 24 ? "/SC DAILY /MO " + std::to_string(every_hours / 24)
                                      : "/SC HOURLY /MO " + std::to_string(every_hours < 1 ? 1 : every_hours);
  std::string cmd = "schtasks /Create /TN \"" + task + "\" /TR \"\\\"" + exe + "\\\" auto run --quiet\" " + sc + " /F 2>&1";
  return run_capture(cmd, msg);
}
int schedule_delete(const std::string& task, std::string& msg) {
  return run_capture("schtasks /Delete /TN \"" + task + "\" /F 2>&1", msg);
}
int schedule_query(const std::string& task, std::string& msg) {
  return run_capture("schtasks /Query /TN \"" + task + "\" 2>&1", msg);
}

#else
// ===========================================================================
// POSIX shim (used for development/testing; the product target is Windows)
// ===========================================================================
std::optional<std::string> env(const char* name) {
  const char* v = std::getenv(name);
  if (!v) return std::nullopt;
  return std::string(v);
}
void console_init() {}
std::vector<std::string> utf8_args(int argc, char** argv) {
  return std::vector<std::string>(argv, argv + argc);
}
fs::path exe_path() {
  std::error_code ec;
  return fs::read_symlink("/proc/self/exe", ec);
}
size_t private_bytes() {
  std::ifstream f("/proc/self/statm");
  uint64_t size = 0, resident = 0;
  f >> size >> resident;
  return (size_t)(resident * (uint64_t)sysconf(_SC_PAGESIZE));
}
bool apply_mem_cap(size_t bytes, std::string& msg) {
  // Cap the data segment (heap); address-space limits break mmap-heavy libc setups.
  struct rlimit rl;
  rl.rlim_cur = rl.rlim_max = bytes;
  if (setrlimit(RLIMIT_DATA, &rl) != 0) { msg = "setrlimit failed"; return false; }
  return true;
}
bool is_elevated() {
  if (auto e = env("CNA_FAKE_ADMIN")) return *e != "0";
  return geteuid() == 0;
}
bool is_reparse_dir(const fs::path&) { return false; }
bool is_cloud_placeholder(const fs::path&) { return false; }
FileId file_id(const fs::path& p) {
  FileId id;
  struct stat st;
  if (stat(p.c_str(), &st) == 0) { id.a = (uint64_t)st.st_dev; id.b = (uint64_t)st.st_ino; id.ok = true; }
  return id;
}
std::vector<App> installed_apps() { return {}; }
std::vector<std::string> registry_find(const std::string&) { return {}; }
// ---- fake registry (development / tests): one TSV file, hive<TAB>key<TAB>name<TAB>type<TAB>data -------------
namespace {
struct FakeRow { std::string hive, key, name, type, data; };
fs::path fake_reg_path() {
  if (auto e = env("CNA_FAKE_REGISTRY")) return P(*e);
  return state_dir() / "fake-registry.tsv";
}
std::vector<FakeRow> fake_load() {
  std::vector<FakeRow> v;
  std::ifstream f(fake_reg_path());
  std::string line;
  while (std::getline(f, line)) {
    auto c = split(line, '\t');
    if (c.size() < 5) continue;
    v.push_back({c[0], unesc_tsv(c[1]), unesc_tsv(c[2]), c[3], unesc_tsv(c[4])});
  }
  return v;
}
void fake_save(const std::vector<FakeRow>& v) {
  std::error_code ec;
  fs::create_directories(fake_reg_path().parent_path(), ec);
  std::ofstream f(fake_reg_path(), std::ios::trunc);
  for (auto& r : v) f << r.hive << '\t' << esc_tsv(r.key) << '\t' << esc_tsv(r.name) << '\t' << r.type << '\t' << esc_tsv(r.data) << '\n';
}
bool fake_same(const FakeRow& r, const std::string& h, const std::string& k, const std::string& n) {
  return r.hive == h && lower(r.key) == lower(k) && lower(r.name) == lower(n);
}
} // namespace
RegValue reg_get(const std::string& hive, const std::string& key, const std::string& name) {
  RegValue v;
  for (auto& r : fake_load())
    if (fake_same(r, hive, key, name)) {
      v.exists = true;
      v.type = r.type == "dword" ? RegType::Dword : r.type == "binary" ? RegType::Binary : RegType::Sz;
      v.data = r.data;
    }
  return v;
}
bool reg_set(const std::string& hive, const std::string& key, const std::string& name, RegType t, const std::string& data, std::string& err) {
  if (hive == "HKLM" && !is_elevated()) { err = "access denied (administrator rights needed)"; return false; }
  auto rows = fake_load();
  std::string ty = t == RegType::Dword ? "dword" : t == RegType::Binary ? "binary" : "sz";
  for (auto& r : rows) if (fake_same(r, hive, key, name)) { r.type = ty; r.data = data; fake_save(rows); return true; }
  rows.push_back({hive, key, name, ty, data});
  fake_save(rows);
  return true;
}
bool reg_delete_value(const std::string& hive, const std::string& key, const std::string& name, std::string&) {
  auto rows = fake_load();
  std::vector<FakeRow> keep;
  for (auto& r : rows) if (!fake_same(r, hive, key, name)) keep.push_back(r);
  fake_save(keep);
  return true;
}
std::vector<std::string> reg_values(const std::string& hive, const std::string& key) {
  std::vector<std::string> out;
  for (auto& r : fake_load()) if (r.hive == hive && lower(r.key) == lower(key)) out.push_back(r.name);
  return out;
}
bool reg_key_exists(const std::string& hive, const std::string& key) {
  for (auto& r : fake_load()) if (r.hive == hive && lower(r.key) == lower(key)) return true;
  return false;
}
std::vector<std::string> appx_full_names() {
  std::vector<std::string> out;
  if (auto e = env("CNA_FAKE_APPX")) {
    std::ifstream f(P(*e));
    std::string line;
    while (std::getline(f, line)) { line = trim(line); if (!line.empty() && line[0] != '#') out.push_back(line); }
  }
  return out;
}
// Tests: CNA_FAKE_EXEC=<logfile> records the command instead of running anything.
int run_command(const std::string& cmdline, std::string& out) {
  if (auto e = env("CNA_FAKE_EXEC")) {
    std::ofstream(P(*e), std::ios::app) << cmdline << '\n';
    out = "(recorded)";
    return 0;
  }
  out = "running external commands is only implemented for Windows";
  return -1;
}

int schedule_create(const std::string&, const std::string&, int, std::string& msg) {
  msg = "scheduling is only implemented for Windows (Task Scheduler)"; return -1;
}
int schedule_delete(const std::string&, std::string& msg) { msg = "not supported on this OS"; return -1; }
int schedule_query(const std::string&, std::string& msg) { msg = "not supported on this OS"; return -1; }
#endif

} // namespace cna::plat
