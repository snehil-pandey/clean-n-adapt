#pragma once
// Tiny on-device classifier: "is this folder safe to clean, worth reviewing, or must it be kept?"
// Hashed sparse features + softmax regression trained with SGD. No dependencies, ~200 KB of weights.
#include <ctime>
#include <utility>

#include "common.hpp"

namespace cna {

enum Cls { C_REGEN = 0, C_CAUTION = 1, C_KEEP = 2, C_N = 3 };
const char* cls_name(int c);   // "regenerable" | "caution" | "keep"
int cls_from(const std::string& s); // -1 if unknown

// Everything the model is allowed to know about a folder (no file contents, only names/sizes/dates).
struct Descriptor {
  std::string name, parent, root_kind;
  std::vector<std::string> children;                     // lowercase top-level child names (<= 40)
  std::vector<std::pair<std::string, uint64_t>> ext_bytes; // top extensions by bytes (<= 6)
  uint64_t bytes = 0, files = 0, dirs = 0;
  double newest_days = -1;                               // -1 = unknown / empty
  bool has_exe = false, has_uninstaller = false, owner_known = false;
};

struct Prediction {
  int cls = C_CAUTION;
  float prob[C_N] = {0, 0, 0};
  std::vector<std::pair<std::string, float>> why; // human-readable reasons, strongest first (only if requested)
  float conf() const { return prob[cls]; }
};

class OwnerIndex;
std::string root_kind_of(const fs::path& p);
Descriptor make_descriptor(const fs::path& p, const TreeInfo& ti, const std::set<std::string>& top_children, bool owner_known);
Descriptor describe_path(const fs::path& p, const OwnerIndex* owners, uint64_t max_entries = 60000);

// What the app should DO with a folder. The model proposes; these rules (not learned) have the last word.
struct Advice {
  std::string action;  // "clean" | "review" | "keep"
  int cls = C_CAUTION;
  float conf = 0;
  std::string note;    // why the action differs from the raw prediction, if it does
};
bool sensitive_name(const Descriptor& d);   // credentials / wallets / keys / tdata ... never cleaned automatically
bool cleanish_name(const Descriptor& d);    // name or parent looks like cache / temp / logs / crash dumps
Advice advise(const Descriptor& d, const std::string& path, const Prediction& pr);

struct Model {
  static constexpr int D = 16384;
  std::vector<float> w; // C_N * D
  uint32_t examples = 0, feedback = 0;
  std::time_t trained = 0;
  Prediction predict(const Descriptor& d, bool explain = false) const;
};

const Model& model();                                                // loads, or trains from the built-in seed
bool model_feedback(const Descriptor& d, const std::string& path, int label, std::string& err);
void model_reset();

struct EvalResult {
  int n = 0, correct = 0;
  int confusion[C_N][C_N] = {{0}};     // [true][predicted]
  int dangerous = 0, dangerous_gated = 0, dangerous_total = 0; // caution/keep predicted as regenerable with p >= 0.85
};
EvalResult model_eval(); // grouped 5-fold cross-validation on the built-in seed (unseen folder names)

int cmd_model(const Args& a);
int cmd_recommend(const Args& a);

} // namespace cna
