#include <fstream>

#include "cmds.hpp"
#include "model.hpp"

namespace cna {

// ---- built-in knowledge: well-known folder names ---------------------------
struct Sig { const char* leaf; const char* kind; const char* safety; const char* summary; };
static const Sig SIGS[] = {
    {"node_modules", "dependencies", "regenerable", "npm/yarn package dependencies; recreated by `npm install`."},
    {".git", "git-repo", "keep", "Git repository database with the full project history. Deleting it loses history."},
    {"__pycache__", "cache", "regenerable", "Python bytecode cache; regenerated automatically."},
    {".venv", "environment", "regenerable", "Python virtual environment; recreate from requirements."},
    {"venv", "environment", "regenerable", "Python virtual environment; recreate from requirements."},
    {"cache", "cache", "regenerable", "Application cache; rebuilt on demand."},
    {"cache_data", "cache", "regenerable", "Browser/app cache payload; rebuilt on demand."},
    {"code cache", "cache", "regenerable", "Compiled JavaScript cache of a Chromium/Electron app."},
    {"gpucache", "cache", "regenerable", "GPU shader cache of a Chromium/Electron app."},
    {"shadercache", "cache", "regenerable", "Compiled shader cache; rebuilt on next launch."},
    {"grshadercache", "cache", "regenerable", "Chromium graphics shader cache."},
    {"dxcache", "cache", "regenerable", "DirectX shader cache; recompiled on next game launch."},
    {"glcache", "cache", "regenerable", "OpenGL shader cache; recompiled on next launch."},
    {"d3dscache", "cache", "regenerable", "Direct3D shader cache."},
    {"inetcache", "cache", "regenerable", "Internet/WebView temporary cache."},
    {"cache2", "cache", "regenerable", "Firefox disk cache."},
    {"cachestorage", "cache", "regenerable", "Service-worker Cache Storage of a browser/app."},
    {"crashdumps", "crash-reports", "regenerable", "Crash dump files; only useful for debugging."},
    {"crashpad", "crash-reports", "regenerable", "Crash reporter data of a Chromium/Electron app."},
    {"minidump", "crash-reports", "regenerable", "Windows crash minidumps."},
    {"wer", "crash-reports", "regenerable", "Windows Error Reporting queue."},
    {"temp", "temp", "regenerable", "Temporary files; safe to clear (skip files in use)."},
    {"tmp", "temp", "regenerable", "Temporary files; safe to clear."},
    {"logs", "logs", "regenerable", "Log files; safe to delete unless you are debugging."},
    {"local storage", "app-data", "caution", "Per-site/app local storage (login state, settings) of a browser/Electron app."},
    {"indexeddb", "app-data", "caution", "Browser/app database; may hold offline data and sessions."},
    {"session storage", "app-data", "caution", "Browser/app session data."},
    {"user data", "browser-profile", "caution", "Browser profile root: bookmarks, passwords, extensions, history."},
    {"winsxs", "system", "keep", "Windows component store. Never delete manually; use DISM."},
    {"installer", "system", "keep", "Windows Installer cache; needed to repair/uninstall programs."},
    {"package cache", "system", "keep", "Setup cache of Visual C++ and other installers; needed to repair or uninstall programs."},
    {"assembly", "system", "keep", "Windows/.NET assembly cache."},
    {"system32", "system", "keep", "Core Windows system files."},
    {"syswow64", "system", "keep", "32-bit Windows system files."},
    {"driverstore", "system", "keep", "Installed driver packages."},
    {"servicing", "system", "keep", "Windows servicing stack."},
    {"softwaredistribution", "system", "caution", "Windows Update working folder; only the Download subfolder is safe to clear."},
    {"$recycle.bin", "system", "caution", "Recycle Bin storage. Empty it from Explorer."},
    {"system volume information", "system", "keep", "Restore points and volume metadata."},
    {"windowsapps", "system", "keep", "Microsoft Store app installs."},
    {"packages", "app-data", "caution", "Store (UWP) app data; managed by Windows."},
    {"steamapps", "game-library", "caution", "Steam game installs; deleting removes installed games."},
    {".gradle", "tool-cache", "regenerable", "Gradle caches and wrapper downloads; re-downloaded on next build."},
    {".m2", "tool-cache", "regenerable", "Maven local repository; re-downloaded on next build."},
    {".cargo", "tool-cache", "caution", "Rust toolchain home (registry cache is regenerable; installed binaries are not)."},
    {".rustup", "toolchain", "caution", "Installed Rust toolchains."},
    {".nuget", "tool-cache", "regenerable", ".NET package cache."},
    {".npm", "tool-cache", "regenerable", "npm download cache."},
    {".ssh", "credentials", "keep", "SSH keys and known hosts. Do not delete."},
    {".aws", "credentials", "keep", "AWS credentials/config. Do not delete."},
    {".kube", "credentials", "keep", "Kubernetes cluster credentials."},
    {".docker", "tool-config", "caution", "Docker client config and credentials."},
    {".vscode", "tool-config", "caution", "VS Code settings/extensions for the user or project."},
    {"appdata", "user-profile", "keep", "Per-user application data root. Never delete as a whole."},
    {"onedrive", "sync-folder", "keep", "OneDrive synced files. Deleting here deletes them in the cloud."},
};

struct Facts {
  std::string kind = "unknown", safety = "unknown", summary, source = "rules";
  std::string owner;
  uint64_t bytes = 0, files = 0;
  double newest_days = -1;
  bool truncated = false;
  std::string contents;
};

static std::string category_of(const std::string& ext) {
  static const std::map<std::string, std::string> m = {
      {".jpg", "images"}, {".jpeg", "images"}, {".png", "images"}, {".gif", "images"}, {".webp", "images"}, {".heic", "images"},
      {".mp4", "video"}, {".mkv", "video"}, {".avi", "video"}, {".mov", "video"}, {".webm", "video"},
      {".mp3", "audio"}, {".flac", "audio"}, {".wav", "audio"}, {".m4a", "audio"}, {".ogg", "audio"},
      {".pdf", "documents"}, {".docx", "documents"}, {".doc", "documents"}, {".xlsx", "documents"}, {".pptx", "documents"}, {".txt", "documents"}, {".md", "documents"},
      {".cpp", "source code"}, {".h", "source code"}, {".py", "source code"}, {".js", "source code"}, {".ts", "source code"}, {".java", "source code"}, {".cs", "source code"}, {".rs", "source code"}, {".go", "source code"},
      {".zip", "archives"}, {".7z", "archives"}, {".rar", "archives"}, {".iso", "disk images"}, {".vhdx", "virtual disks"},
      {".exe", "programs"}, {".dll", "programs"}, {".msi", "installers"},
      {".sqlite", "databases"}, {".db", "databases"}, {".ldb", "databases"}, {".log", "logs"}, {".tmp", "temp files"}};
  auto it = m.find(ext);
  return it == m.end() ? "" : it->second;
}

// ---- knowledge base (TSV, streamed; never fully loaded) -----------------------
static fs::path kb_path() { return state_dir() / "kb.tsv"; }

struct KbRow { std::string key, kind, safety, summary, source, path; std::time_t when = 0; };

static bool parse_kb(const std::string& line, KbRow& r) {
  auto c = split(line, '\t');
  if (c.size() < 7) return false;
  r.key = unesc_tsv(c[0]); r.kind = c[1]; r.safety = c[2]; r.summary = unesc_tsv(c[3]); r.source = c[4];
  r.when = (std::time_t)std::strtoll(c[5].c_str(), nullptr, 10); r.path = unesc_tsv(c[6]);
  return true;
}

static std::optional<KbRow> kb_get(const std::string& key) {
  std::ifstream f(kb_path());
  std::string line;
  std::optional<KbRow> found;
  while (std::getline(f, line)) {
    KbRow r;
    if (parse_kb(line, r) && r.key == key) found = r; // last wins
  }
  return found;
}

static void kb_put(const KbRow& row) {
  const size_t kMaxRows = 50000;
  fs::path tmp = kb_path();
  tmp += ".new";
  {
    std::ifstream in(kb_path());
    std::ofstream out(tmp, std::ios::trunc);
    std::string line;
    std::vector<std::string> keep; // bounded by kMaxRows
    while (std::getline(in, line)) {
      KbRow r;
      if (parse_kb(line, r) && r.key != row.key) keep.push_back(line);
      if (keep.size() > kMaxRows) keep.erase(keep.begin());
    }
    for (auto& l : keep) out << l << '\n';
    out << esc_tsv(row.key) << '\t' << row.kind << '\t' << row.safety << '\t' << esc_tsv(row.summary) << '\t' << row.source << '\t'
        << (long long)row.when << '\t' << esc_tsv(row.path) << '\n';
  }
  std::error_code ec;
  fs::rename(tmp, kb_path(), ec);
  if (ec) { fs::remove(kb_path(), ec); fs::rename(tmp, kb_path(), ec); }
}

static int kb_forget(const std::string& key) {
  fs::path tmp = kb_path();
  tmp += ".new";
  int removed = 0;
  {
    std::ifstream in(kb_path());
    std::ofstream out(tmp, std::ios::trunc);
    std::string line;
    while (std::getline(in, line)) {
      KbRow r;
      if (parse_kb(line, r) && r.key == key) { ++removed; continue; }
      out << line << '\n';
    }
  }
  std::error_code ec;
  fs::remove(kb_path(), ec);
  fs::rename(tmp, kb_path(), ec);
  return removed;
}

int cmd_kb(const Args& a) {
  std::string sub = a.pos.empty() ? "list" : a.pos[0];
  if (sub == "forget") {
    if (a.pos.size() < 2) { std::fprintf(stderr, "usage: cna kb forget <path>\n"); return 64; }
    std::printf("Removed %d entr%s.\n", kb_forget(norm_key(P(a.pos[1]))), "y/ies");
    return 0;
  }
  if (sub != "list" && sub != "search") { std::fprintf(stderr, "usage: cna kb [list|search <words>|forget <path>]\n"); return 64; }
  std::vector<std::string> words;
  for (size_t i = 1; i < a.pos.size(); ++i) words.push_back(lower(a.pos[i]));
  std::ifstream f(kb_path());
  std::string line;
  int n = 0;
  bool first = true;
  if (G.json) std::printf("{\"entries\":[");
  while (std::getline(f, line)) {
    KbRow r;
    if (!parse_kb(line, r)) continue;
    std::string hay = lower(r.path + " " + r.kind + " " + r.summary + " " + r.safety);
    bool ok = true;
    for (auto& w : words) if (hay.find(w) == std::string::npos) { ok = false; break; }
    if (!ok) continue;
    ++n;
    if (G.json) {
      std::printf("%s{\"path\":\"%s\",\"kind\":\"%s\",\"safety\":\"%s\",\"summary\":\"%s\",\"source\":\"%s\"}", first ? "" : ",",
                  jesc(r.path).c_str(), r.kind.c_str(), r.safety.c_str(), jesc(r.summary).c_str(), r.source.c_str());
      first = false;
    } else {
      std::printf("%s\n    [%s/%s, %s] %s\n", r.path.c_str(), r.kind.c_str(), r.safety.c_str(), r.source.c_str(), r.summary.c_str());
    }
  }
  if (G.json) std::printf("],\"count\":%d}\n", n);
  else std::printf("%d entr%s.\n", n, n == 1 ? "y" : "ies");
  return 0;
}

// ---- classification ------------------------------------------------------------
// Deterministic rules only: well-known folder names, marker files, build-output heuristic.
static void apply_rules(const fs::path& p, const std::string& leaf, const TreeInfo& ti, const std::set<std::string>& top, Facts& f) {
  std::error_code ec;
  // 1) well-known folder names
  for (auto& s : SIGS)
    if (leaf == s.leaf) { f.kind = s.kind; f.safety = s.safety; f.summary = s.summary; break; }

  // 2) marker files inside the folder
  auto has = [&](const char* n) { return top.count(n) > 0; };
  if (f.kind == "unknown" || f.kind == "app-data") {
    if (has("local state") && (has("default") || has("cache"))) { f.kind = "browser-profile"; f.safety = "caution"; f.summary = "Chromium-based browser profile root (bookmarks, passwords, extensions)."; }
    else if (has("prefs.js")) { f.kind = "browser-profile"; f.safety = "caution"; f.summary = "Firefox profile (bookmarks, logins, extensions)."; }
    else if (has(".git")) { f.kind = "git-working-copy"; f.safety = "keep"; f.summary = "Source project under Git version control."; }
    else if (has("package.json")) { f.kind = "node-project"; f.safety = "keep"; f.summary = "Node.js project (package.json). node_modules inside can be regenerated."; }
    else if (has("cargo.toml")) { f.kind = "rust-project"; f.safety = "keep"; f.summary = "Rust project (Cargo.toml). The target/ folder inside is regenerable."; }
    else if (has("cmakelists.txt")) { f.kind = "cpp-project"; f.safety = "keep"; f.summary = "C/C++ project built with CMake; build directories inside are regenerable."; }
    else if (has("pyproject.toml") || has("setup.py") || has("requirements.txt")) { f.kind = "python-project"; f.safety = "keep"; f.summary = "Python project."; }
    else if (has("pom.xml") || has("build.gradle") || has("build.gradle.kts")) { f.kind = "java-project"; f.safety = "keep"; f.summary = "Java/Kotlin project (Maven or Gradle)."; }
    else if (ti.uninstaller) { f.kind = "installed-program"; f.safety = "caution"; f.summary = "Install folder of a program (contains an uninstaller). Remove it with that uninstaller."; }
  }
  if (f.kind == "unknown" && (leaf == "dist" || leaf == "build" || leaf == "target" || leaf == "out" || leaf == "obj" || leaf == "bin")) {
    fs::path parent = p.parent_path();
    for (const char* m : {"package.json", "Cargo.toml", "CMakeLists.txt", "pom.xml", "pyproject.toml"})
      if (fs::exists(parent / m, ec)) { f.kind = "build-output"; f.safety = "regenerable"; f.summary = "Build output of the project in the parent folder; rebuilt by the build tool."; break; }
  }

}

RuleVerdict rule_verdict(const fs::path& p, const TreeInfo& ti, const std::set<std::string>& top) {
  Facts f;
  apply_rules(p, lower(u8(p.filename())), ti, top, f);
  return {f.kind, f.safety, f.summary};
}

static Facts analyze(const fs::path& p, const OwnerIndex& idx) {
  Facts f;
  std::error_code ec;
  std::string leaf = lower(u8(p.filename()));
  std::set<std::string> top;
  bool is_dir = fs::is_directory(p, ec);
  TreeInfo ti;
  if (is_dir) {
    int n = 0;
    for (fs::directory_iterator it(p, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end && n < 5000; it.increment(ec), ++n)
      top.insert(lower(u8(it->path().filename())));
    ti = tree_info(p, 100000, true);
  } else if (fs::is_regular_file(p, ec)) {
    ti.bytes = fs::file_size(p, ec);
    ti.files = 1;
    ti.newest = fs::last_write_time(p, ec);
    ti.any = true;
    auto dot = leaf.rfind('.');
    if (dot != std::string::npos) ti.ext[leaf.substr(dot)] = {1, ti.bytes};
  }
  f.bytes = ti.bytes; f.files = ti.files; f.truncated = ti.truncated;
  if (ti.any) f.newest_days = age_days(ti.newest);

  apply_rules(p, leaf, ti, top, f);

  // 3) content mix
  if (!ti.ext.empty()) {
    std::map<std::string, uint64_t> cat;
    for (auto& e : ti.ext) { auto c = category_of(e.first); if (!c.empty()) cat[c] += e.second.second; }
    std::string best;
    uint64_t bb = 0;
    for (auto& c : cat) if (c.second > bb) { bb = c.second; best = c.first; }
    if (!best.empty() && ti.bytes) {
      int pct = (int)(100.0 * (double)bb / (double)ti.bytes);
      if (pct >= 40) f.contents = "mostly " + best + " (" + std::to_string(pct) + "% by size)";
    }
    if (f.kind == "unknown" && !f.contents.empty()) {
      f.summary = "Unrecognised folder; " + f.contents + ".";
      f.safety = "unknown";
    }
  }

  // 4) who owns it?
  if (const plat::App* a = idx.find(u8(p.filename()), p)) f.owner = a->name.empty() ? a->alias : a->name;
  return f;
}

int cmd_explain(const Args& a) {
  if (a.pos.empty()) { std::fprintf(stderr, "usage: cna explain <path> [--refresh] [--llm] [--no-store]\n"); return 64; }
  fs::path p = P(a.pos[0]);
  std::error_code ec;
  if (!fs::exists(p, ec)) { std::fprintf(stderr, "no such path: %s\n", a.pos[0].c_str()); return 1; }
  p = fs::absolute(p, ec);

  auto apps = load_apps(a.get("apps-file"));
  OwnerIndex idx;
  idx.build(apps);
  Facts f = analyze(p, idx);
  std::string key = norm_key(p);

  bool from_kb = false;
  std::time_t kb_when = 0;
  if (!a.has("refresh")) {
    if (auto row = kb_get(key)) {
      // A cached answer wins only when the built-in rules would not know better.
      if (f.kind == "unknown") {
        f.kind = row->kind; f.safety = row->safety; f.summary = row->summary; f.source = "knowledge base (" + row->source + ")";
        from_kb = true; kb_when = row->when;
      }
    }
  }
  if (f.kind == "unknown" && a.has("llm")) {
    if (!llm::available()) std::fprintf(stderr, "note: this build has no local LLM backend (see README, \"Optional local LLM\").\n");
    else {
      std::string facts = "Folder: " + u8(p) + "\nSize: " + human(f.bytes) + " in " + commas(f.files) + " files\nContents: " + f.contents;
      if (auto d = llm::describe(facts)) { f.summary = *d; f.source = "local LLM"; f.kind = "described"; }
    }
  }
  // the learned model: always shown for folders; decides the verdict only where rules and knowledge base are silent
  bool have_model = fs::is_directory(p, ec);
  Prediction mp;
  Advice madv;
  if (have_model) {
    Descriptor md = describe_path(p, &idx);
    mp = model().predict(md, true);
    madv = advise(md, u8(p), mp);
    if (f.kind == "unknown" && madv.conf >= 0.6f) {
      f.safety = madv.action == "clean" ? "regenerable" : madv.action == "keep" ? "keep" : "caution";
      f.summary = std::string("No known signature; the on-device model estimates this folder is ") + cls_name(madv.cls) + ".";
      f.source = "model";
    }
  }
  if (f.kind == "unknown" && f.summary.empty()) f.summary = "No known signature. Review the contents before deleting.";

  if (!a.has("no-store") && !from_kb && f.kind != "unknown") {
    kb_put({key, f.kind, f.safety, f.summary, f.source == "rules" ? "rules" : "llm", u8(p), std::time(nullptr)});
  }

  if (G.json) {
    std::printf("{\"path\":\"%s\",\"kind\":\"%s\",\"safety\":\"%s\",\"summary\":\"%s\",\"owner\":\"%s\",\"bytes\":%llu,\"files\":%llu,\"source\":\"%s\",\"model_class\":\"%s\",\"model_confidence\":%.3f,\"action\":\"%s\"}\n",
                jesc(u8(p)).c_str(), f.kind.c_str(), f.safety.c_str(), jesc(f.summary).c_str(), jesc(f.owner).c_str(),
                (unsigned long long)f.bytes, (unsigned long long)f.files, jesc(f.source).c_str(),
                have_model ? cls_name(mp.cls) : "", have_model ? mp.conf() : 0.f, have_model ? madv.action.c_str() : "");
    return 0;
  }
  std::printf("Path:     %s\n", u8(p).c_str());
  std::printf("What:     %s\n", f.summary.c_str());
  std::printf("Kind:     %s\n", f.kind.c_str());
  const char* verdict = f.safety == "regenerable" ? "safe to delete (it is rebuilt automatically)"
                        : f.safety == "keep" ? "keep - do not delete"
                        : f.safety == "caution" ? "caution - contains data you may want"
                                                : "unknown - inspect first";
  std::printf("Safety:   %s\n", verdict);
  if (have_model) {
    std::printf("Model:    %s (%.0f%%)", cls_name(mp.cls), mp.conf() * 100);
    for (size_t i = 0; i < mp.why.size() && i < 3; ++i) std::printf("%s%s", i ? "; " : "  because: ", mp.why[i].first.c_str());
    std::printf("\nAdvice:   %s%s%s\n", madv.action.c_str(), madv.note.empty() ? "" : " - ", madv.note.c_str());
  }
  if (!f.owner.empty()) std::printf("Owner:    installed app \"%s\"\n", f.owner.c_str());
  else if (!apps.empty()) std::printf("Owner:    no installed app matches this name\n");
  std::printf("Size:     %s in %s file(s)%s", human(f.bytes).c_str(), commas(f.files).c_str(), f.truncated ? "+ (stopped counting)" : "");
  if (f.newest_days >= 0) std::printf(", last change %d day(s) ago", (int)f.newest_days);
  std::printf("\n");
  if (!f.contents.empty()) std::printf("Contents: %s\n", f.contents.c_str());
  std::printf("Source:   %s", f.source.c_str());
  if (from_kb) std::printf(", saved %s", fmt_time(kb_when).c_str());
  std::printf("\n");
  return 0;
}

} // namespace cna
