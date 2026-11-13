#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

namespace AgcDriver::HtileProfile {

enum Slot : std::size_t { DispatchPrologue, DispatchHle, DepthPrologue, DepthHle, StoreLock, StoreRead, StoreApply, StoreWrite, StoreNote, StoreTotal, SlotCount };
inline constexpr std::array<const char*, SlotCount> SlotNames{"mask-prologue", "mask-enqueue", "depth-prologue(incl. drain)", "depth-enqueue", "store-lock", "store-read", "store-apply", "store-write", "store-note", "store-total"};

inline bool Enabled() {
    static const bool on = std::getenv("APS5_PROFILE_HTILE") != nullptr;
    return on;
}

inline std::uint64_t Now() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct Counter {
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> ns{0};
    std::atomic<std::uint64_t> bytes{0};
};

struct DrainSite {
    std::atomic<std::uintptr_t> pc{0};
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> waited{0};
    std::atomic<std::uint64_t> ns{0};
};

inline std::array<Counter, SlotCount>& Counters() {
    static std::array<Counter, SlotCount> counters;
    return counters;
}

inline std::array<DrainSite, 32>& Sites() {
    static std::array<DrainSite, 32> sites;
    return sites;
}

inline void Report() {
    static std::atomic<std::uint64_t> last{0};
    const auto now = Now();
    auto seen = last.load(std::memory_order_relaxed);
    if (seen == 0) {
        last.compare_exchange_strong(seen, now);
        return;
    }
    if (now - seen < 10'000'000'000ull || !last.compare_exchange_strong(seen, now)) return;
    char line[4096];
    int n = std::snprintf(line, sizeof(line), "[htile-prof] (10 s)");
    for (std::size_t i = 0; i < SlotCount; ++i) {
        auto& c = Counters()[i];
        const auto calls = c.calls.exchange(0);
        const auto ns = c.ns.exchange(0);
        const auto bytes = c.bytes.exchange(0);
        if (calls == 0 || n >= static_cast<int>(sizeof(line))) continue;
        n += std::snprintf(line + n, sizeof(line) - static_cast<std::size_t>(n), " %s x%llu %.1fms (%.0fus each%s%.1fMiB)", SlotNames[i], static_cast<unsigned long long>(calls), ns / 1e6, ns / 1e3 / static_cast<double>(calls), bytes != 0 ? ", " : "", bytes != 0 ? bytes / 1048576.0 : 0.0);
    }
    std::fprintf(stderr, "%s\n", line);
    n = std::snprintf(line, sizeof(line), "[htile-prof] drains by caller (calls/waited, ms):");
    for (auto& site : Sites()) {
        const auto pc = site.pc.load(std::memory_order_acquire);
        if (pc == 0) break;
        const auto calls = site.calls.exchange(0);
        const auto waited = site.waited.exchange(0);
        const auto ns = site.ns.exchange(0);
        if (calls == 0 || n >= static_cast<int>(sizeof(line))) continue;
        Dl_info info{};
        const bool named = ::dladdr(reinterpret_cast<const void*>(pc), &info) != 0 && info.dli_fbase != nullptr;
        const char* module = named && info.dli_fname != nullptr ? info.dli_fname : "?";
        for (const char* p = module; *p != 0; ++p) {
            if (*p == '/') module = p + 1;
        }
        const auto offset = named ? pc - reinterpret_cast<std::uintptr_t>(info.dli_fbase) : pc;
        n += std::snprintf(line + n, sizeof(line) - static_cast<std::size_t>(n), " %s+0x%llx %llu/%llu %.1fms;", module, static_cast<unsigned long long>(offset), static_cast<unsigned long long>(calls), static_cast<unsigned long long>(waited), ns / 1e6);
    }
    std::fprintf(stderr, "%s\n", line);
}

inline void Add(Slot slot, std::uint64_t ns, std::uint64_t bytes = 0) {
    auto& c = Counters()[slot];
    c.calls.fetch_add(1, std::memory_order_relaxed);
    c.ns.fetch_add(ns, std::memory_order_relaxed);
    c.bytes.fetch_add(bytes, std::memory_order_relaxed);
    Report();
}

inline void NoteDrain(std::uintptr_t pc, bool waited, std::uint64_t ns) {
    for (auto& site : Sites()) {
        auto current = site.pc.load(std::memory_order_acquire);
        if (current == 0 && site.pc.compare_exchange_strong(current, pc, std::memory_order_acq_rel)) current = pc;
        if (current != pc) continue;
        site.calls.fetch_add(1, std::memory_order_relaxed);
        if (waited) site.waited.fetch_add(1, std::memory_order_relaxed);
        site.ns.fetch_add(ns, std::memory_order_relaxed);
        break;
    }
    Report();
}

}
