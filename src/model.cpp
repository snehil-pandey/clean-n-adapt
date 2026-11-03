#include "model.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

#include "cmds.hpp"
#include "platform.hpp"

namespace cna {

static const uint32_t MODEL_VERSION = 1; // bump when features or seed data change: stored models retrain themselves

const char* cls_name(int c) {
  return c == C_REGEN ? "regenerable" : c == C_CAUTION ? "caution" : c == C_KEEP ? "keep" : "unknown";
}
int cls_from(const std::string& s) {
  std::string l = lower(s);
  if (l == "regenerable" || l == "regen" || l == "clean" || l == "safe") return C_REGEN;
  if (l == "caution" || l == "review") return C_CAUTION;
  if (l == "keep") return C_KEEP;
  return -1;
}

// =================================================================================================
// features
// =================================================================================================
struct Feat { uint32_t idx; float val; std::string key; };

static uint32_t fnv(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) { h ^= c; h *= 16777619u; }
  return h;
}
static void add(std::vector<Feat>& f, const std::string& key, float v = 1.f) {
  f.push_back({fnv(key) & (uint32_t)(Model::D - 1), v, key});
}

// "GPUCache" -> gpu, cache ; "Code Cache" -> code, cache ; "npm-cache" -> npm, cache
static std::vector<std::string> name_tokens(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&] {
    bool alldig = !cur.empty();
    for (char c : cur) if (!std::isdigit((unsigned char)c)) alldig = false;
    if (cur.size() >= 2 && !alldig) out.push_back(lower(cur));
    cur.clear();
  };
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = (unsigned char)s[i];
    if (!std::isalnum(c)) { flush(); continue; }
    if (!cur.empty()) {
      unsigned char p = (unsigned char)cur.back(), n = i + 1 < s.size() ? (unsigned char)s[i + 1] : 0;
      bool boundary = (std::islower(p) && std::isupper(c)) || (std::isupper(p) && std::isupper(c) && std::islower(n)) ||
                      (std::isdigit(p) != std::isdigit(c));
      if (boundary) flush();
    }
    cur += (char)c;
  }
  flush();
  return out;
}

// f_0000a1 -> f_H ; data_12 -> data_0 ; keeps real words intact
static std::string norm_child(const std::string& in) {
  std::string s = lower(in), o;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = i;
    bool dig = false;
    while (j < s.size() && std::isxdigit((unsigned char)s[j])) { if (std::isdigit((unsigned char)s[j])) dig = true; ++j; }
    if (j - i >= 4 && dig) { o += 'H'; i = j; continue; }
    char c = s[i++];
    o += std::isdigit((unsigned char)c) ? '0' : c;
  }
  if (o.size() > 32) o.resize(32);
  return o;
}

static std::string ext_of(const std::string& name) {
  size_t d = name.rfind('.');
  return d == std::string::npos || d == 0 || name.size() - d > 8 ? "" : lower(name.substr(d));
}

static const char* category_of(const std::string& e) {
  static const std::map<std::string, const char*> m = {
      {".jpg", "media"}, {".jpeg", "media"}, {".png", "media"}, {".gif", "media"}, {".webp", "media"}, {".heic", "media"},
      {".bmp", "media"}, {".mp4", "media"}, {".mkv", "media"}, {".avi", "media"}, {".mov", "media"}, {".webm", "media"},
      {".mp3", "media"}, {".flac", "media"}, {".wav", "media"}, {".m4a", "media"},
      {".pdf", "docs"}, {".doc", "docs"}, {".docx", "docs"}, {".xls", "docs"}, {".xlsx", "docs"}, {".ppt", "docs"},
      {".pptx", "docs"}, {".txt", "docs"}, {".md", "docs"}, {".rtf", "docs"},
      {".cpp", "code"}, {".h", "code"}, {".c", "code"}, {".py", "code"}, {".js", "code"}, {".ts", "code"}, {".java", "code"},
      {".cs", "code"}, {".rs", "code"}, {".go", "code"}, {".sln", "code"},
      {".zip", "archive"}, {".7z", "archive"}, {".rar", "archive"}, {".iso", "archive"}, {".gz", "archive"}, {".tar", "archive"},
      {".exe", "program"}, {".dll", "program"}, {".sys", "program"}, {".msi", "program"}, {".mui", "program"},
      {".manifest", "program"}, {".cat", "program"},
      {".sqlite", "db"}, {".db", "db"}, {".ldb", "db"}, {".mdb", "db"}, {".sst", "db"},
      {".log", "logs"}, {".etl", "logs"},
      {".tmp", "temp"}, {".temp", "temp"}, {".bak", "temp"}, {".old", "temp"}, {".dmp", "temp"}, {".mdmp", "temp"}, {".wer", "temp"},
      {".bin", "blob"}, {".dat", "blob"}, {".cache", "blob"}, {".idx", "blob"}, {"", "blob"},
      {".json", "config"}, {".ini", "config"}, {".xml", "config"}, {".cfg", "config"}, {".conf", "config"},
      {".yaml", "config"}, {".yml", "config"}, {".toml", "config"},
      {".pem", "keys"}, {".key", "keys"}, {".pfx", "keys"}, {".pub", "keys"}, {".p12", "keys"}};
  auto it = m.find(e);
  return it == m.end() ? "other" : it->second;
}

static int ilog2(uint64_t v) { int n = 0; while (v > 1) { v >>= 1; ++n; } return n; }

static std::vector<Feat> features(const Descriptor& d) {
  std::vector<Feat> f;
  add(f, "bias");
  auto nt = name_tokens(d.name);
  std::string nn = norm_alnum(d.name);
  if (nn.size() > 40) nn.resize(40);
  add(f, "nn:" + nn);
  if (nt.size() > 8) nt.resize(8);
  for (auto& t : nt) add(f, "n:" + t);
  if (nn.size() >= 4)
    for (size_t i = 0; i + 3 <= nn.size() && i < 24; ++i) add(f, "n3:" + nn.substr(i, 3), 0.5f);
  auto pt = name_tokens(d.parent);
  if (pt.size() > 6) pt.resize(6);
  for (auto& t : pt) add(f, "p:" + t, 0.7f);
  add(f, "r:" + d.root_kind);
  for (size_t i = 0; i < nt.size() && i < 6; ++i) {
    add(f, "n:" + nt[i] + "&r:" + d.root_kind, 0.7f);
    add(f, "n:" + nt[i] + (d.owner_known ? "&own" : "&noown"), 0.7f);
  }
  // children
  std::set<std::string> kids, kid_ext;
  for (auto& c : d.children) { kids.insert(norm_child(c)); std::string e = ext_of(c); if (!e.empty()) kid_ext.insert(e); }
  if (!kids.empty()) {
    float sc = 1.f / std::sqrt((float)kids.size());
    for (auto& k : kids) add(f, "c:" + k, sc * 1.6f);
    for (auto& e : kid_ext) add(f, "ce:" + e, sc * 1.6f);
  }
  // content mix by bytes
  uint64_t tot = 0;
  for (auto& e : d.ext_bytes) tot += e.second;
  if (tot) {
    std::map<std::string, float> cat;
    for (auto& e : d.ext_bytes) {
      float share = (float)((double)e.second / (double)tot);
      add(f, "x:" + e.first, share);
      cat[category_of(e.first)] += share;
    }
    for (auto& c : cat) add(f, "xc:" + c.first, c.second);
  }
  // buckets
  add(f, d.bytes == 0 ? "sz:0" : "sz:" + std::to_string(std::min(24, ilog2(d.bytes / 1024 + 1))), 0.6f);
  add(f, "fc:" + std::to_string(std::min(20, ilog2(d.files + 1))), 0.6f);
  const char* ag = d.newest_days < 0 ? "u" : d.newest_days < 3 ? "0" : d.newest_days < 14 ? "1" : d.newest_days < 60 ? "2" : d.newest_days < 180 ? "3" : d.newest_days < 365 ? "4" : "5";
  add(f, std::string("ag:") + ag, 0.6f);
  if (d.has_exe) add(f, "f:exe", 0.8f);
  if (d.has_uninstaller) add(f, "f:unins", 0.8f);
  add(f, d.owner_known ? "f:owner" : "f:noowner", 0.8f);
  if (d.files == 0) add(f, "f:empty");
  return f;
}

