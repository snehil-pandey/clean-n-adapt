#include <fstream>
#include <regex>

#include "cmds.hpp"

// `cna debloat`: removes Windows "bloat" in four reversible-by-design ways plus one irreversible one.
//   privacy  - per-user / policy registry values (ads, suggestions, telemetry level ...)         [undoable]
//   startup  - disables Run-key and Startup-folder entries the same way Task Manager does       [undoable]
//   services - sets a short allow-list of non-essential services to Disabled                      [undoable, admin]
//   disk     - hibernation file                                                                   [undoable, admin]
//   apps     - removes preinstalled Store apps from an allow-list                                 [NOT undoable, explicit opt-in]
// Everything is catalogue-driven: nothing that is not in a list below can be touched by a preset. Every change
// records the previous value in a journal, so `cna debloat undo` restores exactly what was there before.
namespace cna {

namespace {

using plat::RegType;

struct Rc { std::string hive, key, name; RegType type; std::string data; };

enum Kind { K_REG, K_STARTUP, K_SERVICE, K_APP, K_POWER };

struct DItem {
  std::string id, category, title, effect, cost, detail, status; // status: pending | applied
  int tier = 0;                // 1 safe, 2 balanced, 3 aggressive, 0 = unrecognised (manual only), -1 = protected
  bool admin = false, reversible = true;
  Kind kind = K_REG;
  std::vector<Rc> regs;
  std::vector<std::string> pkgs;
  uint64_t bytes = 0;
  std::string cmd, undo_cmd;
};

const char* tier_name(int t) { return t == 1 ? "safe" : t == 2 ? "balanced" : t == 3 ? "aggressive" : t == 0 ? "manual" : "protected"; }

std::string esc_ps(const std::string& s) { return s; } // names are validated against a strict pattern before use

// ---- catalogues -----------------------------------------------------------------------------------------------
const char* CDM = "Software\\Microsoft\\Windows\\CurrentVersion\\ContentDeliveryManager";
const char* EXA = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced";

Rc dw(const char* hive, const std::string& key, const char* name, const char* v) { return {hive, key, name, RegType::Dword, v}; }

std::vector<DItem> tweak_items() {
  std::vector<DItem> v;
  auto add = [&](const char* id, const char* title, const char* effect, const char* cost, int tier, bool admin, std::vector<Rc> regs) {
    DItem d;
    d.id = std::string("tw:") + id; d.category = "privacy"; d.title = title; d.effect = effect; d.cost = cost;
    d.tier = tier; d.admin = admin; d.kind = K_REG; d.regs = std::move(regs);
    v.push_back(std::move(d));
  };
  add("ads-id", "Advertising ID", "Apps can no longer use a per-user advertising ID.", "Ads become less relevant.", 1, false,
      {dw("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\AdvertisingInfo", "Enabled", "0")});
  add("tailored", "Tailored experiences", "Stops using diagnostic data to personalise tips and ads.", "Less personalised suggestions.", 1, false,
      {dw("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\Privacy", "TailoredExperiencesWithDiagnosticDataEnabled", "0")});
  add("start-ads", "Suggestions and promoted apps in Start", "Removes tips, suggested apps and promotions from Start and Settings.", "None for most people.", 1, false,
      {dw("HKCU", CDM, "SubscribedContent-338388Enabled", "0"), dw("HKCU", CDM, "SubscribedContent-338389Enabled", "0"),
       dw("HKCU", CDM, "SubscribedContent-353694Enabled", "0"), dw("HKCU", CDM, "SubscribedContent-353696Enabled", "0"),
       dw("HKCU", CDM, "SystemPaneSuggestionsEnabled", "0")});
  add("silent-installs", "Silent promoted-app installs", "Stops Windows quietly installing promoted apps in the background.", "None.", 1, false,
      {dw("HKCU", CDM, "SilentInstalledAppsEnabled", "0"), dw("HKCU", CDM, "PreInstalledAppsEnabled", "0"),
       dw("HKCU", CDM, "OemPreInstalledAppsEnabled", "0"), dw("HKCU", CDM, "PreInstalledAppsEverEnabled", "0"),
       dw("HKCU", CDM, "ContentDeliveryAllowed", "0"), dw("HKCU", CDM, "SoftLandingEnabled", "0"), dw("HKCU", CDM, "FeatureManagementEnabled", "0")});
  add("lockscreen-tips", "Lock screen tips", "Removes fun facts, tips and ads from the lock screen.", "None.", 1, false,
      {dw("HKCU", CDM, "RotatingLockScreenOverlayEnabled", "0"), dw("HKCU", CDM, "SubscribedContent-338387Enabled", "0"),
       dw("HKCU", CDM, "SubscribedContent-338393Enabled", "0")});
  add("explorer-ads", "Ads in File Explorer", "Hides sync-provider promotions in File Explorer.", "None.", 1, false,
      {dw("HKCU", EXA, "ShowSyncProviderNotifications", "0")});
  add("feedback", "Feedback prompts", "Stops Windows asking you for feedback.", "None.", 1, false,
      {dw("HKCU", "Software\\Microsoft\\Siuf\\Rules", "NumberOfSIUFInPeriod", "0")});
  add("search-highlights", "Search highlights", "Removes the daily doodles/ads from the search box.", "None.", 1, false,
      {dw("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\SearchSettings", "IsDynamicSearchBoxEnabled", "0")});
  add("consumer-features", "Consumer features (all users)", "Stops Windows installing consumer apps and suggestions for every account.", "Only applies to Pro/Enterprise/Education editions.", 1, true,
      {dw("HKLM", "SOFTWARE\\Policies\\Microsoft\\Windows\\CloudContent", "DisableWindowsConsumerFeatures", "1")});
  add("start-recs", "Start 'Recommended' section", "Stops Start suggesting recent files and apps (Windows 11).", "Start shows fewer shortcuts.", 2, false,
      {dw("HKCU", EXA, "Start_IrisRecommendations", "0")});
  add("bing-search", "Web results in Start search", "Search finds only your PC, not Bing results.", "No web results when searching from Start.", 2, false,
      {dw("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\Search", "BingSearchEnabled", "0"),
       dw("HKCU", "Software\\Policies\\Microsoft\\Windows\\Explorer", "DisableSearchBoxSuggestions", "1")});
  add("track-progs", "Track launched apps", "Stops Windows tracking which apps you launch for Start.", "Start's 'most used' list stops updating.", 2, false,
      {dw("HKCU", EXA, "Start_TrackProgs", "0")});
  add("taskbar-widgets", "Taskbar Widgets button", "Hides the Widgets button/news feed (Windows 11).", "No widgets panel. May be ignored on the newest builds.", 2, false,
      {dw("HKCU", EXA, "TaskbarDa", "0")});
  add("taskbar-chat", "Taskbar Chat button", "Hides the Chat/Teams button (Windows 11).", "None. May be ignored on the newest builds.", 2, false,
      {dw("HKCU", EXA, "TaskbarMn", "0")});
  add("copilot", "Windows Copilot", "Turns off the Copilot side panel by policy.", "No Copilot. Policy is ignored on some newer builds.", 2, false,
      {dw("HKCU", "Software\\Policies\\Microsoft\\Windows\\WindowsCopilot", "TurnOffWindowsCopilot", "1")});
  add("telemetry-min", "Diagnostic data level", "Asks Windows to send only the minimum required diagnostic data.", "Home edition treats this as 'Required'. A few Insider features need more.", 2, true,
      {dw("HKLM", "SOFTWARE\\Policies\\Microsoft\\Windows\\DataCollection", "AllowTelemetry", "0")});
  add("activity-history", "Activity history", "Stops collecting and uploading your activity history (Timeline).", "No cross-device activity history.", 2, true,
      {dw("HKLM", "SOFTWARE\\Policies\\Microsoft\\Windows\\System", "EnableActivityFeed", "0"),
       dw("HKLM", "SOFTWARE\\Policies\\Microsoft\\Windows\\System", "PublishUserActivities", "0"),
       dw("HKLM", "SOFTWARE\\Policies\\Microsoft\\Windows\\System", "UploadUserActivities", "0")});
  add("game-dvr", "Game DVR / background recording", "Turns off Game Bar background capture.", "No Game Bar clips or recording.", 3, false,
      {dw("HKCU", "System\\GameConfigStore", "GameDVR_Enabled", "0"), dw("HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\GameDVR", "AppCaptureEnabled", "0")});
  return v;
}

struct Svc { const char* name; const char* title; int tier; const char* cost; };
const Svc SERVICES[] = {
    {"RetailDemo", "Retail Demo Service", 1, "None (only used on store demo PCs)."},
    {"Fax", "Fax", 1, "None unless you use a fax modem."},
    {"DiagTrack", "Connected User Experiences and Telemetry", 2, "Less diagnostic data is sent to Microsoft."},
    {"dmwappushservice", "WAP Push Message Routing", 2, "None for most PCs."},
    {"MapsBroker", "Downloaded Maps Manager", 2, "Offline maps in the Maps app stop updating."},
    {"WerSvc", "Windows Error Reporting", 3, "Crash reports are no longer sent or collected."},
    {"SysMain", "SysMain (Superfetch)", 3, "May slow app launches on hard disks (negligible on SSDs)."},
    {"WSearch", "Windows Search indexing", 3, "File and Outlook search become slower."},
    {"XblAuthManager", "Xbox Live Auth Manager", 3, "Xbox app, Game Pass and some Store games can no longer sign in."},
    {"XblGameSave", "Xbox Live Game Save", 3, "Cloud saves for Xbox/Store games stop syncing."},
    {"XboxNetApiSvc", "Xbox Live Networking Service", 3, "Xbox multiplayer features stop working."},
    {"XboxGipSvc", "Xbox Accessory Management", 3, "Xbox controllers lose accessory management."},
};

struct App { const char* pattern; const char* title; int tier; const char* cost; };
const App APPS[] = {
    {"Microsoft.BingNews", "Microsoft News", 1, "No News app."},
    {"Microsoft.GetHelp", "Get Help", 1, "None."},
    {"Microsoft.Getstarted", "Tips", 1, "None."},
    {"Microsoft.MicrosoftSolitaireCollection", "Solitaire Collection", 1, "The card games are gone."},
    {"Microsoft.MixedReality.Portal", "Mixed Reality Portal", 1, "None unless you own a VR headset."},
    {"Microsoft.Microsoft3DViewer", "3D Viewer", 1, "No 3D model viewer."},
    {"Microsoft.Print3D", "Print 3D", 1, "None."},
    {"Microsoft.OneConnect", "Mobile Plans", 1, "None."},
    {"Microsoft.People", "People", 1, "None."},
    {"Microsoft.SkypeApp", "Skype (Store version)", 1, "No Store Skype."},
    {"Microsoft.WindowsFeedbackHub", "Feedback Hub", 1, "Cannot send Windows feedback."},
    {"Microsoft.MicrosoftOfficeHub", "Microsoft 365 hub", 1, "Only a launcher/ad for Office."},
    {"Microsoft.549981C3F5F10", "Cortana", 1, "None."},
    {"Disney.37853FC22B2CE", "Disney+", 1, "Promoted app."},
    {"BytedanceInc.TikTok", "TikTok", 1, "Promoted app."},
    {"king.com.CandyCrush*", "Candy Crush", 1, "Promoted games."},
    {"Duolingo-LearnLanguagesforFree", "Duolingo", 1, "Promoted app."},
    {"AdobeSystemsIncorporated.AdobeExpress", "Adobe Express", 1, "Promoted app."},
    {"Facebook.Facebook", "Facebook", 1, "Promoted app."},
    {"Microsoft.BingWeather", "Weather", 2, "No Weather app (the taskbar weather needs it)."},
    {"Microsoft.WindowsMaps", "Maps", 2, "No Maps app."},
    {"Microsoft.ZuneVideo", "Movies & TV", 2, "No Movies & TV app."},
    {"Microsoft.Todos", "Microsoft To Do", 2, "No To Do app."},
    {"Microsoft.Whiteboard", "Whiteboard", 2, "No Whiteboard app."},
    {"Clipchamp.Clipchamp", "Clipchamp", 2, "No Clipchamp video editor."},
    {"MicrosoftTeams", "Microsoft Teams (personal)", 2, "No personal Teams/Chat."},
    {"MSTeams", "Microsoft Teams (personal, new)", 2, "No personal Teams/Chat."},
    {"Microsoft.YourPhone", "Phone Link", 2, "No Phone Link (phone notifications/photos on PC)."},
    {"Microsoft.PowerAutomateDesktop", "Power Automate", 2, "No desktop automation flows."},
    {"Microsoft.Windows.DevHome", "Dev Home", 2, "No Dev Home dashboard."},
    {"Microsoft.OutlookForWindows", "Outlook (new)", 2, "No new Outlook app."},
    {"SpotifyAB.SpotifyMusic", "Spotify (Store)", 2, "Reinstall from the Store if you use it."},
    {"4DF9E0F8.Netflix", "Netflix", 2, "Reinstall from the Store if you use it."},
    {"AmazonVideo.PrimeVideo", "Prime Video", 2, "Reinstall from the Store if you use it."},
    {"Microsoft.ZuneMusic", "Media Player", 3, "The default music/video player is gone."},
    {"Microsoft.XboxApp", "Xbox (old)", 3, "No Xbox companion app."},
    {"Microsoft.GamingApp", "Xbox", 3, "No Xbox app / Game Pass PC games."},
    {"Microsoft.XboxGamingOverlay", "Xbox Game Bar", 3, "No Game Bar (Win+G)."},
    {"Microsoft.Xbox.TCUI", "Xbox sign-in UI", 3, "Some Store games can no longer sign in."},
    {"Microsoft.XboxGameOverlay", "Xbox Game overlay", 3, "No in-game overlay."},
    {"Microsoft.XboxIdentityProvider", "Xbox Identity Provider", 3, "Xbox/Store game sign-in breaks."},
    {"Microsoft.XboxSpeechToTextOverlay", "Xbox speech overlay", 3, "No in-game speech overlay."},
};

// substrings (lowercase) that mark a startup entry as security software or a hardware driver: never offered
const char* STARTUP_PROTECTED[] = {"securityhealth", "defender", "msascui", "realtek", "rthdvcpl", "rtkaud", "synaptics", "elantech", "touchpad",
                                   "bitdefender", "kaspersky", "norton", "mcafee", "avast", "avg", "malwarebytes", "eset", "intel", "igfx",
                                   "amd", "radeon", "vmware", "wacom", "logi", "nvidia", "nvcontainer", "ctfmon", "sunloginclient"};
struct StartupHint { const char* needle; int tier; };
const StartupHint STARTUP_HINTS[] = {
    {"adobeaamupdater", 1}, {"adobegcinvoker", 1}, {"ituneshelper", 1}, {"jusched", 1}, {"sunjavaupdatesched", 1}, {"javaupdate", 1},
    {"microsoftedgeautolaunch", 1}, {"cortana", 1}, {"quicktime", 1}, {"realplay", 1}, {"skype", 2}, {"spotify", 2}, {"discord", 2},
    {"steam", 2}, {"epicgameslauncher", 2}, {"msteams", 2}, {"teams", 2}, {"zoom", 2}, {"slack", 2}, {"ccleaner", 2}, {"acrotray", 2},
    {"creative cloud", 2}, {"ccxprocess", 2}, {"webex", 2}, {"eadesktop", 2}, {"origin", 2}, {"battle.net", 2}, {"ubisoft", 2},
    {"riotclient", 2}, {"onedrive", 3}, {"dropbox", 3}, {"googledrive", 3}, {"yourphone", 3}, {"phonelink", 3}};

int startup_tier(const std::string& lname_cmd) {
  for (auto* p : STARTUP_PROTECTED) if (lname_cmd.find(p) != std::string::npos) return -1;
  for (auto& h : STARTUP_HINTS) if (lname_cmd.find(h.needle) != std::string::npos) return h.tier;
  return 0;
}

// ---- registry helpers -----------------------------------------------------------------------------------------
bool same_value(const Rc& r) {
  auto v = plat::reg_get(r.hive, r.key, r.name);
  if (!v.exists) return false;
  if (r.type == RegType::Dword) return v.type == RegType::Dword && std::strtoul(v.data.c_str(), nullptr, 10) == std::strtoul(r.data.c_str(), nullptr, 10);
  return v.data == r.data;
}

const char* APPROVED = "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\";
const char* DISABLED_BLOB = "030000000000000000000000";

bool approval_disabled(const std::string& sub, const std::string& name) {
  auto v = plat::reg_get("HKCU", std::string(APPROVED) + sub, name);
  if (!v.exists || v.type != RegType::Binary || v.data.size() < 2) return false;
  int first = std::stoi(v.data.substr(0, 2), nullptr, 16);
  return (first & 1) != 0; // Task Manager: 02/06 = enabled, 03/07 = disabled
}

std::vector<DItem> startup_items() {
  struct Src { const char* hive; const char* key; const char* sub; const char* label; };
  const Src runs[] = {{"HKCU", "Software\\Microsoft\\Windows\\CurrentVersion\\Run", "Run", "HKCU Run"},
                      {"HKLM", "Software\\Microsoft\\Windows\\CurrentVersion\\Run", "Run", "HKLM Run"},
                      {"HKLM", "Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Run", "Run32", "HKLM Run (32-bit)"}};
  std::vector<DItem> v;
  std::set<std::string> seen;
  auto push = [&](const std::string& name, const std::string& cmd, const std::string& sub, const std::string& label) {
    if (!seen.insert(lower(sub + "|" + name)).second) return;
    DItem d;
    d.id = "startup:" + name; d.category = "startup"; d.kind = K_STARTUP;
    d.title = name + "  (" + label + ")";
    d.detail = cmd;
    d.tier = startup_tier(lower(name + " " + cmd));
    d.effect = "Stops it launching at sign-in. You can still start it by hand.";
    d.cost = d.tier == 0 ? "Not recognised: presets leave it alone." : d.tier < 0 ? "Security software or a hardware driver: not offered." : "The app starts a bit later (when you open it).";
    d.regs.push_back({"HKCU", std::string(APPROVED) + sub, name, RegType::Binary, DISABLED_BLOB});
    d.status = approval_disabled(sub, name) ? "applied" : "pending";
    v.push_back(std::move(d));
  };
  for (auto& s : runs)
    for (auto& name : plat::reg_values(s.hive, s.key)) push(name, plat::reg_get(s.hive, s.key, name).data, s.sub, s.label);
  for (const char* var : {"APPDATA", "PROGRAMDATA"}) {
    auto e = plat::env(var);
    if (!e || e->empty()) continue;
    fs::path dir = P(*e) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / (std::string(var) == "APPDATA" ? "Startup" : "StartUp");
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
      std::string n = u8(it->path().filename());
      if (lower(n) == "desktop.ini" || !it->is_regular_file(ec)) continue;
      push(n, u8(it->path()), "StartupFolder", std::string(var) == "APPDATA" ? "Startup folder" : "Startup folder (all users)");
    }
  }
  return v;
}

std::vector<DItem> service_items() {
  std::vector<DItem> v;
  for (auto& s : SERVICES) {
    std::string key = std::string("SYSTEM\\CurrentControlSet\\Services\\") + s.name;
    if (!plat::reg_key_exists("HKLM", key)) continue; // not present on this edition
    DItem d;
    d.id = std::string("svc:") + s.name; d.category = "services"; d.kind = K_SERVICE; d.admin = true;
    d.title = std::string(s.title) + "  (" + s.name + ")";
    d.effect = "Sets the service to Disabled (takes effect after a restart).";
    d.cost = s.cost; d.tier = s.tier;
    d.regs.push_back({"HKLM", key, "Start", RegType::Dword, "4"});
    d.status = same_value(d.regs[0]) ? "applied" : "pending";
    v.push_back(std::move(d));
  }
  return v;
}

bool valid_pkg(const std::string& s) { static const std::regex re("^[A-Za-z0-9][A-Za-z0-9._\\-]*$"); return std::regex_match(s, re); }

std::vector<DItem> app_items(const std::vector<std::string>& fulls) {
  std::vector<DItem> v;
  for (auto& a : APPS) {
    DItem d;
    for (auto& f : fulls) {
      std::string nm = f.substr(0, f.find('_'));
      if (wild_match(a.pattern, nm.c_str()) && valid_pkg(f)) d.pkgs.push_back(f);
    }
    if (d.pkgs.empty()) continue;
    std::string prefix = a.pattern;
    if (!prefix.empty() && prefix.back() == '*') prefix.pop_back();
    d.id = "app:" + prefix; d.category = "apps"; d.kind = K_APP; d.reversible = false;
    d.title = a.title;
    d.effect = "Removes the app for your account.";
    d.cost = std::string(a.cost) + " Cannot be undone here: reinstall from the Microsoft Store.";
    d.tier = a.tier; d.status = "pending";
    for (auto& f : d.pkgs) d.detail += (d.detail.empty() ? "" : ", ") + f;
    v.push_back(std::move(d));
  }
  return v;
}

std::vector<DItem> power_items() {
  std::vector<DItem> v;
  auto h = plat::reg_get("HKLM", "SYSTEM\\CurrentControlSet\\Control\\Power", "HibernateEnabled");
  if (!h.exists) return v;
  DItem d;
  d.id = "power:hibernate"; d.category = "disk"; d.kind = K_POWER; d.admin = true; d.tier = 3;
  d.title = "Hibernation file (hiberfil.sys)";
  d.effect = "Turns hibernation off and deletes hiberfil.sys (about 40% of your RAM in size).";
  d.cost = "No hibernate and no Fast Startup. Undo with: cna debloat undo.";
  d.cmd = "powercfg.exe /hibernate off"; d.undo_cmd = "powercfg.exe /hibernate on";
  d.status = h.data == "0" ? "applied" : "pending";
  if (auto sd = plat::env("SystemDrive")) {
    std::error_code ec;
    fs::path hf = P(*sd + "/hiberfil.sys");
    if (fs::exists(hf, ec)) d.bytes = fs::file_size(hf, ec);
  }
  v.push_back(std::move(d));
  return v;
}

std::vector<std::string> package_list(const Args& a) {
  if (!a.has("packages-file")) return plat::appx_full_names();
  std::vector<std::string> v;
  std::ifstream f(P(a.get("packages-file")));
  std::string line;
  while (std::getline(f, line)) { line = trim(line); if (!line.empty() && line[0] != '#') v.push_back(line); }
  return v;
}

std::vector<DItem> build_items(const Args& a) {
  std::vector<DItem> all = tweak_items();
  for (auto& d : all) {
    bool applied = true;
    for (auto& r : d.regs) if (!same_value(r)) applied = false;
    d.status = applied ? "applied" : "pending";
  }
  for (auto* f : {&startup_items, &service_items, &power_items}) for (auto& d : (*f)()) all.push_back(std::move(d));
  for (auto& d : app_items(package_list(a))) all.push_back(std::move(d));
  return all;
}

// ---- journal --------------------------------------------------------------------------------------------------
struct JRow { std::string id, kind, label, hive, key, name, prev_type, prev_data, undo_cmd; bool prev_exists = false; std::time_t when = 0; };
fs::path jpath() { return state_dir() / "debloat-journal.tsv"; }

std::vector<JRow> jload() {
  std::vector<JRow> v;
  std::ifstream f(jpath());
  std::string line;
  while (std::getline(f, line)) {
    auto c = split(line, '\t');
    if (c.size() < 11) continue;
    JRow r;
    r.id = c[0]; r.when = (std::time_t)std::strtoll(c[1].c_str(), nullptr, 10); r.kind = c[2]; r.label = unesc_tsv(c[3]);
    r.hive = c[4]; r.key = unesc_tsv(c[5]); r.name = unesc_tsv(c[6]); r.prev_exists = c[7] == "1"; r.prev_type = c[8];
    r.prev_data = unesc_tsv(c[9]); r.undo_cmd = unesc_tsv(c[10]);
    v.push_back(std::move(r));
  }
  return v;
}
void jsave(const std::vector<JRow>& v) {
  std::error_code ec;
  fs::create_directories(jpath().parent_path(), ec);
  std::ofstream f(jpath(), std::ios::trunc);
  for (auto& r : v)
    f << r.id << '\t' << (long long)r.when << '\t' << r.kind << '\t' << esc_tsv(r.label) << '\t' << r.hive << '\t' << esc_tsv(r.key) << '\t' << esc_tsv(r.name)
      << '\t' << (r.prev_exists ? 1 : 0) << '\t' << r.prev_type << '\t' << esc_tsv(r.prev_data) << '\t' << esc_tsv(r.undo_cmd) << '\n';
}
std::string type_str(RegType t) { return t == RegType::Dword ? "dword" : t == RegType::Binary ? "binary" : "sz"; }
RegType type_from(const std::string& s) { return s == "dword" ? RegType::Dword : s == "binary" ? RegType::Binary : RegType::Sz; }

int g_seq = 0;
std::string new_id() { return "D" + std::to_string((long long)std::time(nullptr)) + "-" + std::to_string(++g_seq); }

// ---- apply ----------------------------------------------------------------------------------------------------
struct Outcome { std::string id, status, reason; }; // status: applied | skipped | failed

Outcome apply_item(const DItem& d) {
  Outcome o{d.id, "applied", ""};
  std::vector<JRow> journal = jload();
  auto add_row = [&](JRow r) { journal.push_back(std::move(r)); jsave(journal); };
  if (d.kind == K_APP) {
    int ok = 0;
    for (auto& pkg : d.pkgs) {
      if (!valid_pkg(pkg)) { o.status = "failed"; o.reason = "unsafe package name refused"; return o; }
      std::string out;
      std::string cmd = "powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Remove-AppxPackage -Package '" + esc_ps(pkg) + "' -ErrorAction Stop\"";
      if (plat::run_command(cmd, out) == 0) ++ok;
      else { o.reason = "powershell failed: " + trim(out).substr(0, 160); }
    }
    JRow r;
    r.id = new_id(); r.when = std::time(nullptr); r.kind = "app"; r.label = d.id; r.prev_type = "none";
    if (ok) add_row(r);
    if (ok == 0) o.status = "failed";
    else if (ok < (int)d.pkgs.size()) { o.status = "failed"; }
    else o.reason.clear();
    return o;
  }
  if (d.kind == K_POWER) {
    std::string out;
    if (plat::run_command(d.cmd, out) != 0) { o.status = "failed"; o.reason = "command failed: " + trim(out).substr(0, 160); return o; }
    JRow r;
    r.id = new_id(); r.when = std::time(nullptr); r.kind = "power"; r.label = d.id; r.prev_type = "none"; r.undo_cmd = d.undo_cmd;
    add_row(r);
    return o;
  }
  for (auto& rc : d.regs) {
    auto prev = plat::reg_get(rc.hive, rc.key, rc.name);
    std::string err;
    if (!plat::reg_set(rc.hive, rc.key, rc.name, rc.type, rc.data, err)) { o.status = "failed"; o.reason = err; return o; }
    JRow r;
    r.id = new_id(); r.when = std::time(nullptr);
    r.kind = d.kind == K_STARTUP ? "startup" : d.kind == K_SERVICE ? "service" : "reg";
    r.label = d.id; r.hive = rc.hive; r.key = rc.key; r.name = rc.name;
    r.prev_exists = prev.exists; r.prev_type = type_str(prev.type); r.prev_data = prev.data;
    add_row(r);
  }
  return o;
}

// ---- printing -------------------------------------------------------------------------------------------------
void print_item_json(const DItem& d, bool first) {
  std::printf("%s{\"id\":\"%s\",\"category\":\"%s\",\"title\":\"%s\",\"tier\":%d,\"tier_name\":\"%s\",\"admin\":%s,\"reversible\":%s,\"status\":\"%s\",\"effect\":\"%s\",\"cost\":\"%s\",\"detail\":\"%s\",\"bytes\":%llu}",
              first ? "" : ",", jesc(d.id).c_str(), d.category.c_str(), jesc(d.title).c_str(), d.tier, tier_name(d.tier), d.admin ? "true" : "false",
              d.reversible ? "true" : "false", d.status.c_str(), jesc(d.effect).c_str(), jesc(d.cost).c_str(), jesc(d.detail).c_str(), (unsigned long long)d.bytes);
}

int preset_tier(const std::string& p) { return p == "aggressive" ? 3 : p == "balanced" ? 2 : 1; }

std::set<std::string> cats_from(const Args& a) {
  std::set<std::string> c;
  if (a.has("only")) { for (auto& s : split(a.get("only"), ',')) c.insert(lower(trim(s))); }
  else c = {"privacy", "startup", "services", "disk"};     // apps are never part of a default run: irreversible
  if (a.has("apps")) c.insert("apps");
  return c;
}

} // namespace

