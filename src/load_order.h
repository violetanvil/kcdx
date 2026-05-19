#pragma once
#include <string>
#include <utility>
#include <vector>

namespace kcdx::load_order {

// Compute final load order for a set of entry names. Today this is the
// existing (priority, name) tiebreaker; a future revision will also read
// `<plugins-dir>/load_order.txt` and apply user-specified overrides.
//
// Input: a vector of (name, priority) pairs in their TOML-discovery order.
// Output: a permutation of the input indices in final apply order.
//
// Stable: equal-priority entries retain discovery order (which is itself
// already (priority asc, name asc) since config.cpp sorts after parsing).
//
// Phase 4b.3 ships the stub: returns indices in input order, since input
// is already correctly pre-sorted. The function exists so that when
// load_order.txt support lands (planned v0.2), all call sites already
// route through this single point.
std::vector<size_t> ComputeOrder(
    const std::vector<std::pair<std::string, int>>& entries);

}  // namespace kcdx::load_order