static std::string pretty_feature(const std::string& k) {
  auto after = [&](size_t n) { return k.substr(n); };
  if (k.find('&') != std::string::npos) {
    size_t a = k.find('&');
    std::string left = k.substr(2, a - 2), right = k.substr(a + 1);
    if (right.rfind("r:", 0) == 0) return "name word \"" + left + "\" in " + right.substr(2);
    return "name word \"" + left + "\" and " + (right == "own" ? "an installed app owns it" : "no installed app owns it");
  }
  if (k.rfind("nn:", 0) == 0) return "named \"" + after(3) + "\"";
  if (k.rfind("n3:", 0) == 0) return "name contains \"" + after(3) + "\"";
  if (k.rfind("n:", 0) == 0) return "name word \"" + after(2) + "\"";
  if (k.rfind("p:", 0) == 0) return "inside \"" + after(2) + "\"";
  if (k.rfind("r:", 0) == 0) return "located in " + after(2);
  if (k.rfind("ce:", 0) == 0) return "contains " + after(3) + " files";
  if (k.rfind("c:", 0) == 0) return "contains entries like \"" + after(2) + "\"";
  if (k.rfind("xc:", 0) == 0) return "mostly " + after(3) + " by size";
  if (k.rfind("x:", 0) == 0) return after(2).empty() ? "mostly extension-less files" : "mostly " + after(2) + " files";
  if (k.rfind("sz:", 0) == 0) return "size class " + after(3);
  if (k.rfind("fc:", 0) == 0) return "file-count class " + after(3);
  if (k == "ag:5") return "untouched for over a year";
  if (k == "ag:4") return "untouched for months";
  if (k == "ag:0") return "modified in the last days";
  if (k.rfind("ag:", 0) == 0) return "age class " + after(3);
  if (k == "f:exe") return "contains executables";
  if (k == "f:unins") return "contains an uninstaller";
  if (k == "f:owner") return "an installed app owns it";
  if (k == "f:noowner") return "no installed app owns it";
  if (k == "f:empty") return "empty";
  return k;
}

// =================================================================================================
// model
// =================================================================================================
Prediction Model::predict(const Descriptor& d, bool explain) const {
  Prediction p;
  auto f = features(d);
  float s[C_N] = {0, 0, 0};
  if (w.size() == (size_t)C_N * D)
    for (int c = 0; c < C_N; ++c)
      for (auto& ft : f) s[c] += w[(size_t)c * D + ft.idx] * ft.val;
  float mx = std::max(s[0], std::max(s[1], s[2])), z = 0;
  for (int c = 0; c < C_N; ++c) { p.prob[c] = std::exp(s[c] - mx); z += p.prob[c]; }
  for (int c = 0; c < C_N; ++c) p.prob[c] /= z;
  p.cls = (int)(std::max_element(p.prob, p.prob + C_N) - p.prob);
  if (explain && w.size() == (size_t)C_N * D) {
    int run = -1;
    for (int c = 0; c < C_N; ++c) if (c != p.cls && (run < 0 || p.prob[c] > p.prob[run])) run = c;
    // Prefer reasons a human can read (words, contents, location). Character fragments are a last resort.
    std::vector<std::pair<std::string, float>> contrib;
    for (int pass = 0; pass < 2 && contrib.empty(); ++pass) {
      std::set<std::string> seen;
      for (auto& ft : f) {
        if (ft.key == "bias") continue;
        bool frag = ft.key.rfind("n3:", 0) == 0;
        if (frag != (pass == 1)) continue;
        float c = ft.val * (w[(size_t)p.cls * D + ft.idx] - w[(size_t)run * D + ft.idx]);
        std::string pr = pretty_feature(ft.key);
        if (c > 0.12f && seen.insert(pr).second) contrib.push_back({pr, c});
      }
    }
    std::sort(contrib.begin(), contrib.end(), [](auto& a, auto& b) { return a.second > b.second; });
    if (contrib.size() > 4) contrib.resize(4);
    p.why = contrib;
  }
  return p;
}

struct Ex { std::vector<Feat> f; int y; float wt; int group; bool gate_clean = false; };

