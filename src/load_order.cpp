#include "load_order.h"

namespace kcdx::load_order {

std::vector<size_t> ComputeOrder(
    const std::vector<std::pair<std::string, int>>& entries) {
    // Phase 4b.3 stub: input is already (priority asc, name asc) from
    // config::LoadAllConfigs. Return identity permutation.
    //
    // When load_order.txt support lands (planned v0.2):
    //   1. Read <plugins-dir>/load_order.txt (one plugin-folder-name per line)
    //   2. Build a name -> file-position map
    //   3. Sort entries by (file_position if present else INT_MAX, priority, name)
    //   4. Log a WARN line for each name in load_order.txt that doesn't appear in entries
    //   5. Log an INFO line for each entry not in load_order.txt (will load at end)
    std::vector<size_t> order;
    order.reserve(entries.size());
    for (size_t i = 0; i < entries.size(); ++i) order.push_back(i);
    return order;
}

}  // namespace kcdx::load_order
