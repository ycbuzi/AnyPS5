#include "SceTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <future>

extern "C" {
KernelCpumask APS5_VABI sceKernelGetAvailableCpumask(void);
int APS5_VABI scePthreadCreate(Pthread* thread, const PthreadAttr* attr, PthreadEntry entry, void* arg, const char* name);
int APS5_VABI scePthreadJoin(Pthread thread, void** retval);
int APS5_VABI scePthreadGetaffinity(Pthread thread, KernelCpumask* mask);
int APS5_VABI scePthreadSetaffinity(Pthread thread, KernelCpumask mask);
int APS5_VABI scePthreadAttrInit(PthreadAttr* attr);
int APS5_VABI scePthreadAttrDestroy(PthreadAttr* attr);
int APS5_VABI scePthreadAttrGetaffinity(const PthreadAttr* attr, KernelCpumask* mask);
Pthread APS5_VABI pthread_self_nid_postfix(void);
int APS5_VABI pthread_getaffinity_np_nid_postfix(Pthread thread, std::size_t cpusetSize, void* cpuset);
int APS5_VABI cpuset_getaffinity_nid_postfix(int level, int which, std::int64_t id, std::size_t size, void* mask);
int* APS5_VABI __error_nid_postfix();
}

static constexpr int SCE_OK = 0;
static constexpr int PS5_LOGICAL_CPUS = 16;

static void Require(bool value) { if (!value) std::abort(); }

static constexpr std::size_t GUEST_CPUSET_BYTES = 32;
static constexpr int GUEST_ESRCH = 3;
static constexpr int GUEST_EFAULT = 14;
static constexpr int GUEST_ERANGE = 34;

static KernelCpumask ReadCpuset(Pthread thread) {
    std::uint8_t cpuset[GUEST_CPUSET_BYTES + 8];
    std::memset(cpuset, 0xAB, sizeof(cpuset));
    Require(pthread_getaffinity_np_nid_postfix(thread, GUEST_CPUSET_BYTES, cpuset) == 0);
    for (std::size_t i = sizeof(KernelCpumask); i < GUEST_CPUSET_BYTES; ++i) Require(cpuset[i] == 0);
    for (std::size_t i = GUEST_CPUSET_BYTES; i < sizeof(cpuset); ++i) Require(cpuset[i] == 0xAB);
    KernelCpumask mask = 0;
    std::memcpy(&mask, cpuset, sizeof(mask));
    return mask;
}

static void* APS5_VABI Worker(void* arg) {
    static_cast<std::future<void>*>(arg)->get();
    return nullptr;
}

static void* APS5_VABI ReportCpuset(void* arg) {
    auto* result = static_cast<std::uint64_t*>(arg);
    std::uint64_t set[4] = {~0ull, ~0ull, ~0ull, ~0ull};
    result[0] = cpuset_getaffinity_nid_postfix(3, 1, -1, 8, set);
    result[1] = set[0];
    result[2] = set[1];
    result[3] = cpuset_getaffinity_nid_postfix(3, 1, -1, sizeof(set), set);
    result[4] = set[0] | (set[1] != 0 || set[2] != 0 || set[3] != 0 ? 1ull << 63 : 0);
    result[5] = cpuset_getaffinity_nid_postfix(3, 1, -1, 4, set) == -1 && *__error_nid_postfix() == 34;
    result[6] = cpuset_getaffinity_nid_postfix(3, 1, -1, 33, set) == -1 && *__error_nid_postfix() == 34;
    result[7] = cpuset_getaffinity_nid_postfix(3, 1, -1, 8, nullptr) == -1 && *__error_nid_postfix() == 14;
    return nullptr;
}

static void* APS5_VABI ReportMask(void* arg) {
    *static_cast<KernelCpumask*>(arg) = sceKernelGetAvailableCpumask();
    return nullptr;
}

int main() {
    const KernelCpumask available = sceKernelGetAvailableCpumask();
    Require(available != 0);
    Require((available >> PS5_LOGICAL_CPUS) == 0);
    Require(sceKernelGetAvailableCpumask() == available);

    PthreadAttr attr = nullptr;
    Require(scePthreadAttrInit(&attr) == SCE_OK);
    KernelCpumask defaultAffinity = 0;
    Require(scePthreadAttrGetaffinity(&attr, &defaultAffinity) == SCE_OK);
    Require(defaultAffinity == available);

    std::promise<void> release;
    auto released = release.get_future();
    Pthread thread = nullptr;
    Require(scePthreadCreate(&thread, &attr, Worker, &released, nullptr) == SCE_OK);
    KernelCpumask threadAffinity = 0;
    Require(scePthreadGetaffinity(thread, &threadAffinity) == SCE_OK);
    Require(threadAffinity == available);

    for (KernelCpumask cpu = 1; cpu != 0; cpu <<= 1) {
        if ((available & cpu) == 0) continue;
        Require(scePthreadSetaffinity(thread, cpu) == SCE_OK);
        Require(scePthreadGetaffinity(thread, &threadAffinity) == SCE_OK);
        Require(threadAffinity == cpu);
        Require(ReadCpuset(thread) == cpu);
    }
    release.set_value();
    Require(scePthreadJoin(thread, nullptr) == SCE_OK);

    KernelCpumask fromThread = 0;
    Require(scePthreadCreate(&thread, &attr, ReportMask, &fromThread, nullptr) == SCE_OK);
    Require(scePthreadJoin(thread, nullptr) == SCE_OK);
    Require(fromThread == available);

    std::uint64_t cpuset[8] = {};
    Require(scePthreadCreate(&thread, &attr, ReportCpuset, cpuset, nullptr) == SCE_OK);
    Require(scePthreadJoin(thread, nullptr) == SCE_OK);
    Require(cpuset[0] == 0 && cpuset[1] == available && cpuset[2] == ~0ull);
    Require(cpuset[3] == 0 && cpuset[4] == available);
    Require(cpuset[5] == 1 && cpuset[6] == 1 && cpuset[7] == 1);
    Require(scePthreadAttrDestroy(&attr) == SCE_OK);

    Require(ReadCpuset(pthread_self_nid_postfix()) == available);
    std::uint8_t cpuset[GUEST_CPUSET_BYTES]{};
    for (const std::size_t size : {std::size_t{0}, std::size_t{8}, std::size_t{31}, std::size_t{33}, std::size_t{64}})
        Require(pthread_getaffinity_np_nid_postfix(pthread_self_nid_postfix(), size, cpuset) == GUEST_ERANGE);
    Require(pthread_getaffinity_np_nid_postfix(nullptr, GUEST_CPUSET_BYTES, cpuset) == GUEST_ESRCH);
    Require(pthread_getaffinity_np_nid_postfix(pthread_self_nid_postfix(), GUEST_CPUSET_BYTES, nullptr) == GUEST_EFAULT);
}