static void train(Model& m, const std::vector<Ex>& ex, int epochs = 25) {
  m.w.assign((size_t)C_N * Model::D, 0.f);
  std::vector<size_t> order(ex.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  uint64_t rng = 88172645463325252ull;
  auto next = [&] { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; };
  for (int e = 0; e < epochs; ++e) {
    for (size_t i = order.size(); i > 1; --i) std::swap(order[i - 1], order[next() % i]);
    float lr = 0.25f / (1.f + 0.08f * (float)e);
    for (size_t oi : order) {
      const Ex& x = ex[oi];
      float s[C_N] = {0, 0, 0};
      for (int c = 0; c < C_N; ++c) for (auto& ft : x.f) s[c] += m.w[(size_t)c * Model::D + ft.idx] * ft.val;
      float mx = std::max(s[0], std::max(s[1], s[2])), z = 0, p[C_N];
      for (int c = 0; c < C_N; ++c) { p[c] = std::exp(s[c] - mx); z += p[c]; }
      for (int c = 0; c < C_N; ++c) {
        p[c] /= z;
        float g = (p[c] - (c == x.y ? 1.f : 0.f)) * x.wt;
        g = std::max(-3.f, std::min(3.f, g));
        if (std::fabs(g) < 1e-4f) continue;
        for (auto& ft : x.f) { float& wv = m.w[(size_t)c * Model::D + ft.idx]; wv -= lr * (g * ft.val + 1e-4f * wv); }
      }
    }
  }
}

// =================================================================================================
// built-in seed data: hand-written knowledge about Windows folders, expanded into varied examples
// =================================================================================================
namespace {
struct Rng {
  uint64_t s = 0x9E3779B97F4A7C15ull;
  uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; }
  int range(int n) { return (int)(next() % (uint64_t)n); }
  double uni() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
};
struct SeedName { const char* name; int cls; int kind; const char* parents; const char* roots; };

// kind: 0 cache blobs, 1 logs, 2 temp, 3 crash dumps, 4 app db/profile, 5 system, 6 credentials, 7 project,
//       8 user media/docs, 9 settings, 10 sync root.  roots: L local, R roaming, P programdata, F program files,
//       W windows, U user profile, T temp.
const SeedName SEEDS[] = {
    // ---- regenerable -------------------------------------------------------------------------
    {"Cache", 0, 0, "", "LRP"}, {"cache", 0, 0, "", "LRP"}, {"Code Cache", 0, 0, "", "LR"}, {"GPUCache", 0, 0, "", "LR"},
    {"ShaderCache", 0, 0, "", "LR"}, {"GrShaderCache", 0, 0, "", "L"}, {"DawnCache", 0, 0, "", "L"},
    {"DawnGraphiteCache", 0, 0, "", "L"}, {"DawnWebGPUCache", 0, 0, "", "L"}, {"Cache_Data", 0, 0, "Cache", "L"},
    {"cache2", 0, 0, "", "L"}, {"startupCache", 0, 0, "", "L"}, {"jumpListCache", 0, 0, "", "L"},
    {"IconCache", 0, 0, "", "L"}, {"thumbnails", 0, 0, "", "LR"}, {"INetCache", 0, 0, "", "L"}, {"WebCache", 0, 0, "", "L"},
    {"Temporary Internet Files", 0, 0, "", "L"}, {"Media Cache", 0, 0, "", "LR"}, {"Media Cache Files", 0, 0, "", "LR"},
    {"CacheStorage", 0, 0, "Service Worker", "LR"}, {"ScriptCache", 0, 0, "", "L"}, {"Application Cache", 0, 0, "", "LR"},
    {"AppCache", 0, 0, "", "LR"}, {"blob_storage", 0, 0, "", "LR"}, {"DXCache", 0, 0, "NVIDIA", "L"},
    {"GLCache", 0, 0, "NVIDIA", "L"}, {"D3DSCache", 0, 0, "", "L"}, {"NV_Cache", 0, 0, "NVIDIA Corporation", "P"},
    {"ComponentModelCache", 0, 0, "", "L"}, {"npm-cache", 0, 0, "", "LR"}, {"__pycache__", 0, 0, "", "U"},
    {".cache", 0, 0, "", "U"}, {"httpcache", 0, 0, "appcache", "F"}, {"htmlcache", 0, 0, "Steam", "L"},
    {"ImageCache", 0, 0, "", "LR"}, {"OfflineCache", 0, 0, "", "LR"}, {"GraphicsCache", 0, 0, "", "L"},
    {"Logs", 0, 1, "", "LRP"}, {"logs", 0, 1, "", "LRP"}, {"Log", 0, 1, "", "LRP"}, {"LogFiles", 0, 1, "", "PW"},
    {"CBS", 0, 1, "Logs", "W"},
    {"Crash Reports", 0, 3, "", "LR"}, {"Crashpad", 0, 3, "", "LR"}, {"CrashDumps", 0, 3, "", "L"}, {"crash", 0, 3, "", "LR"},
    {"Minidump", 0, 3, "", "W"}, {"WER", 0, 3, "Windows", "LP"}, {"ReportQueue", 0, 3, "WER", "P"},
    {"ReportArchive", 0, 3, "WER", "P"}, {"Dumps", 0, 3, "", "LR"}, {"CrashRpt", 0, 3, "", "LR"},
    {"Temp", 0, 2, "", "LTW"}, {"tmp", 0, 2, "", "LRT"}, {"Temporary Files", 0, 2, "", "LT"}, {"TempState", 0, 2, "", "L"},
    {"Download", 0, 2, "SoftwareDistribution", "W"}, {"Cache", 0, 0, "DeliveryOptimization", "W"},
    // ---- caution (user data / app state worth keeping) ----------------------------------------
    {"Local Storage", 1, 4, "", "LR"}, {"IndexedDB", 1, 4, "", "LR"}, {"Session Storage", 1, 4, "", "LR"},
    {"Sessions", 1, 4, "", "LR"}, {"Default", 1, 4, "User Data", "L"}, {"Profile 1", 1, 4, "User Data", "L"},
    {"Profile 2", 1, 4, "User Data", "L"}, {"Profiles", 1, 4, "Firefox", "LR"}, {"User Data", 1, 4, "Chrome", "L"},
    {"Extensions", 1, 4, "", "LR"}, {"Extension State", 1, 4, "", "L"}, {"Sync Data", 1, 4, "", "L"},
    {"Storage", 1, 4, "", "LR"}, {"databases", 1, 4, "", "LR"}, {"workspaceStorage", 1, 4, "Code", "R"},
    {"History", 1, 4, "", "LR"}, {"Settings", 1, 9, "", "LRP"}, {"Config", 1, 9, "", "LRP"}, {"config", 1, 9, "", "LRP"},
    {"Preferences", 1, 9, "", "LR"}, {"Backups", 1, 8, "", "LRU"}, {"Backup", 1, 8, "", "LRU"}, {"Saves", 1, 8, "", "LRU"},
    {"SaveGames", 1, 8, "", "U"}, {"Save Games", 1, 8, "", "U"}, {"My Games", 1, 8, "", "U"}, {"Projects", 1, 7, "", "U"},
    {"Workspaces", 1, 7, "", "U"}, {"Templates", 1, 8, "", "R"}, {"Documents", 1, 8, "", "U"}, {"Downloads", 1, 8, "", "U"},
    {"Desktop", 1, 8, "", "U"}, {"Pictures", 1, 8, "", "U"}, {"Music", 1, 8, "", "U"}, {"Videos", 1, 8, "", "U"},
    {"Notes", 1, 8, "", "LRU"}, {"Library", 1, 8, "", "LRU"}, {"Mods", 1, 8, "", "LRU"}, {"Screenshots", 1, 8, "", "LRU"},
    {"Recordings", 1, 8, "", "LRU"}, {"Captures", 1, 8, "", "LRU"}, {"Plugins", 1, 4, "", "LR"},
    {"tdata", 1, 4, "Telegram Desktop", "R"}, {"WhatsApp", 1, 4, "", "LR"},
    // ---- keep (system, credentials, sync roots, version control) --------------------------------
    {"WinSxS", 2, 5, "Windows", "W"}, {"System32", 2, 5, "Windows", "W"}, {"SysWOW64", 2, 5, "Windows", "W"},
    {"Installer", 2, 5, "Windows", "W"}, {"assembly", 2, 5, "Windows", "W"}, {"Microsoft.NET", 2, 5, "", "WF"},
    {"DriverStore", 2, 5, "System32", "W"}, {"servicing", 2, 5, "Windows", "W"}, {"Boot", 2, 5, "", "W"},
    {"Recovery", 2, 5, "", "W"}, {"System Volume Information", 2, 5, "", "W"}, {"Windows", 2, 5, "", "W"},
    {"Package Cache", 2, 5, "", "P"}, {"Common Files", 2, 5, "", "F"}, {"WindowsApps", 2, 5, "", "F"},
    {"Packages", 2, 4, "", "L"}, {"Microsoft", 2, 9, "", "LRP"}, {"Credentials", 2, 6, "Microsoft", "LR"},
    {"Protect", 2, 6, "Microsoft", "R"}, {"Vault", 2, 6, "Microsoft", "LRP"}, {".ssh", 2, 6, "", "U"}, {".aws", 2, 6, "", "U"},
    {".kube", 2, 6, "", "U"}, {".gnupg", 2, 6, "", "U"}, {"gnupg", 2, 6, "", "R"}, {"Certificates", 2, 6, "", "LRP"},
    {"Keys", 2, 6, "", "LRP"}, {"Wallet", 2, 6, "", "LR"}, {"Wallets", 2, 6, "", "LR"}, {"keystore", 2, 6, "", "LR"},
    {".git", 2, 7, "", "U"}, {"OneDrive", 2, 10, "", "U"}, {"Dropbox", 2, 10, "", "U"}, {"Google Drive", 2, 10, "", "U"},
    {"iCloudDrive", 2, 10, "", "U"}, {"Windows Defender", 2, 5, "", "FP"}, {"Windows Security", 2, 5, "", "FP"},
    {"Intel", 2, 5, "", "FPL"}, {"NVIDIA Corporation", 2, 5, "", "FP"}, {"AMD", 2, 5, "", "FPL"}, {"Drivers", 2, 5, "", "WP"},
    {"Fonts", 2, 5, "Windows", "W"},
};
const int N_SEEDS = (int)(sizeof(SEEDS) / sizeof(SEEDS[0]));

const char* PARENTS[] = {"Google", "Chrome", "User Data", "Microsoft", "Edge", "BraveSoftware", "Discord", "Slack", "Spotify",
                         "Code", "Steam", "Epic Games", "Zoom", "Teams", "Adobe", "JetBrains", "NVIDIA", "Mozilla", "Firefox",
                         "Opera", "Telegram", "Dropbox", "Notion", "Figma", "Obsidian", "Postman", "Docker", "Unity", "Blender",
                         "Autodesk", "Skype", "Riot Games", "Battle.net", "Ubisoft", "GOG", "Python", "pip", "Yarn", "Gradle",
                         "Programs", "Packages", "WhatsApp", "Signal", "Evernote", "Zoom", "Lenovo", "Dell", "HP"};
const int N_PARENTS = (int)(sizeof(PARENTS) / sizeof(PARENTS[0]));

const char* BRANDS[] = {"Zorvex", "Lumina", "QuillTools", "Nimbus Notes", "Pixelforge", "Acme Sync", "Brightwave", "Helix Editor",
                        "Orbitly", "CloudNest", "FlowDesk", "Kestrel", "Mosaic", "Northwind Tools", "Optima", "Paperplane",
                        "Quartz", "RedPine", "Solace", "TinyTasks", "Umbra", "Vortex Player", "Wavelet", "Xylo", "Yonder",
                        "Zenith Suite", "ArcLight", "BlueFern", "Cobalt", "DriftNote", "Ember", "Fathom", "Gadgetry", "Harbor",
                        "Inkwell", "Juniper", "Kiln", "Lattice", "Meridian", "Nectar"};
const int N_BRANDS = (int)(sizeof(BRANDS) / sizeof(BRANDS[0]));

struct KindSpec { std::vector<const char*> kids; std::vector<const char*> exts; };
const KindSpec& kind_spec(int k) {
  static const std::vector<KindSpec> v = {
      /*0*/ {{"f_{h6}", "data_{d}", "index", "index-dir", "the-real-index", "{h8}{h8}", "f_{h6}"}, {"", ".bin", ".cache", ".dat"}},
      /*1*/ {{"app-2024-{d2}-{d2}.log", "debug.log", "trace.log", "service.log.{d}", "output-{d}.txt"}, {".log", ".txt", ".etl"}},
      /*2*/ {{"tmp{h4}.tmp", "~DF{H4}.tmp", "{h8}.tmp", "install.log", "{h6}.bak"}, {".tmp", ".bak", ".log"}},
      /*3*/ {{"{h8}-{h4}.dmp", "Report{d}", "report.wer", "{w}.mdmp", "{h8}.dmp"}, {".dmp", ".wer", ".mdmp"}},
      /*4*/ {{"{d6}.log", "LOCK", "CURRENT", "MANIFEST-{d6}", "{d6}.ldb", "Cookies", "History", "Preferences", "leveldb", "Local State", "{w}.sqlite"},
            {".ldb", ".log", ".sqlite", ".json", ""}},
      /*5*/ {{"x64", "en-us", "kernel{d}.dll", "{w}.manifest", "{w}.mui", "{w}.cat", "setup.exe", "{w}.dll", "{w}.sys"},
            {".dll", ".exe", ".manifest", ".mui", ".cat", ".sys"}},
      /*6*/ {{"id_rsa", "id_rsa.pub", "known_hosts", "config", "credentials", "key.pem", "{w}.pfx", "master.key"},
            {"", ".pem", ".pfx", ".key", ".pub"}},
      /*7*/ {{".git", "src", "README.md", "package.json", "build", "main.cpp", "{w}.sln", "tests"}, {".cpp", ".h", ".js", ".json", ".md", ".py"}},
      /*8*/ {{"photo{d}.jpg", "holiday.mp4", "notes.docx", "report.pdf", "IMG_{d4}.png", "song{d}.mp3"}, {".jpg", ".mp4", ".pdf", ".docx", ".mp3", ".png"}},
      /*9*/ {{"settings.json", "config.ini", "prefs.xml", "user.cfg", "options.txt", "profile.yaml"}, {".json", ".ini", ".xml", ".cfg", ".yaml"}},
      /*10*/ {{"Documents", "Pictures", "Desktop", "Work", "notes.txt", ".tmp.drivedownload"}, {".docx", ".jpg", ".pdf", ".txt"}}};
  return v[(size_t)k];
}

std::string expand_tpl(const std::string& t, Rng& r) {
  static const char* words[] = {"core", "main", "shell", "base", "util", "net", "disk", "audio", "video", "kernel", "driver", "update"};
  std::string o;
  char buf[32];
  for (size_t i = 0; i < t.size();) {
    if (t[i] == '{') {
      size_t e = t.find('}', i);
      std::string k = t.substr(i + 1, e - i - 1);
      if (k == "h6") std::snprintf(buf, sizeof buf, "%06x", (unsigned)(r.next() & 0xffffff));
      else if (k == "h8") std::snprintf(buf, sizeof buf, "%08x", (unsigned)r.next());
      else if (k == "h4") std::snprintf(buf, sizeof buf, "%04x", (unsigned)(r.next() & 0xffff));
      else if (k == "H4") std::snprintf(buf, sizeof buf, "%04X", (unsigned)(r.next() & 0xffff));
      else if (k == "d2") std::snprintf(buf, sizeof buf, "%02d", r.range(28) + 1);
      else if (k == "d4") std::snprintf(buf, sizeof buf, "%04d", r.range(9000) + 100);
      else if (k == "d6") std::snprintf(buf, sizeof buf, "%06d", r.range(9000) + 1);
      else if (k == "w") std::snprintf(buf, sizeof buf, "%s", words[r.range(12)]);
      else std::snprintf(buf, sizeof buf, "%d", r.range(40));
      o += buf;
      i = e + 1;
    } else o += t[i++];
  }
  return o;
}

const char* root_name(char c) {
  switch (c) {
    case 'L': return "appdata-local"; case 'R': return "appdata-roaming"; case 'P': return "programdata";
    case 'F': return "program-files"; case 'W': return "windows"; case 'U': return "user-profile"; case 'T': return "temp";
  }
  return "other";
}

// One varied example of a folder with the given class and content pattern.
Descriptor synth(const std::string& name, int cls, int kind, const std::string& parents, const std::string& roots, Rng& r) {
  Descriptor d;
  d.name = name;
  if (!parents.empty()) d.parent = parents;
  else if (r.range(10) < 7) d.parent = PARENTS[r.range(N_PARENTS)];
  d.root_kind = root_name(roots.empty() ? 'L' : roots[(size_t)r.range((int)roots.size())]);
  const KindSpec& ks = kind_spec(kind);
  int nk = 3 + r.range(12);
  for (int i = 0; i < nk; ++i) d.children.push_back(lower(expand_tpl(ks.kids[(size_t)r.range((int)ks.kids.size())], r)));
  double lo = cls == C_REGEN ? 20.7 : cls == C_CAUTION ? 18.7 : 20.7, hi = cls == C_REGEN ? 28.5 : cls == C_CAUTION ? 27.0 : 29.0;
  d.bytes = (uint64_t)std::exp(lo + r.uni() * (hi - lo));
  d.files = std::max<uint64_t>(1, d.bytes >> (10 + r.range(7)));
  d.dirs = (uint64_t)r.range((int)std::min<uint64_t>(60, d.files) + 1);
  int ne = 1 + r.range(3);
  std::set<std::string> chosen;
  while ((int)chosen.size() < ne) chosen.insert(ks.exts[(size_t)r.range((int)ks.exts.size())]);
  double sum = 0;
  std::vector<double> sh;
  for (int i = 0; i < ne; ++i) { sh.push_back(0.2 + r.uni()); sum += sh.back(); }
  int i = 0;
  for (auto& e : chosen) d.ext_bytes.push_back({e, (uint64_t)((double)d.bytes * sh[(size_t)i++] / sum)});
  double u = r.uni();
  d.newest_days = cls == C_REGEN ? (u < 0.7 ? r.uni() * 20 : 20 + r.uni() * 380)
                : cls == C_CAUTION ? (u < 0.25 ? r.uni() * 30 : 30 + r.uni() * 670)
                                   : (u < 0.4 ? r.uni() * 60 : 60 + r.uni() * 1900);
  d.has_exe = kind == 5 ? r.range(10) < 8 : r.range(100) < 4;
  d.has_uninstaller = kind == 5 && r.range(10) < 2;
  d.owner_known = cls == C_KEEP && kind == 5 ? r.range(10) < 2 : r.range(10) < 8;
  return d;
}

Ex to_ex(const Descriptor& d, int y, float wt, int group) { return Ex{features(d), y, wt, group, cleanish_name(d) && !sensitive_name(d)}; }

std::vector<Ex> seed_examples() {
  std::vector<Ex> ex;
  Rng r;
  const int V = 10;
  for (int s = 0; s < N_SEEDS; ++s)
    for (int v = 0; v < V; ++v)
      ex.push_back(to_ex(synth(SEEDS[s].name, SEEDS[s].cls, SEEDS[s].kind, SEEDS[s].parents, SEEDS[s].roots, r), SEEDS[s].cls, 1.f, s));
  // Hard negatives: the name must outweigh the contents. Non-cache names holding cache-like blobs
  // (Credentials, tdata, Protect ...) and cache names holding data-like files.
  for (int s = 0; s < N_SEEDS; ++s) {
    if (SEEDS[s].cls != C_REGEN) {
      const int kinds[4] = {0, 2, 3, 0};
      for (int v = 0; v < 4; ++v)
        ex.push_back(to_ex(synth(SEEDS[s].name, SEEDS[s].cls, kinds[v], SEEDS[s].parents, SEEDS[s].roots, r), SEEDS[s].cls, 1.f, s));
    } else {
      const int kinds[3] = {4, 9, 8};
      for (int v = 0; v < 3; ++v)
        ex.push_back(to_ex(synth(SEEDS[s].name, C_REGEN, kinds[v], SEEDS[s].parents, SEEDS[s].roots, r), C_REGEN, 1.f, s));
    }
  }
  // Folders with invented/unknown names: the model must lean on contents, not on the name.
  for (int b = 0; b < N_BRANDS; ++b)
    for (int v = 0; v < 8; ++v) {
      int pick = r.range(10);
      int cls, kind;
      if (pick < 4) { cls = C_CAUTION; kind = r.range(2) ? 4 : 9; }
      else if (pick < 6) { cls = C_REGEN; kind = r.range(3); }               // blobs / logs / temp only
      else if (pick < 8) { cls = C_CAUTION; kind = 8; }                      // user media/documents
      else { cls = C_KEEP; kind = r.range(2) ? 5 : 6; }                      // binaries / key material
      Descriptor d = synth(BRANDS[b], cls, kind == 1 ? 1 : kind, "", cls == C_KEEP ? "FP" : "LRP", r);
      if (cls == C_REGEN && kind == 1) d.ext_bytes = {{".log", d.bytes}};
      ex.push_back(to_ex(d, cls, 1.f, N_SEEDS + b));
    }
  return ex;
}
} // namespace

