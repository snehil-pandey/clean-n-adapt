#include <array>
#include <fstream>
#include <unordered_map>

#include "cmds.hpp"
#include "sha256.hpp"

namespace cna {

using Hash = std::array<uint8_t, 32>;

static bool hash_file(const fs::path& p, uint64_t limit, Hash& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  static std::vector<char> buf(1 << 16);
  Sha256 s;
  uint64_t left = limit ? limit : UINT64_MAX;
  while (left && f) {
    size_t want = (size_t)std::min<uint64_t>(buf.size(), left);
    f.read(buf.data(), (std::streamsize)want);
    std::streamsize n = f.gcount();
    if (n <= 0) break;
    s.update((const uint8_t*)buf.data(), (size_t)n);
    left -= (uint64_t)n;
    tick();
  }
  if (f.bad()) return false;
  s.finish(out.data());
  return true;
}

static bool skip_dir_name(const std::string& lname) {
  static const std::set<std::string> skip = {".git", "node_modules", "$recycle.bin", "system volume information",
                                             "windows", "winsxs", "program files", "program files (x86)", "programdata"};
  return skip.count(lname) > 0;
}

std::vector<fs::path> default_dupe_roots() {
  std::vector<fs::path> v;
  fs::path h = home_dir();
  std::error_code ec;
  for (const char* d : {"Downloads", "Documents", "Desktop", "Pictures", "Videos", "Music"})
    if (!h.empty() && fs::is_directory(h / d, ec)) v.push_back(h / d);
  return v;
}

namespace {
struct Cand { uint64_t size; uint32_t off, len; };
}

std::vector<DupSet> find_dupes(const std::vector<fs::path>& roots, uint64_t min_size, uint64_t max_size, bool default_skips) {
  auto size_ok = [&](uint64_t s) { return s >= min_size && (!max_size || s <= max_size); };
  auto visit_dir = [&](const Entry& e) { return !(default_skips && skip_dir_name(lower(u8(e.path.filename())))); };

  // Pass 1: count files per size (a small hash map; no paths stored yet).
  std::unordered_map<uint64_t, uint32_t> sizes;
  for (auto& r : roots)
    walk(r, [&](const Entry& e) {
      if (e.is_dir) return visit_dir(e);
      if (size_ok(e.size)) { auto& c = sizes[e.size]; if (c < UINT32_MAX) ++c; }
      return true;
    });

  // Pass 2: remember paths only for sizes seen more than once.
  std::string arena;
  std::vector<Cand> cands;
  for (auto& r : roots)
    walk(r, [&](const Entry& e) {
      if (e.is_dir) return visit_dir(e);
      if (!size_ok(e.size)) return true;
      auto it = sizes.find(e.size);
      if (it == sizes.end() || it->second < 2) return true;
      if (plat::is_cloud_placeholder(e.path)) return true; // reading would trigger a download
      std::string s = u8(e.path);
      cands.push_back({e.size, (uint32_t)arena.size(), (uint32_t)s.size()});
      arena += s;
      return true;
    });
  std::unordered_map<uint64_t, uint32_t>().swap(sizes);
  std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.size < b.size; });

  auto path_of = [&](const Cand& c) { return P(arena.substr(c.off, c.len)); };
  std::vector<DupSet> sets;

  for (size_t i = 0; i < cands.size();) {
    size_t j = i;
    while (j < cands.size() && cands[j].size == cands[i].size) ++j;
    if (j - i >= 2) {
      uint64_t size = cands[i].size;
      // Stage 1: hash first 16 KB.
      std::map<Hash, std::vector<size_t>> by_head;
      for (size_t k = i; k < j; ++k) {
        Hash h;
        if (hash_file(path_of(cands[k]), 16384, h)) by_head[h].push_back(k);
      }
      for (auto& hb : by_head) {
        if (hb.second.size() < 2) continue;
        // Stage 2: full-content hash.
        std::map<Hash, std::vector<size_t>> by_full;
        for (size_t k : hb.second) {
          Hash h;
          if (size <= 16384) h = hb.first;
          else if (!hash_file(path_of(cands[k]), 0, h)) continue;
          by_full[h].push_back(k);
        }
        for (auto& fb : by_full) {
          // Hard links share data already: count each physical file once.
          std::vector<plat::FileId> ids;
          std::vector<size_t> distinct;
          for (size_t k : fb.second) {
            auto id = plat::file_id(path_of(cands[k]));
            bool seen = false;
            for (auto& x : ids) if (x == id) { seen = true; break; }
            if (seen) continue;
            ids.push_back(id);
            distinct.push_back(k);
          }
          if (distinct.size() < 2) continue;
          DupSet ds;
          ds.size = size;
          ds.wasted = size * (distinct.size() - 1);
          fs::file_time_type best{};
          bool first = true;
          for (size_t n = 0; n < distinct.size(); ++n) {
            fs::path p = path_of(cands[distinct[n]]);
            ds.paths.push_back(u8(p));
            std::error_code ec;
            auto mt = fs::last_write_time(p, ec);
            // keeper = oldest copy, ties -> shorter path
            if (first || mt < best || (mt == best && ds.paths.back().size() < ds.paths[ds.keeper].size())) {
              best = mt; ds.keeper = n; first = false;
            }
          }
          sets.push_back(std::move(ds));
        }
      }
    }
    i = j;
  }
  std::sort(sets.begin(), sets.end(), [](const DupSet& a, const DupSet& b) { return a.wasted > b.wasted; });
  return sets;
}

