#pragma once
#include <optional>

#include "common.hpp"
#include "platform.hpp"

namespace cna {

// ---- clean.cpp -------------------------------------------------------------
enum Profile : unsigned { QUICK = 1, BROWSER = 2, DEV = 4, GAMING = 8, WINDOWS = 16, APPS = 32, CUSTOM = 64, FULL = 127 };
struct Stats { uint64_t files = 0, bytes = 0, failed = 0, failed_bytes = 0; };
struct RuleResult { std::string rule, note, path, warn; Stats st; };
unsigned parse_profiles(const std::string& csv, std::string& bad);
void add_custom_rule(const std::string& pattern);
std::vector<RuleResult> run_clean(unsigned mask, double override_age, bool apply, std::vector<std::string>& skipped_admin);
int cmd_clean(const Args& a, bool force_dry);

// ---- quarantine.cpp --------------------------------------------------------
std::string q_move(const fs::path& src, const std::string& reason, uint64_t bytes, std::string& err);
int q_purge(double older_than_days, bool all, uint64_t& freed);
int cmd_quarantine(const Args& a);

// ---- dupes.cpp -------------------------------------------------------------
struct DupSet { uint64_t size = 0, wasted = 0; std::vector<std::string> paths; size_t keeper = 0; };
std::vector<DupSet> find_dupes(const std::vector<fs::path>& roots, uint64_t min_size, uint64_t max_size, bool default_skips);
std::vector<fs::path> default_dupe_roots();
int cmd_dupes(const Args& a);
int dupes_quarantine(const std::vector<DupSet>& sets, uint64_t& moved_bytes, uint64_t& moved_files,
                     const std::set<std::string>& selected = {});

// ---- remnants.cpp ----------------------------------------------------------
struct Remnant { std::string path, kind, reason; uint64_t bytes = 0; int score = 0; double newest_days = 0; bool truncated = false; };
class OwnerIndex {
 public:
  void build(const std::vector<plat::App>& apps);
  const plat::App* find(const std::string& folder_name, const fs::path& p) const;
  size_t size() const { return apps_.size(); }
 private:
  struct AppN { plat::App a; std::set<std::string> names, toks; std::string loc; };
  std::vector<AppN> apps_;
};
std::vector<plat::App> load_apps(const std::string& apps_file);
std::vector<Remnant> find_remnants(int min_score, const std::vector<plat::App>& apps, std::vector<std::string>& stale);
int remnants_quarantine(const std::vector<Remnant>& r, uint64_t& moved_bytes);
int cmd_remnants(const Args& a);
int cmd_apps(const Args& a);

struct RuleVerdict { std::string kind = "unknown", safety = "unknown", summary; };
RuleVerdict rule_verdict(const fs::path& p, const TreeInfo& ti, const std::set<std::string>& top);
bool system_folder_name(const std::string& lowercase_name);

// ---- explain.cpp -----------------------------------------------------------
namespace llm {
bool available();
std::optional<std::string> describe(const std::string& facts);
}
int cmd_explain(const Args& a);
int cmd_kb(const Args& a);

int cmd_debloat(const Args& a);

// ---- autorun.cpp -----------------------------------------------------------
int cmd_auto(const Args& a);
int cmd_config(const Args& a);

} // namespace cna