// =================================================================================================
// feedback storage + persistence
// =================================================================================================
static fs::path model_path() { return state_dir() / "model.bin"; }
static fs::path feedback_path() { return state_dir() / "feedback.tsv"; }

struct FbRow { std::string key, path; int label = 0; std::time_t when = 0; Descriptor d; };
static void fb_cache_drop();

static std::string desc_exts(const Descriptor& d) {
  std::string s;
  for (auto& e : d.ext_bytes) s += (s.empty() ? "" : ";") + e.first + ":" + std::to_string(e.second);
  return s;
}
static std::string fb_line(const FbRow& r) {
  std::string kids;
  for (auto& c : r.d.children) kids += (kids.empty() ? "" : "\x1f") + c;
  std::ostringstream o;
  o << esc_tsv(r.key) << '\t' << cls_name(r.label) << '\t' << (long long)r.when << '\t' << esc_tsv(r.path) << '\t' << esc_tsv(r.d.name) << '\t'
    << esc_tsv(r.d.parent) << '\t' << r.d.root_kind << '\t' << esc_tsv(kids) << '\t' << esc_tsv(desc_exts(r.d)) << '\t' << r.d.bytes << '\t'
    << r.d.files << '\t' << r.d.dirs << '\t' << r.d.newest_days << '\t' << (r.d.has_exe ? 1 : 0) << (r.d.has_uninstaller ? 1 : 0) << (r.d.owner_known ? 1 : 0);
  return o.str();
}
static bool fb_parse(const std::string& line, FbRow& r) {
  auto c = split(line, '\t');
  if (c.size() < 14) return false;
  r.key = unesc_tsv(c[0]);
  r.label = cls_from(c[1]);
  if (r.label < 0) return false;
  r.when = (std::time_t)std::strtoll(c[2].c_str(), nullptr, 10);
  r.path = unesc_tsv(c[3]);
  r.d.name = unesc_tsv(c[4]);
  r.d.parent = unesc_tsv(c[5]);
  r.d.root_kind = c[6];
  std::string kids = unesc_tsv(c[7]);
  r.d.children.clear();
  if (!kids.empty()) { size_t st = 0; for (;;) { size_t i = kids.find('\x1f', st); r.d.children.push_back(kids.substr(st, i == std::string::npos ? i : i - st)); if (i == std::string::npos) break; st = i + 1; } }
  r.d.ext_bytes.clear();
  for (auto& e : split(unesc_tsv(c[8]), ';')) { auto kv = split(e, ':'); if (kv.size() == 2) r.d.ext_bytes.push_back({kv[0], std::strtoull(kv[1].c_str(), nullptr, 10)}); }
  r.d.bytes = std::strtoull(c[9].c_str(), nullptr, 10);
  r.d.files = std::strtoull(c[10].c_str(), nullptr, 10);
  r.d.dirs = std::strtoull(c[11].c_str(), nullptr, 10);
  r.d.newest_days = std::atof(c[12].c_str());
  if (c[13].size() >= 3) { r.d.has_exe = c[13][0] == '1'; r.d.has_uninstaller = c[13][1] == '1'; r.d.owner_known = c[13][2] == '1'; }
  return true;
}
static std::vector<FbRow> fb_load() {
  std::vector<FbRow> v;
  std::ifstream f(feedback_path());
  std::string line;
  while (std::getline(f, line)) { FbRow r; if (fb_parse(line, r)) v.push_back(std::move(r)); }
  return v;
}
static void fb_save(const std::vector<FbRow>& v) {
  std::error_code ec;
  fs::create_directories(feedback_path().parent_path(), ec);
  std::ofstream f(feedback_path(), std::ios::trunc);
  for (auto& r : v) f << fb_line(r) << '\n';
}