int dupes_quarantine(const std::vector<DupSet>& sets, uint64_t& moved_bytes, uint64_t& moved_files) {
  int failed = 0;
  for (auto& s : sets)
    for (size_t i = 0; i < s.paths.size(); ++i) {
      if (i == s.keeper) continue;
      std::string err;
      if (q_move(P(s.paths[i]), "duplicate of " + s.paths[s.keeper], s.size, err).empty()) {
        std::fprintf(stderr, "skip %s: %s\n", s.paths[i].c_str(), err.c_str());
        ++failed;
      } else { moved_bytes += s.size; ++moved_files; }
    }
  return failed;
}

int cmd_dupes(const Args& a) {
  uint64_t min_size = 1u << 20, max_size = 0;
  if (a.has("min-size") && !parse_size(a.get("min-size"), min_size)) { std::fprintf(stderr, "bad --min-size\n"); return 64; }
  if (a.has("max-size") && !parse_size(a.get("max-size"), max_size)) { std::fprintf(stderr, "bad --max-size\n"); return 64; }
  std::vector<fs::path> roots;
  for (auto& s : a.pos) roots.push_back(P(s));
  if (roots.empty()) roots = default_dupe_roots();
  if (roots.empty()) { std::fprintf(stderr, "no roots given and no default folders found\n"); return 64; }
  bool skips = !a.has("no-skip");

  std::vector<DupSet> sets;
  std::string of = a.get("of");
  if (!of.empty()) {
    // "Is there another copy of this one file?"
    fs::path target = P(of);
    std::error_code ec;
    if (!fs::is_regular_file(target, ec)) { std::fprintf(stderr, "--of must be a file\n"); return 64; }
    uint64_t sz = fs::file_size(target, ec);
    Hash th;
    if (!hash_file(target, 0, th)) { std::fprintf(stderr, "cannot read %s\n", of.c_str()); return 1; }
    auto tid = plat::file_id(target);
    DupSet ds;
    ds.size = sz;
    ds.paths.push_back(u8(target));
    for (auto& r : roots)
      walk(r, [&](const Entry& e) {
        if (e.is_dir) return !(skips && skip_dir_name(lower(u8(e.path.filename()))));
        if (e.size != sz || plat::is_cloud_placeholder(e.path)) return true;
        if (plat::file_id(e.path) == tid) return true;
        Hash h;
        if (hash_file(e.path, 0, h) && h == th) ds.paths.push_back(u8(e.path));
        return true;
      });
    if (ds.paths.size() > 1) { ds.wasted = sz * (ds.paths.size() - 1); sets.push_back(ds); }
  } else {
    sets = find_dupes(roots, min_size, max_size, skips);
  }

  uint64_t wasted = 0;
  for (auto& s : sets) wasted += s.wasted;

  uint64_t qb = 0, qf = 0;
  int qfail = 0;
  bool do_q = a.has("quarantine") && G.yes;
  if (do_q) qfail = dupes_quarantine(sets, qb, qf);

  if (G.json) {
    std::printf("{\"sets\":[");
    for (size_t i = 0; i < sets.size(); ++i) {
      std::printf("%s{\"size\":%llu,\"wasted\":%llu,\"keeper\":%zu,\"paths\":[", i ? "," : "", (unsigned long long)sets[i].size,
                  (unsigned long long)sets[i].wasted, sets[i].keeper);
      for (size_t j = 0; j < sets[i].paths.size(); ++j) std::printf("%s\"%s\"", j ? "," : "", jesc(sets[i].paths[j]).c_str());
      std::printf("]}");
    }
    std::printf("],\"total_wasted\":%llu,\"quarantined_files\":%llu}\n", (unsigned long long)wasted, (unsigned long long)qf);
    return qfail ? 2 : 0;
  }
  if (sets.empty()) { std::printf("No duplicates found (min size %s).\n", human(min_size).c_str()); return 0; }
  size_t limit = (size_t)std::atoi(a.get("limit", "25").c_str());
  size_t shown = 0;
  for (auto& s : sets) {
    if (shown++ >= limit) break;
    std::printf("\n%s x %zu  (wasted %s)\n", human(s.size).c_str(), s.paths.size(), human(s.wasted).c_str());
    for (size_t j = 0; j < s.paths.size(); ++j) std::printf("  %s %s\n", j == s.keeper ? "keep" : "dupe", s.paths[j].c_str());
  }
  if (sets.size() > limit) std::printf("\n... %zu more sets (use --limit N or --json)\n", sets.size() - limit);
  std::printf("\n%zu duplicate set(s), %s reclaimable.\n", sets.size(), human(wasted).c_str());
  if (do_q) std::printf("Moved %llu file(s) (%s) to quarantine. Undo with `cna quarantine`.\n", (unsigned long long)qf, human(qb).c_str());
  else std::printf("Add --quarantine --yes to move the extra copies to quarantine (reversible).\n");
  return qfail ? 2 : 0;
}

} // namespace cna
