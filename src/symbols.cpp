#include "symbols.h"

#include <mutex>
#include <unordered_map>

namespace kcdx::symbols {

namespace {

struct Entry {
    uintptr_t   addr = 0;
    std::string owner;
};

std::mutex                              g_mutex;
std::unordered_map<std::string, Entry>  g_table;

}  // namespace

bool Register(const std::string& name, uintptr_t addr, const std::string& ownerName) {
    if (name.empty() || addr == 0) return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto [it, inserted] = g_table.try_emplace(name, Entry{addr, ownerName});
    return inserted;
}

std::optional<uintptr_t> Lookup(const std::string& name) {
    if (name.empty()) return std::nullopt;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_table.find(name);
    if (it == g_table.end()) return std::nullopt;
    return it->second.addr;
}

std::string OwnerOf(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_table.find(name);
    if (it == g_table.end()) return {};
    return it->second.owner;
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_table.size();
}

void ForEach(void (*fn)(const char* name, uintptr_t addr, const char* owner)) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& [name, entry] : g_table) {
        fn(name.c_str(), entry.addr, entry.owner.c_str());
    }
}

}  // namespace kcdx::symbols