static void train_all(Model& m) {
  auto ex = seed_examples();
  auto fb = fb_load();
  for (auto& r : fb) ex.push_back(to_ex(r.d, r.label, 4.f, -1));
  train(m, ex);
  m.examples = (uint32_t)ex.size();
  m.feedback = (uint32_t)fb.size();
  m.trained = std::time(nullptr);
}

static uint32_t wsum(const std::vector<float>& w) {
  uint32_t h = 2166136261u;
  const unsigned char* p = (const unsigned char*)w.data();
  for (size_t i = 0; i < w.size() * sizeof(float); ++i) { h ^= p[i]; h *= 16777619u; }
  return h;
}
static bool save_model(const Model& m) {
  std::error_code ec;
  fs::create_directories(model_path().parent_path(), ec);
  std::ofstream f(model_path(), std::ios::binary | std::ios::trunc);
  if (!f) return false;
  uint32_t hdr[8] = {0x4D414E43u /*CNAM*/, MODEL_VERSION, (uint32_t)Model::D, (uint32_t)C_N, m.examples, m.feedback, (uint32_t)m.trained, wsum(m.w)};
  f.write((const char*)hdr, sizeof hdr);
  f.write((const char*)m.w.data(), (std::streamsize)(m.w.size() * sizeof(float)));
  return (bool)f;
}
static bool load_model(Model& m) {
  std::ifstream f(model_path(), std::ios::binary);
  if (!f) return false;
  uint32_t hdr[8];
  if (!f.read((char*)hdr, sizeof hdr)) return false;
  if (hdr[0] != 0x4D414E43u || hdr[1] != MODEL_VERSION || hdr[2] != (uint32_t)Model::D || hdr[3] != (uint32_t)C_N) return false;
  std::vector<float> w((size_t)C_N * Model::D);
  if (!f.read((char*)w.data(), (std::streamsize)(w.size() * sizeof(float)))) return false;
  if (wsum(w) != hdr[7]) return false;
  m.w = std::move(w);
  m.examples = hdr[4]; m.feedback = hdr[5]; m.trained = (std::time_t)hdr[6];
  return true;
}

