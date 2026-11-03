#include <new>

#include "cmds.hpp"
#include "model.hpp"

using namespace cna;

static void usage() {
  std::printf(
      "cna %s - lightweight cleaner (hard memory cap, default 125 MB)\n\n"
      "Cleaning\n"
      "  cna scan   [--profile P]            preview what a clean would free\n"
      "  cna clean  [--profile P] --yes      delete caches (preview without --yes)\n"
      "      profiles: quick browser dev gaming windows apps custom full (comma-separated)\n"
      "      --min-age DAYS  keep files newer than this     -v  list every folder\n\n"
      "Finding things\n"
      "  cna dupes [paths...] [--min-size 1M] [--quarantine --yes]\n"
      "  cna dupes --of FILE [paths...]      is there another copy of this file?\n"
      "  cna remnants [--min-score 65] [--apps-file F] [--quarantine --yes] [--select PATH ...]\n"
      "  cna remnants --app NAME             everything an (uninstalled) app left behind\n"
      "  cna explain PATH [--refresh] [--llm] [--no-store]\n"
      "  cna kb [list|search WORDS|forget PATH]\n\n"
      "Safety net\n"
      "  cna quarantine [list|restore ID|purge --older-than DAYS --yes]\n\n"
      "Automation\n"
      "  cna config [path|init|show]\n"
      "  cna auto   [run|install|remove|status] [--force] [--dry-run] [--every-hours N]\n\n"
      "Global: --json  --yes/-y  --quiet/-q  --verbose/-v  --mem-cap MB  --state DIR  --exclude PATH\n"
      "Exit codes: 0 ok, 1 error, 2 partial (some items in use/failed), 3 memory cap hit, 64 usage\n",
      VERSION);
}

static int run(const std::vector<std::string>& argv) {
  if (argv.size() < 2 || argv[1] == "help" || argv[1] == "--help" || argv[1] == "-h") { usage(); return argv.size() < 2 ? 64 : 0; }
  std::string cmd = argv[1];
  if (cmd == "version" || cmd == "--version") { std::printf("cna %s\n", VERSION); return 0; }

  Args a = parse_args(argv, 2);
  if (a.has("help")) { usage(); return 0; }
  G.json = a.has("json");
  G.yes = a.has("yes");
  G.quiet = a.has("quiet");
  G.verbose = a.has("verbose");
  if (a.has("state")) G.state = P(a.get("state"));
  for (auto& e : a.all("exclude")) G.excludes.push_back(P(e));
  size_t cap_mb = 125;
  if (a.has("mem-cap")) {
    int v = std::atoi(a.get("mem-cap").c_str());
    if (v < 16) { std::fprintf(stderr, "--mem-cap must be at least 16 (MB)\n"); return 64; }
    cap_mb = (size_t)v;
  }
  G.mem_cap = cap_mb * 1024 * 1024;

  // The auto command may override the cap from its config; everything else uses the flag.
  std::string msg;
  if (!plat::apply_mem_cap(G.mem_cap, msg) && !G.quiet)
    std::fprintf(stderr, "warning: OS memory cap not applied (%s); internal budget check still active\n", msg.c_str());

  if (cmd == "scan") return cmd_clean(a, true);
  if (cmd == "clean") return cmd_clean(a, false);
  if (cmd == "dupes") return cmd_dupes(a);
  if (cmd == "remnants") return cmd_remnants(a);
  if (cmd == "explain") return cmd_explain(a);
  if (cmd == "kb") return cmd_kb(a);
  if (cmd == "model") return cmd_model(a);
  if (cmd == "recommend") return cmd_recommend(a);
  if (cmd == "debloat") return cmd_debloat(a);
  if (cmd == "quarantine") return cmd_quarantine(a);
  if (cmd == "config") return cmd_config(a);
  if (cmd == "auto") return cmd_auto(a);
  if (cmd == "mem") {
    std::printf("cap: %s   in use now: %s\n", human(G.mem_cap).c_str(), human(plat::private_bytes()).c_str());
    return 0;
  }
  std::fprintf(stderr, "unknown command '%s' (try `cna help`)\n", cmd.c_str());
  return 64;
}

int main(int argc, char** argv) {
  plat::console_init();
  auto args = plat::utf8_args(argc, argv);
  try {
    return run(args);
  } catch (const CapExceeded& e) {
    std::fprintf(stderr, "stopped: %s\n", e.what());
    return 3;
  } catch (const std::bad_alloc&) {
    std::fprintf(stderr, "stopped: out of memory under the %s cap; narrow the scan or raise --mem-cap\n", human(G.mem_cap).c_str());
    return 3;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
