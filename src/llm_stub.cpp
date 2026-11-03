#include "cmds.hpp"

// Default build: no language model. `cna explain` still works through the built-in
// signature database and the persistent knowledge base.
//
// To add a real model, replace this file with an implementation of the same two
// functions (e.g. on top of llama.cpp with a ~100M-parameter quantised model) and run
// it ONLY after the scan has finished and its buffers are freed, so that the model
// and the scanner never share the 125 MB budget at the same time.
namespace cna::llm {

bool available() { return false; }
std::optional<std::string> describe(const std::string&) { return std::nullopt; }

} // namespace cna::llm