static Model& model_mut() {
  static Model m;
  static bool ready = false;
  if (!ready) {
    ready = true;
    if (!load_model(m)) { train_all(m); save_model(m); }
  }
  return m;
}
const Model& model() { return model_mut(); }

void model_reset() {
  std::error_code ec;
  fs::remove(model_path(), ec);
  fs::remove(feedback_path(), ec);
  fb_cache_drop();
  Model& m = model_mut();
  train_all(m); // seed only
  save_model(m);
}

bool model_feedback(const Descriptor& d, const std::string& path, int label, std::string& err) {
  if (label < 0 || label >= C_N) { err = "bad label"; return false; }
  std::string key = norm_key(P(path));
  auto rows = fb_load();
  std::vector<FbRow> keep;
  for (auto& r : rows) if (r.key != key) keep.push_back(r);
  FbRow row;
  row.key = key; row.path = path; row.label = label; row.when = std::time(nullptr); row.d = d;
  keep.push_back(row);
  if (keep.size() > 5000) keep.erase(keep.begin(), keep.begin() + (long)(keep.size() - 5000));
  fb_save(keep);
  fb_cache_drop();
  Model& m = model_mut();
  train_all(m);
  if (!save_model(m)) { err = "could not save model (check the state folder)"; return false; }
  return true;
}

EvalResult model_eval() {
  EvalResult res;
  auto ex = seed_examples();
  const int FOLDS = 5;
  for (int fold = 0; fold < FOLDS; ++fold) {
    std::vector<Ex> tr, te;
    for (auto& e : ex) (e.group % FOLDS == fold ? te : tr).push_back(e);
    Model m;
    train(m, tr);
    for (auto& e : te) {
      float s[C_N] = {0, 0, 0};
      for (int c = 0; c < C_N; ++c) for (auto& ft : e.f) s[c] += m.w[(size_t)c * Model::D + ft.idx] * ft.val;
      float mx = std::max(s[0], std::max(s[1], s[2])), z = 0, p[C_N];
      for (int c = 0; c < C_N; ++c) { p[c] = std::exp(s[c] - mx); z += p[c]; }
      int pred = 0;
      for (int c = 0; c < C_N; ++c) { p[c] /= z; if (p[c] > p[pred]) pred = c; }
      res.n++;
      res.confusion[e.y][pred]++;
      if (pred == e.y) res.correct++;
      if (e.y != C_REGEN) { res.dangerous_total++; if (pred == C_REGEN && p[C_REGEN] >= 0.85f) { res.dangerous++; if (e.gate_clean) res.dangerous_gated++; } }
    }
  }
  return res;
}