// =================================================================================================================
int cmd_debloat(const Args& a) {
  std::string sub = a.pos.empty() ? "scan" : a.pos[0];
  bool elevated = plat::is_elevated();

  if (sub == "scan" || sub == "list") {
    auto items = build_items(a);
    int pend[4] = {0, 0, 0, 0}, applied = 0, need_admin = 0;
    for (auto& d : items) {
      if (d.status == "applied") ++applied;
      else if (d.tier >= 1) { pend[d.tier]++; if (d.admin && !elevated) ++need_admin; }
    }
    if (G.json) {
      std::printf("{\"elevated\":%s,\"items\":[", elevated ? "true" : "false");
      for (size_t i = 0; i < items.size(); ++i) print_item_json(items[i], i == 0);
      std::printf("],\"summary\":{\"pending_safe\":%d,\"pending_balanced\":%d,\"pending_aggressive\":%d,\"applied\":%d,\"need_admin\":%d}}\n", pend[1], pend[2], pend[3], applied, need_admin);
      return 0;
    }
    std::printf("Debloat scan   (administrator: %s)\n", elevated ? "yes" : "no");
    const char* order[] = {"privacy", "startup", "services", "disk", "apps"};
    const char* names[] = {"PRIVACY AND ADS (undoable)", "STARTUP ITEMS (undoable)", "SERVICES (undoable, needs administrator)", "DISK (undoable, needs administrator)",
                           "STORE APPS (NOT undoable here: reinstall from the Microsoft Store)"};
    for (int c = 0; c < 5; ++c) {
      bool any = false;
      for (auto& d : items) {
        if (d.category != order[c]) continue;
        if (!any) { std::printf("\n%s\n", names[c]); any = true; }
        std::printf("  [%-10s] %-9s %-26s %s\n", tier_name(d.tier), d.status.c_str(), d.id.c_str(), d.title.c_str());
        if (G.verbose || d.category == "apps") std::printf("                              cost: %s\n", d.cost.c_str());
        if (G.verbose && !d.detail.empty()) std::printf("                              %s\n", d.detail.c_str());
      }
    }
    std::printf("\n%d pending (safe %d, balanced %d, aggressive %d), %d already applied, %d need administrator.\n", pend[1] + pend[2] + pend[3], pend[1], pend[2], pend[3], applied, need_admin);
    std::printf("Apply:  cna debloat apply --preset safe --yes      (privacy, startup, services, disk; add --apps to also remove Store apps)\n");
    std::printf("Undo:   cna debloat undo --all --yes\n");
    return 0;
  }

  if (sub == "apply") {
    auto items = build_items(a);
    int maxtier = preset_tier(lower(a.get("preset", "safe")));
    auto cats = cats_from(a);
    auto ids = a.all("id");
    std::vector<const DItem*> chosen;
    std::vector<Outcome> outcomes;
    for (auto& d : items) {
      bool pick;
      if (!ids.empty()) pick = std::find(ids.begin(), ids.end(), d.id) != ids.end();
      else pick = d.tier >= 1 && d.tier <= maxtier && cats.count(d.category);
      if (!pick || d.status == "applied") continue;
      if (d.tier < 0) { outcomes.push_back({d.id, "skipped", "protected (security software or driver)"}); continue; }
      chosen.push_back(&d);
    }
    for (auto& id : ids) {
      bool found = false;
      for (auto& d : items) if (d.id == id) found = true;
      if (!found) outcomes.push_back({id, "skipped", "unknown id (see: cna debloat scan)"});
    }
    bool real = G.yes;
    for (auto* d : chosen) {
      if (d->admin && !elevated) { outcomes.push_back({d->id, "skipped", "needs administrator"}); continue; }
      if (!real) { outcomes.push_back({d->id, "would apply", ""}); continue; }
      outcomes.push_back(apply_item(*d));
    }
    int ok = 0, bad = 0;
    for (auto& o : outcomes) { if (o.status == "applied") ++ok; else if (o.status == "failed") ++bad; }
    if (G.json) {
      std::printf("{\"dry_run\":%s,\"applied\":%d,\"failed\":%d,\"results\":[", real ? "false" : "true", ok, bad);
      for (size_t i = 0; i < outcomes.size(); ++i)
        std::printf("%s{\"id\":\"%s\",\"status\":\"%s\",\"reason\":\"%s\"}", i ? "," : "", jesc(outcomes[i].id).c_str(), outcomes[i].status.c_str(), jesc(outcomes[i].reason).c_str());
      std::printf("]}\n");
      return bad ? 2 : 0;
    }
    for (auto& o : outcomes) std::printf("  %-12s %-28s %s\n", o.status.c_str(), o.id.c_str(), o.reason.c_str());
    if (!real) std::printf("\nPreview only. Re-run with --yes to apply.\n");
    else std::printf("\n%d applied, %d failed. Undo with: cna debloat undo --all --yes%s\n", ok, bad, a.has("apps") || cats.count("apps") ? "  (Store apps cannot be undone)" : "");
    return bad ? 2 : 0;
  }

  if (sub == "journal") {
    auto rows = jload();
    if (G.json) {
      std::printf("{\"entries\":[");
      for (size_t i = 0; i < rows.size(); ++i)
        std::printf("%s{\"id\":\"%s\",\"kind\":\"%s\",\"label\":\"%s\",\"when\":\"%s\"}", i ? "," : "", rows[i].id.c_str(), rows[i].kind.c_str(), jesc(rows[i].label).c_str(), fmt_time(rows[i].when).c_str());
      std::printf("]}\n");
      return 0;
    }
    if (rows.empty()) std::printf("No debloat changes recorded.\n");
    for (auto& r : rows) std::printf("%s  %-8s %-26s %s\n", fmt_time(r.when).c_str(), r.kind.c_str(), r.label.c_str(), r.kind == "app" ? "(cannot be undone here)" : "undoable");
    return 0;
  }

  if (sub == "undo") {
    if (!a.has("all") && !a.has("id")) { std::fprintf(stderr, "usage: cna debloat undo --all | --id LABEL  [--yes]\n"); return 64; }
    auto rows = jload();
    auto ids = a.all("id");
    std::vector<JRow> keep;
    int undone = 0, failed = 0, manual = 0;
    // newest first
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
      JRow& r = *it;
      bool pick = a.has("all") || std::find(ids.begin(), ids.end(), r.label) != ids.end();
      if (!pick) { keep.push_back(r); continue; }
      if (r.kind == "app") { ++manual; std::printf("  manual       %-28s reinstall from the Microsoft Store\n", r.label.c_str()); keep.push_back(r); continue; }
      if (!G.yes) { std::printf("  would undo   %-28s %s\n", r.label.c_str(), r.name.c_str()); keep.push_back(r); continue; }
      std::string err;
      bool ok = true;
      if (r.kind == "power") { std::string out; ok = plat::run_command(r.undo_cmd, out) == 0; if (!ok) err = trim(out); }
      else if (r.prev_exists) ok = plat::reg_set(r.hive, r.key, r.name, type_from(r.prev_type), r.prev_data, err);
      else ok = plat::reg_delete_value(r.hive, r.key, r.name, err);
      if (ok) { ++undone; std::printf("  restored     %-28s %s\n", r.label.c_str(), r.name.c_str()); }
      else { ++failed; std::printf("  failed       %-28s %s\n", r.label.c_str(), err.c_str()); keep.push_back(r); }
    }
    if (G.yes) {
      std::reverse(keep.begin(), keep.end());
      jsave(keep);
      std::printf("\n%d restored, %d failed%s.\n", undone, failed, manual ? (", " + std::to_string(manual) + " Store app(s) need a manual reinstall").c_str() : "");
    } else std::printf("\nPreview only. Re-run with --yes to restore the previous values.\n");
    return failed ? 2 : 0;
  }

  std::fprintf(stderr, "usage: cna debloat [scan|apply|undo|journal]\n"
                       "  scan  [--packages-file F] [-v]\n"
                       "  apply [--preset safe|balanced|aggressive] [--only privacy,startup,services,disk,apps] [--apps] [--id ID ...] --yes\n"
                       "  undo  --all | --id LABEL  --yes\n");
  return 64;
}

} // namespace cna