// =================================================================================================
// advice: model proposal + non-learned safety rules + the user's own earlier decisions
// =================================================================================================
bool sensitive_name(const Descriptor& d) {
  if (norm_alnum(d.name) == "packagecache" || norm_alnum(d.name) == "installer") return true; // setup caches needed to repair/uninstall
  static const std::set<std::string> toks = {"credential", "credentials", "password", "passwords", "passwd", "secret", "secrets", "token",
                                             "tokens", "wallet", "wallets", "vault", "keystore", "keychain", "ssh", "gnupg", "gpg", "cert",
                                             "certs", "certificate", "certificates", "protect", "private", "tdata", "seed", "mnemonic",
                                             "keys", "key", "identity", "authenticator", "otp", "2fa", "backup", "backups", "save", "saves",
                                             "savegame", "savegames"};
  static const char* subs[] = {"credential", "password", "wallet", "keystore", "keychain", "secret", "privatekey", "savegame"};
  for (const std::string* nm : {&d.name, &d.parent}) {
    for (auto& t : name_tokens(*nm)) if (toks.count(t)) return true;
    std::string n = norm_alnum(*nm);
    for (auto* sb : subs) if (n.find(sb) != std::string::npos) return true;
  }
  // An app-data root can be named after the app while containing a credential,
  // wallet, key, or save subfolder. Protect the root rather than asking the
  // user to infer that risk from a shallow folder name.
  for (auto& child : d.children) {
    for (auto& t : name_tokens(child)) if (toks.count(t)) return true;
    std::string n = norm_alnum(child);
    for (auto* sb : subs) if (n.find(sb) != std::string::npos) return true;
  }
  return false;
}

bool cleanish_name(const Descriptor& d) {
  static const std::set<std::string> toks = {"temp", "tmp", "temporary", "log", "logs", "trace", "traces", "minidump", "wer",
                                             "reportqueue", "reportarchive", "crashpad"};
  static const char* subs[] = {"cache", "crash", "dump", "shader", "thumbnail", "webcache"};
  for (const std::string* nm : {&d.name, &d.parent}) {
    for (auto& t : name_tokens(*nm)) if (toks.count(t)) return true;
    std::string n = norm_alnum(*nm);
    for (auto* sb : subs) if (n.find(sb) != std::string::npos) return true;
  }
  return false;
}

static std::vector<FbRow>& fb_cache() {
  static std::vector<FbRow> rows;
  static bool loaded = false;
  if (!loaded) { rows = fb_load(); loaded = true; }
  return rows;
}
static void fb_cache_drop() { fb_cache() = fb_load(); }

Advice advise(const Descriptor& d, const std::string& path, const Prediction& pr) {
  Advice a;
  a.cls = pr.cls;
  a.conf = pr.conf();
  // 1. the user's own decision about this exact folder always wins
  std::string key = norm_key(P(path));
  for (auto& r : fb_cache())
    if (r.key == key) {
      a.cls = r.label; a.conf = 1.f;
      a.action = r.label == C_REGEN ? "clean" : r.label == C_KEEP ? "keep" : "review";
      a.note = "your earlier decision";
      return a;
    }
  // 2. never-clean names, whatever the model thinks
  if (sensitive_name(d)) { a.action = "keep"; a.cls = C_KEEP; a.note = "name suggests credentials, keys, wallets or saves"; return a; }
  if (pr.cls == C_KEEP && pr.conf() >= 0.5f) { a.action = "keep"; return a; }
  if (pr.cls == C_REGEN && pr.conf() >= 0.85f) {
    bool learned = false; // a folder with the same name was marked regenerable by the user before
    std::string nn = norm_alnum(d.name);
    for (auto& r : fb_cache()) if (r.label == C_REGEN && norm_alnum(r.d.name) == nn && !nn.empty()) learned = true;
    if (cleanish_name(d) || learned) { a.action = "clean"; if (learned && !cleanish_name(d)) a.note = "same name as a folder you marked regenerable"; return a; }
    a.action = "review";
    a.note = "contents look like a cache, but the folder name is unfamiliar";
    return a;
  }
  a.action = "review";
  if (pr.conf() < 0.6f) a.note = "model is not sure";
  return a;
}

// =================================================================================================
// descriptors from the file system
// =================================================================================================
std::string root_kind_of(const fs::path& p) {
  auto under = [&](const char* var) {
    auto e = plat::env(var);
    return e && !e->empty() && path_under(p, P(*e));
  };
  if (under("TEMP")) return "temp";
  if (under("SystemRoot")) return "windows";
  if (under("PROGRAMFILES") || under("PROGRAMFILES(X86)")) return "program-files";
  if (under("PROGRAMDATA")) return "programdata";
  if (under("LOCALAPPDATA")) return "appdata-local";
  if (under("APPDATA")) return "appdata-roaming";
  if (under("USERPROFILE")) return "user-profile";
  return "other";
}

Descriptor make_descriptor(const fs::path& p, const TreeInfo& ti, const std::set<std::string>& top, bool owner_known) {
  Descriptor d;
  d.name = u8(p.filename());
  d.parent = u8(p.parent_path().filename());
  d.root_kind = root_kind_of(p);
  for (auto& c : top) { d.children.push_back(c); if (d.children.size() >= 40) break; }
  std::vector<std::pair<std::string, uint64_t>> e;
  for (auto& kv : ti.ext) e.push_back({kv.first, kv.second.second});
  std::sort(e.begin(), e.end(), [](auto& a, auto& b) { return a.second > b.second; });
  if (e.size() > 6) e.resize(6);
  d.ext_bytes = e;
  d.bytes = ti.bytes; d.files = ti.files; d.dirs = ti.dirs;
  d.newest_days = ti.any ? age_days(ti.newest) : -1;
  d.has_exe = ti.exes > 0; d.has_uninstaller = ti.uninstaller; d.owner_known = owner_known;
  return d;
}

Descriptor describe_path(const fs::path& p, const OwnerIndex* owners, uint64_t max_entries) {
  std::set<std::string> top;
  std::error_code ec;
  int n = 0;
  if (fs::is_directory(p, ec))
    for (fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && n < 40; it.increment(ec), ++n)
      top.insert(lower(u8(it->path().filename())));
  TreeInfo ti = tree_info(p, max_entries, true);
  bool own = owners && owners->find(u8(p.filename()), p) != nullptr;
  return make_descriptor(p, ti, top, own);
}

// =================================================================================================
// `cna model ...`
// =================================================================================================
static void print_prediction(const Prediction& pr) {
  std::printf("Model:    %s (%.0f%%)   regenerable %.0f%% | caution %.0f%% | keep %.0f%%\n", cls_name(pr.cls), pr.conf() * 100,
              pr.prob[0] * 100, pr.prob[1] * 100, pr.prob[2] * 100);
  if (!pr.why.empty()) {
    std::printf("Because:  ");
    for (size_t i = 0; i < pr.why.size(); ++i) std::printf("%s%s", i ? "; " : "", pr.why[i].first.c_str());
    std::printf("\n");
  }
}

int cmd_model(const Args& a) {
  std::string sub = a.pos.empty() ? "status" : a.pos[0];
  if (sub == "status") {
    const Model& m = model();
    auto fb = fb_load();
    int cnt[C_N] = {0, 0, 0};
    for (auto& r : fb) cnt[r.label]++;
    if (G.json) {
      std::printf("{\"examples\":%u,\"feedback\":%zu,\"feedback_by_class\":{\"regenerable\":%d,\"caution\":%d,\"keep\":%d},\"trained\":\"%s\",\"features\":%d,\"weights_bytes\":%zu,\"file\":\"%s\"}\n",
                  m.examples, fb.size(), cnt[0], cnt[1], cnt[2], fmt_time(m.trained).c_str(), Model::D, m.w.size() * sizeof(float), jesc(u8(model_path())).c_str());
      return 0;
    }
    std::printf("Model file:        %s\n", u8(model_path()).c_str());
    std::printf("Trained on:        %u examples (%d built-in seed groups + %zu of your decisions)\n", m.examples, N_SEEDS, fb.size());
    std::printf("Your decisions:    regenerable %d, caution %d, keep %d\n", cnt[0], cnt[1], cnt[2]);
    std::printf("Last trained:      %s\n", fmt_time(m.trained).c_str());
    std::printf("Size in memory:    %.0f KB (%d hashed features x %d classes)\n", (double)(m.w.size() * sizeof(float)) / 1024.0, Model::D, C_N);
    return 0;
  }
  if (sub == "eval") {
    EvalResult r = model_eval();
    double acc = r.n ? 100.0 * r.correct / r.n : 0;
    if (G.json) {
      std::printf("{\"n\":%d,\"accuracy\":%.2f,\"dangerous\":%d,\"dangerous_after_gate\":%d,\"dangerous_of\":%d,\"confusion\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]]}\n", r.n, acc, r.dangerous, r.dangerous_gated,
                  r.dangerous_total, r.confusion[0][0], r.confusion[0][1], r.confusion[0][2], r.confusion[1][0], r.confusion[1][1], r.confusion[1][2],
                  r.confusion[2][0], r.confusion[2][1], r.confusion[2][2]);
      return 0;
    }
    std::printf("Grouped 5-fold cross-validation on the built-in seed (folder names the model never saw in training)\n");
    std::printf("Accuracy: %.1f%%  (%d / %d)\n\n", acc, r.correct, r.n);
    std::printf("%-14s %12s %9s %6s\n", "true \\ pred", "regenerable", "caution", "keep");
    for (int t = 0; t < C_N; ++t) std::printf("%-14s %12d %9d %6d\n", cls_name(t), r.confusion[t][0], r.confusion[t][1], r.confusion[t][2]);
    std::printf("\nDangerous errors (caution/keep predicted 'regenerable' with >= 85%% confidence): %d of %d (%.1f%%)\n", r.dangerous,
                r.dangerous_total, r.dangerous_total ? 100.0 * r.dangerous / r.dangerous_total : 0.0);
    std::printf("...of which the name-based safety gate would still let through: %d (the gate requires a cache/temp/log/crash-like name)\n", r.dangerous_gated);
    std::printf("Note: the seed is synthetic (generated from hand-written rules), so real-world accuracy will be lower\n"
                "until the model has seen your own decisions.\n");
    return 0;
  }
  if (sub == "retrain") {
    Model& m = model_mut();
    train_all(m);
    save_model(m);
    std::printf("Retrained on %u examples (%u of your decisions).\n", m.examples, m.feedback);
    return 0;
  }
  if (sub == "reset") {
    if (!G.yes) { std::printf("This forgets everything you taught the model (feedback.tsv) and restores the built-in seed. Re-run with --yes.\n"); return 0; }
    model_reset();
    std::printf("Model reset to the built-in seed.\n");
    return 0;
  }
  if (sub == "predict" || sub == "feedback" || sub == "forget") {
    if (a.pos.size() < 2) { std::fprintf(stderr, "usage: cna model %s PATH%s\n", sub.c_str(), sub == "feedback" ? " --label regenerable|caution|keep" : ""); return 64; }
    fs::path p = P(a.pos[1]);
    std::error_code ec;
    if (sub != "forget" && !fs::exists(p, ec)) { std::fprintf(stderr, "no such path: %s\n", a.pos[1].c_str()); return 1; }
    p = fs::absolute(p, ec);
    if (sub == "forget") {
      std::string key = norm_key(p);
      auto rows = fb_load();
      std::vector<FbRow> keep;
      for (auto& r : rows) if (r.key != key) keep.push_back(r);
      fb_save(keep);
      fb_cache_drop();
      Model& m = model_mut();
      train_all(m);
      save_model(m);
      std::printf("Removed %zu decision(s) for this path and retrained.\n", rows.size() - keep.size());
      return 0;
    }
    auto apps = load_apps(a.get("apps-file"));
    OwnerIndex idx;
    idx.build(apps);
    Descriptor d = describe_path(p, &idx);
    Prediction before = model().predict(d, true);
    Advice adv = advise(d, u8(p), before);
    if (sub == "predict") {
      if (G.json) {
        std::printf("{\"path\":\"%s\",\"class\":\"%s\",\"confidence\":%.3f,\"action\":\"%s\",\"note\":\"%s\",\"because\":[", jesc(u8(p)).c_str(), cls_name(before.cls), before.conf(),
                    adv.action.c_str(), jesc(adv.note).c_str());
        for (size_t i = 0; i < before.why.size(); ++i) std::printf("%s\"%s\"", i ? "," : "", jesc(before.why[i].first).c_str());
        std::printf("]}\n");
      } else {
        std::printf("Path:     %s\n", u8(p).c_str());
        print_prediction(before);
        std::printf("Action:   %s%s%s\n", adv.action.c_str(), adv.note.empty() ? "" : "  - ", adv.note.c_str());
      }
      return 0;
    }
    int label = cls_from(a.get("label"));
    if (label < 0) { std::fprintf(stderr, "--label must be regenerable, caution or keep\n"); return 64; }
    std::string err;
    if (!model_feedback(d, u8(p), label, err)) { std::fprintf(stderr, "%s\n", err.c_str()); return 1; }
    Prediction after = model().predict(d, false);
    std::printf("Learned: \"%s\" is %s.\n  before: %s (%.0f%%)   after: %s (%.0f%%)\n", u8(p.filename()).c_str(), cls_name(label), cls_name(before.cls),
                before.conf() * 100, cls_name(after.cls), after.conf() * 100);
    return 0;
  }
  std::fprintf(stderr, "usage: cna model [status|eval|retrain|reset|predict PATH|feedback PATH --label X|forget PATH]\n");
  return 64;
}

} // namespace cna
