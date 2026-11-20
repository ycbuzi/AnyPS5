#define VK_NO_PROTOTYPES
#define _CRT_SECURE_NO_WARNINGS
#define VK_USE_PLATFORM_WIN32_KHR
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.applicationmodel.core.h>
#include <d3dcompiler.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <map>
#include <mutex>
#include <atomic>
#include <string>
#include <vector>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <set>
#include <thread>

#define XVK extern "C" __declspec(dllexport)

namespace {

template <class T> T* Obj(uint64_t h) { return reinterpret_cast<T*>(static_cast<uintptr_t>(h)); }
template <class T> uint64_t Handle(T* p) { return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p)); }
template <class T> void Release(T*& p) { if (p) { p->Release(); p = nullptr; } }
uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

void Log(const char* fmt, ...) {
    static FILE* f = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        char path[MAX_PATH];
        if (GetEnvironmentVariableA("XVK_LOG", path, MAX_PATH)) f = std::fopen(path, "a");
    }
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fflush(f);
}

bool SkipWork(char kind) {
    static const char* skip = std::getenv("XVK_SKIP");
    return skip && std::strchr(skip, kind);
}

void ReportRemoval(ID3D12Device* device) {
    static bool reported = false;
    if (reported) return;
    reported = true;
    Log("device removed reason %08x", unsigned(device->GetDeviceRemovedReason()));
    ID3D12DeviceRemovedExtendedData* dred = nullptr;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dred)))) { Log("dred data unavailable"); return; }
    D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT crumbs{};
    if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput(&crumbs))) {
        for (const D3D12_AUTO_BREADCRUMB_NODE* n = crumbs.pHeadAutoBreadcrumbNode; n; n = n->pNext) {
            const UINT done = n->pLastBreadcrumbValue ? *n->pLastBreadcrumbValue : 0;
            if (done >= n->BreadcrumbCount) continue;
            Log("dred list: %u of %u ops done", done, n->BreadcrumbCount);
            for (UINT i = done > 4 ? done - 4 : 0; i < n->BreadcrumbCount && i < done + 3; ++i) Log("dred op %u: %d%s", i, int(n->pCommandHistory[i]), i == done ? " <- next" : "");
        }
    } else {
        Log("dred breadcrumbs unavailable");
    }
    D3D12_DRED_PAGE_FAULT_OUTPUT fault{};
    if (SUCCEEDED(dred->GetPageFaultAllocationOutput(&fault))) Log("dred page fault va %llx existing %d freed %d", (unsigned long long)fault.PageFaultVA, fault.pHeadExistingAllocationNode != nullptr, fault.pHeadRecentFreedAllocationNode != nullptr);
    dred->Release();
}

struct Perf {
    std::atomic<uint64_t> submitUs{0}, waitUs{0}, pipeUs{0}, pipes{0}, presentUs{0}, acquireUs{0}, submits{0}, draws{0}, waits{0}, stagingNew{0}, stagingReused{0}, stagingBytes{0}, directCopies{0}, stagedCopies{0}, bytesImage{0}, bytesCopy{0}, bytesUpdate{0}, bytesFill{0}, bytesSame{0}, copyRows{0}, gpuUs{0}, gpuTimed{0}, gpuSkipped{0};
};
Perf g_perf;

uint64_t NowUs() {
    static const uint64_t freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return uint64_t(f.QuadPart); }();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return uint64_t(t.QuadPart) * 1000000ull / freq;
}

struct PerfTimer {
    std::atomic<uint64_t>& slot;
    uint64_t start;
    explicit PerfTimer(std::atomic<uint64_t>& s) : slot(s), start(NowUs()) {}
    ~PerfTimer() { slot += NowUs() - start; }
};

uint64_t ProcessCpuUs() {
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    return ((uint64_t(k.dwHighDateTime) << 32 | k.dwLowDateTime) + (uint64_t(u.dwHighDateTime) << 32 | u.dwLowDateTime)) / 10;
}

void PerfReport(uint64_t frames) {
    static uint64_t lastWall = 0, lastCpu = 0;
    const uint64_t wall = NowUs(), cpu = ProcessCpuUs();
    if (lastWall) {
        const double f = double(frames), ms = double(wall - lastWall) / 1000.0;
        auto per = [&](std::atomic<uint64_t>& v) { return double(v.exchange(0)) / 1000.0 / f; };
        const double submit = per(g_perf.submitUs), wait = per(g_perf.waitUs), present = per(g_perf.presentUs), acquire = per(g_perf.acquireUs), pipe = per(g_perf.pipeUs);
        const double gpuTimed = double(g_perf.gpuTimed.exchange(0)), gpuSkipped = double(g_perf.gpuSkipped.exchange(0));
        if (gpuTimed > 0) Log("perf: gpu %.1f ms/frame over %.1f timed submits/frame (%.1f untimed)", per(g_perf.gpuUs), gpuTimed / f, gpuSkipped / f);
        Log("perf: %.2f fps, frame %.1f ms | submit %.1f ms (%.1f/frame) wait %.1f (%.1f/frame) present %.1f acquire %.1f pipelines %.1f (%llu new) | draws %.0f/frame | staging %.1f new %.1f reused %.2f MB/frame | image copies %.1f direct %.1f staged | MB img %.1f copy %.1f same %.1f update %.2f fill %.1f rows %.0f | cpu %.2f cores",
            f * 1000.0 / ms, ms / f, submit, double(g_perf.submits.exchange(0)) / f, wait, double(g_perf.waits.exchange(0)) / f, present, acquire, pipe, (unsigned long long)g_perf.pipes.exchange(0),
            double(g_perf.draws.exchange(0)) / f, double(g_perf.stagingNew.exchange(0)) / f, double(g_perf.stagingReused.exchange(0)) / f, double(g_perf.stagingBytes.exchange(0)) / 1048576.0 / f,
            double(g_perf.directCopies.exchange(0)) / f, double(g_perf.stagedCopies.exchange(0)) / f, double(g_perf.bytesImage.exchange(0)) / 1048576.0 / f, double(g_perf.bytesCopy.exchange(0)) / 1048576.0 / f, double(g_perf.bytesSame.exchange(0)) / 1048576.0 / f, double(g_perf.bytesUpdate.exchange(0)) / 1048576.0 / f, double(g_perf.bytesFill.exchange(0)) / 1048576.0 / f, double(g_perf.copyRows.exchange(0)) / f, double(cpu - lastCpu) / double(wall - lastWall));
    }
    lastWall = wall;
    lastCpu = cpu;
}

struct Tracked {
    ID3D12Resource* res = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
};

struct XInstance { IDXGIFactory4* factory = nullptr; std::vector<struct XPhysical*> physicals; };
struct XPhysical { XInstance* instance = nullptr; IDXGIAdapter1* adapter = nullptr; DXGI_ADAPTER_DESC1 desc{}; bool int64 = false; std::map<int, UINT> support2; };
struct XQueue { struct XDevice* device = nullptr; ID3D12CommandQueue* queue = nullptr; ID3D12Fence* idle = nullptr; uint64_t idleValue = 0; HANDLE event = nullptr; };

struct DescriptorRing {
    ID3D12DescriptorHeap* heap = nullptr;
    UINT size = 0, count = 0, next = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE Cpu(UINT i) const { auto h = heap->GetCPUDescriptorHandleForHeapStart(); h.ptr += SIZE_T(i) * size; return h; }
    D3D12_GPU_DESCRIPTOR_HANDLE Gpu(UINT i) const { auto h = heap->GetGPUDescriptorHandleForHeapStart(); h.ptr += UINT64(i) * size; return h; }
    UINT Take() { UINT i = next; next = (next + 1) % count; return i; }
};

struct XDevice {
    XPhysical* physical = nullptr;
    ID3D12Device* device = nullptr;
    XQueue queue;
    std::mutex spareLock;
    std::vector<std::pair<ID3D12CommandAllocator*, ID3D12GraphicsCommandList*>> spareLists;
    DescriptorRing rtv, dsv, srv, smp;
    ID3D12RootSignature* blitRoot = nullptr;
    std::map<int, ID3D12PipelineState*> blitPso;
    std::mutex lock;
    UINT bdaBase = 0, bdaSlots = 0;
    std::vector<UINT> bdaFree;
    UINT bdaNext = 0;
};

struct XMemory {
    XDevice* device = nullptr;
    uint32_t type = 0;
    uint64_t size = 0;
    Tracked whole;
    void* mapped = nullptr;
    int bda = -1;
    ID3D12Heap* imported = nullptr;
};

struct XBuffer { uint64_t size = 0; XMemory* memory = nullptr; uint64_t offset = 0; };

struct XImage {
    D3D12_RESOURCE_DESC desc{};
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent3D extent{};
    uint32_t mips = 1, layers = 1;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    Tracked t;
    bool owned = true;
    ID3D12Device* d3d = nullptr;
    ID3D12Heap* heap = nullptr;
    bool fresh = false;
    std::vector<std::pair<DXGI_FORMAT, Tracked>> aliases;
};

struct SpareImage {
    ID3D12Device* d3d = nullptr;
    D3D12_RESOURCE_DESC desc{};
    Tracked t;
    ID3D12Heap* heap = nullptr;
    std::vector<std::pair<DXGI_FORMAT, Tracked>> aliases;
    uint64_t bytes = 0;
};

std::mutex g_spareImageLock;
std::deque<SpareImage> g_spareImages;
uint64_t g_spareImageBytes = 0;

void DropSpareImage(SpareImage& s) {
    for (auto& a : s.aliases) if (a.second.res) a.second.res->Release();
    if (s.t.res) s.t.res->Release();
    if (s.heap) s.heap->Release();
    g_spareImageBytes -= s.bytes;
}

bool TakeSpareImage(XImage* img) {
    std::lock_guard<std::mutex> guard(g_spareImageLock);
    for (auto it = g_spareImages.begin(); it != g_spareImages.end(); ++it) {
        if (it->d3d != img->d3d || std::memcmp(&it->desc, &img->desc, sizeof img->desc) != 0) continue;
        img->t = it->t;
        img->heap = it->heap;
        img->aliases = std::move(it->aliases);
        g_spareImageBytes -= it->bytes;
        g_spareImages.erase(it);
        return true;
    }
    return false;
}

void GiveSpareImage(XImage* img) {
    if (std::getenv("XVK_NO_IMAGE_REUSE") || !img->d3d || !img->t.res) {
        for (auto& a : img->aliases) if (a.second.res) a.second.res->Release();
        if (img->t.res) img->t.res->Release();
        if (img->heap) img->heap->Release();
        return;
    }
    SpareImage s;
    s.d3d = img->d3d;
    s.desc = img->desc;
    s.t = img->t;
    s.heap = img->heap;
    s.aliases = std::move(img->aliases);
    s.bytes = img->d3d->GetResourceAllocationInfo(0, 1, &img->desc).SizeInBytes;
    std::lock_guard<std::mutex> guard(g_spareImageLock);
    g_spareImageBytes += s.bytes;
    g_spareImages.push_back(std::move(s));
    while (!g_spareImages.empty() && (g_spareImages.size() > 128 || g_spareImageBytes > (768ull << 20))) {
        DropSpareImage(g_spareImages.front());
        g_spareImages.pop_front();
    }
}

void DropSpareImages(ID3D12Device* d3d) {
    std::lock_guard<std::mutex> guard(g_spareImageLock);
    for (auto it = g_spareImages.begin(); it != g_spareImages.end();) {
        if (it->d3d == d3d) { DropSpareImage(*it); it = g_spareImages.erase(it); } else ++it;
    }
}

struct XImageView { XImage* image = nullptr; VkFormat format = VK_FORMAT_UNDEFINED; VkImageSubresourceRange range{}; };

struct XCommandBuffer {
    XDevice* device = nullptr;
    ID3D12CommandAllocator* allocator = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    std::vector<ID3D12Resource*> staging;
    bool open = false;
    uint64_t epoch = 1, stateEpoch = 0;
    struct XState* state = nullptr;
};

struct XCommandPool { std::vector<XCommandBuffer*> buffers; };
struct XSemaphore { ID3D12Fence* fence = nullptr; uint64_t value = 0; bool timeline = false; uint64_t queued = 0; };
struct XQueuedWait { XSemaphore* sem = nullptr; uint64_t value = 0; uint64_t submit = 0; };
static XQueuedWait g_waits[16];
static uint64_t g_submits = 0;
static std::atomic<unsigned long long> g_stallLog{0};
struct XFence { ID3D12Fence* fence = nullptr; uint64_t target = 0; bool signaled = false; };
struct XSurface { HWND hwnd = nullptr; };
struct XSwapchain { XDevice* device = nullptr; IDXGISwapChain3* chain = nullptr; std::vector<XImage*> images; VkFormat format = VK_FORMAT_B8G8R8A8_UNORM; };

struct FormatInfo { VkFormat vk; DXGI_FORMAT dx; uint32_t bytes; bool depth; bool stencil; uint32_t block; };
const FormatInfo kFormats[] = {
    {VK_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM, 4, false, false, 1},
    {VK_FORMAT_R8G8B8A8_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, 4, false, false, 1},
    {VK_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_UINT, 4, false, false, 1},
    {VK_FORMAT_R8G8B8A8_SNORM, DXGI_FORMAT_R8G8B8A8_SNORM, 4, false, false, 1},
    {VK_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, 4, false, false, 1},
    {VK_FORMAT_B8G8R8A8_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, 4, false, false, 1},
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, DXGI_FORMAT_R10G10B10A2_UNORM, 4, false, false, 1},
    {VK_FORMAT_B10G11R11_UFLOAT_PACK32, DXGI_FORMAT_R11G11B10_FLOAT, 4, false, false, 1},
    {VK_FORMAT_R16G16B16A16_SFLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT, 8, false, false, 1},
    {VK_FORMAT_R16G16B16A16_UNORM, DXGI_FORMAT_R16G16B16A16_UNORM, 8, false, false, 1},
    {VK_FORMAT_R32G32B32A32_SFLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, 16, false, false, 1},
    {VK_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_UINT, 16, false, false, 1},
    {VK_FORMAT_R32G32_SFLOAT, DXGI_FORMAT_R32G32_FLOAT, 8, false, false, 1},
    {VK_FORMAT_R32G32_UINT, DXGI_FORMAT_R32G32_UINT, 8, false, false, 1},
    {VK_FORMAT_R16G16_SFLOAT, DXGI_FORMAT_R16G16_FLOAT, 4, false, false, 1},
    {VK_FORMAT_R16G16_UNORM, DXGI_FORMAT_R16G16_UNORM, 4, false, false, 1},
    {VK_FORMAT_R32_SFLOAT, DXGI_FORMAT_R32_FLOAT, 4, false, false, 1},
    {VK_FORMAT_R32_UINT, DXGI_FORMAT_R32_UINT, 4, false, false, 1},
    {VK_FORMAT_R16_SFLOAT, DXGI_FORMAT_R16_FLOAT, 2, false, false, 1},
    {VK_FORMAT_R16_UNORM, DXGI_FORMAT_R16_UNORM, 2, false, false, 1},
    {VK_FORMAT_R16_UINT, DXGI_FORMAT_R16_UINT, 2, false, false, 1},
    {VK_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM, 2, false, false, 1},
    {VK_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM, 1, false, false, 1},
    {VK_FORMAT_R8_UINT, DXGI_FORMAT_R8_UINT, 1, false, false, 1},
    {VK_FORMAT_D32_SFLOAT, DXGI_FORMAT_D32_FLOAT, 4, true, false, 1},
    {VK_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 2, true, false, 1},
    {VK_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D24_UNORM_S8_UINT, 4, true, true, 1},
    {VK_FORMAT_D32_SFLOAT_S8_UINT, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 8, true, true, 1},
    {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, DXGI_FORMAT_BC1_UNORM, 8, false, false, 4},
    {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, DXGI_FORMAT_BC1_UNORM_SRGB, 8, false, false, 4},
    {VK_FORMAT_BC2_UNORM_BLOCK, DXGI_FORMAT_BC2_UNORM, 16, false, false, 4},
    {VK_FORMAT_BC3_UNORM_BLOCK, DXGI_FORMAT_BC3_UNORM, 16, false, false, 4},
    {VK_FORMAT_BC3_SRGB_BLOCK, DXGI_FORMAT_BC3_UNORM_SRGB, 16, false, false, 4},
    {VK_FORMAT_BC4_UNORM_BLOCK, DXGI_FORMAT_BC4_UNORM, 8, false, false, 4},
    {VK_FORMAT_BC5_UNORM_BLOCK, DXGI_FORMAT_BC5_UNORM, 16, false, false, 4},
    {VK_FORMAT_BC6H_UFLOAT_BLOCK, DXGI_FORMAT_BC6H_UF16, 16, false, false, 4},
    {VK_FORMAT_BC7_UNORM_BLOCK, DXGI_FORMAT_BC7_UNORM, 16, false, false, 4},
    {VK_FORMAT_BC7_SRGB_BLOCK, DXGI_FORMAT_BC7_UNORM_SRGB, 16, false, false, 4},
};

const FormatInfo* Format(VkFormat f) {
    for (const auto& i : kFormats) if (i.vk == f) return &i;
    return nullptr;
}

void Use(ID3D12GraphicsCommandList* list, Tracked& t, D3D12_RESOURCE_STATES want) {
    if (t.state == want) return;
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = t.res;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = t.state;
    b.Transition.StateAfter = want;
    list->ResourceBarrier(1, &b);
    t.state = want;
}

bool EnsureWhole(XMemory* m) {
    if (m->whole.res) return true;
    D3D12_HEAP_PROPERTIES heap{};
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = AlignUp(std::max<uint64_t>(m->size, 256), 65536);
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (m->type == 0) {
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    } else {
        static const bool combine = std::getenv("XVK_GUEST_WC") != nullptr;
        heap.Type = D3D12_HEAP_TYPE_CUSTOM;
        heap.CPUPageProperty = combine ? D3D12_CPU_PAGE_PROPERTY_WRITE_COMBINE : D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
        heap.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
        d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    HRESULT hr = m->device->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m->whole.res));
    if (FAILED(hr)) { Log("memory %llu type %u failed %08x", (unsigned long long)m->size, m->type, unsigned(hr)); if (hr == DXGI_ERROR_DEVICE_REMOVED) ReportRemoval(m->device->device); return false; }
    if (m->type == 1) {
        D3D12_RANGE none{0, 0};
        m->whole.res->Map(0, &none, &m->mapped);
    }
    return true;
}

VkResult ImportHost(XMemory* m, void* host) {
    static const bool disabled = std::getenv("XVK_NO_HOST_IMPORT") != nullptr;
    static std::atomic<int> logged{0};
    if (disabled || !host || (uintptr_t(host) & 65535) != 0) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    HRESULT hr13 = E_NOINTERFACE, hr3 = E_NOINTERFACE;
    ID3D12Device13* dev13 = nullptr;
    if (SUCCEEDED(m->device->device->QueryInterface(IID_PPV_ARGS(&dev13)))) {
        hr13 = dev13->OpenExistingHeapFromAddress1(host, AlignUp(m->size, 65536), IID_PPV_ARGS(&m->imported));
        dev13->Release();
    }
    if (FAILED(hr13)) {
        ID3D12Device3* dev3 = nullptr;
        if (SUCCEEDED(m->device->device->QueryInterface(IID_PPV_ARGS(&dev3)))) {
            hr3 = dev3->OpenExistingHeapFromAddress(host, IID_PPV_ARGS(&m->imported));
            dev3->Release();
        }
    }
    if (FAILED(hr13) && FAILED(hr3)) {
        MEMORY_BASIC_INFORMATION mbi{};
        VirtualQuery(host, &mbi, sizeof(mbi));
        if (logged++ < 8) Log("host import %llu KB failed: device13 %08x device3 %08x | type %lx state %lx protect %lx base %s region %llu KB", (unsigned long long)(m->size >> 10), unsigned(hr13), unsigned(hr3),
            mbi.Type, mbi.State, mbi.Protect, mbi.AllocationBase == host ? "same" : "inside", (unsigned long long)(mbi.RegionSize >> 10));
        if (logged <= 8) {
            std::string walk;
            auto* at = static_cast<char*>(host);
            auto* end = at + m->size;
            for (int i = 0; at < end && i < 8; ++i) {
                MEMORY_BASIC_INFORMATION r{};
                if (!VirtualQuery(at, &r, sizeof(r))) break;
                char part[96];
                std::snprintf(part, sizeof part, " [%llu KB st %lx pr %lx %s]", (unsigned long long)(r.RegionSize >> 10), r.State, r.Protect, r.AllocationBase == host ? "base" : "other");
                walk += part;
                at = static_cast<char*>(r.BaseAddress) + r.RegionSize;
            }
            Log("host import walk:%s", walk.c_str());
        }
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    HRESULT hr = S_OK;
    const uint64_t heapSize = m->imported->GetDesc().SizeInBytes;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = std::min<uint64_t>(AlignUp(std::max<uint64_t>(m->size, 256), 65536), heapSize);
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER;
    hr = d.Width < m->size ? E_INVALIDARG : m->device->device->CreatePlacedResource(m->imported, 0, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m->whole.res));
    if (FAILED(hr)) {
        if (logged++ < 8) Log("host import %llu MB heap %llu MB placed failed %08x", (unsigned long long)(m->size >> 20), (unsigned long long)(heapSize >> 20), unsigned(hr));
        Release(m->imported);
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }
    m->mapped = host;
    if (logged++ < 8) Log("host import %llu MB ok", (unsigned long long)(m->size >> 20));
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkGetMemoryHostPointerPropertiesEXT(VkDevice, VkExternalMemoryHandleTypeFlagBits type, const void* host, VkMemoryHostPointerPropertiesEXT* p) {
    if (type != VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT || (uintptr_t(host) & 65535) != 0) return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    p->memoryTypeBits = 2;
    return VK_SUCCESS;
}

struct StagingPool {
    std::mutex lock;
    std::map<uint64_t, std::vector<std::pair<ID3D12Resource*, void*>>> free;
    std::map<ID3D12Resource*, void*> mapped;
    uint64_t freeBytes = 0;
};
StagingPool g_staging;
constexpr uint64_t kStagingPoolMax = 192ull << 20;

ID3D12Resource* Staging(XCommandBuffer* cb, uint64_t size, void** cpu) {
    const uint64_t width = AlignUp(std::max<uint64_t>(size, 256), 65536);
    g_perf.stagingBytes += size;
    {
        std::lock_guard<std::mutex> guard(g_staging.lock);
        auto it = g_staging.free.find(width);
        if (it != g_staging.free.end() && !it->second.empty()) {
            auto entry = it->second.back();
            it->second.pop_back();
            g_staging.freeBytes -= width;
            cb->staging.push_back(entry.first);
            if (cpu) *cpu = entry.second;
            ++g_perf.stagingReused;
            return entry.first;
        }
    }
    ++g_perf.stagingNew;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_CUSTOM;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = AlignUp(std::max<uint64_t>(size, 256), 65536);
    d.Height = d.DepthOrArraySize = d.MipLevels = 1;
    d.SampleDesc.Count = 1;
    d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* r = nullptr;
    if (FAILED(cb->device->device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r)))) return nullptr;
    cb->staging.push_back(r);
    void* p = nullptr;
    D3D12_RANGE none{0, 0};
    r->Map(0, &none, &p);
    {
        std::lock_guard<std::mutex> guard(g_staging.lock);
        g_staging.mapped[r] = p;
    }
    if (cpu) *cpu = p;
    return r;
}

void ClearStaging(XCommandBuffer* cb) {
    std::lock_guard<std::mutex> guard(g_staging.lock);
    for (auto* r : cb->staging) {
        const uint64_t width = r->GetDesc().Width;
        if (g_staging.freeBytes + width <= kStagingPoolMax) {
            g_staging.free[width].push_back({r, g_staging.mapped[r]});
            g_staging.freeBytes += width;
        } else {
            g_staging.mapped.erase(r);
            r->Release();
        }
    }
    cb->staging.clear();
}

UINT Subresource(const XImage* img, uint32_t mip, uint32_t layer, uint32_t plane) { return mip + layer * img->mips + plane * img->mips * img->layers; }

D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint(const XImage* img, uint32_t mip, uint64_t offset, uint32_t w, uint32_t h, uint32_t d, uint32_t pitch) {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT f{};
    f.Offset = offset;
    f.Footprint.Format = img->desc.Format;
    f.Footprint.Width = w;
    f.Footprint.Height = h;
    f.Footprint.Depth = d;
    f.Footprint.RowPitch = pitch;
    return f;
}

void CopyBufferImage(XCommandBuffer* cb, XBuffer* buf, XImage* img, const VkBufferImageCopy& r, bool toImage) {
    const FormatInfo* fi = Format(img->format);
    if (!fi || !EnsureWhole(buf->memory)) return;
    auto* list = cb->list;
    const uint32_t block = fi->block;
    const uint32_t w = std::max(1u, r.imageExtent.width), h = std::max(1u, r.imageExtent.height), d = std::max(1u, r.imageExtent.depth);
    const uint32_t rowLength = r.bufferRowLength ? r.bufferRowLength : w;
    const uint32_t imageHeight = r.bufferImageHeight ? r.bufferImageHeight : h;
    const uint32_t rows = (h + block - 1) / block, slicesRows = (imageHeight + block - 1) / block;
    const uint32_t tight = (rowLength + block - 1) / block * fi->bytes;
    const uint32_t widthBytes = (w + block - 1) / block * fi->bytes;
    const uint32_t pitch = uint32_t(AlignUp(widthBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
    const uint32_t plane = (r.imageSubresource.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) ? 1u : 0u;
    Tracked& mem = buf->memory->whole;
    static const bool directEnabled = std::getenv("XVK_STAGED_COPY") == nullptr;
    const bool direct = directEnabled && tight % D3D12_TEXTURE_DATA_PITCH_ALIGNMENT == 0 && AlignUp(w, block) == w && AlignUp(h, block) == h && !plane;
    for (uint32_t layer = 0; layer < r.imageSubresource.layerCount; ++layer) {
        const uint64_t base = buf->offset + r.bufferOffset + uint64_t(layer) * tight * slicesRows * d;
        if (direct && base % D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT == 0) {
            ++g_perf.directCopies;
            D3D12_TEXTURE_COPY_LOCATION tex{img->t.res, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            tex.SubresourceIndex = Subresource(img, r.imageSubresource.mipLevel, r.imageSubresource.baseArrayLayer + layer, plane);
            D3D12_TEXTURE_COPY_LOCATION bl{mem.res, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
            bl.PlacedFootprint = Footprint(img, 0, base, uint32_t(AlignUp(rowLength, block)), slicesRows * block, d, tight);
            if (toImage) {
                Use(list, mem, D3D12_RESOURCE_STATE_COPY_SOURCE);
                Use(list, img->t, D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_BOX box{0, 0, 0, w, h, d};
                list->CopyTextureRegion(&tex, UINT(r.imageOffset.x), UINT(r.imageOffset.y), UINT(r.imageOffset.z), &bl, &box);
            } else {
                Use(list, mem, D3D12_RESOURCE_STATE_COPY_DEST);
                Use(list, img->t, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_BOX box{UINT(r.imageOffset.x), UINT(r.imageOffset.y), UINT(r.imageOffset.z), UINT(r.imageOffset.x) + w, UINT(r.imageOffset.y) + h, UINT(r.imageOffset.z) + d};
                list->CopyTextureRegion(&bl, 0, 0, 0, &tex, &box);
            }
            continue;
        }
        ++g_perf.stagedCopies;
        g_perf.bytesImage += uint64_t(pitch) * rows * d;
        g_perf.copyRows += uint64_t(rows) * d;
        ID3D12Resource* stage = Staging(cb, uint64_t(pitch) * rows * d, nullptr);
        if (!stage) return;
        Tracked stt{stage, D3D12_RESOURCE_STATE_COMMON};
        Use(list, stt, D3D12_RESOURCE_STATE_COPY_DEST);
        Use(list, mem, toImage ? D3D12_RESOURCE_STATE_COPY_SOURCE : D3D12_RESOURCE_STATE_COPY_DEST);
        if (!toImage) Use(list, img->t, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION tex{img->t.res, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        tex.SubresourceIndex = Subresource(img, r.imageSubresource.mipLevel, r.imageSubresource.baseArrayLayer + layer, plane);
        D3D12_TEXTURE_COPY_LOCATION st{stage, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        st.PlacedFootprint = Footprint(img, 0, 0, AlignUp(w, block) == w ? w : uint32_t(AlignUp(w, block)), uint32_t(AlignUp(h, block)), d, pitch);
        if (toImage) {
            for (uint32_t z = 0; z < d; ++z)
                for (uint32_t y = 0; y < rows; ++y)
                    list->CopyBufferRegion(stage, uint64_t(pitch) * (z * rows + y), mem.res, base + uint64_t(tight) * (z * slicesRows + y), widthBytes);
            Use(list, img->t, D3D12_RESOURCE_STATE_COPY_DEST);
            Use(list, stt, D3D12_RESOURCE_STATE_COPY_SOURCE);
            list->CopyTextureRegion(&tex, UINT(r.imageOffset.x), UINT(r.imageOffset.y), UINT(r.imageOffset.z), &st, nullptr);
        } else {
            D3D12_BOX box{UINT(r.imageOffset.x), UINT(r.imageOffset.y), UINT(r.imageOffset.z), UINT(r.imageOffset.x) + st.PlacedFootprint.Footprint.Width, UINT(r.imageOffset.y) + st.PlacedFootprint.Footprint.Height, UINT(r.imageOffset.z) + d};
            list->CopyTextureRegion(&st, 0, 0, 0, &tex, &box);
            Use(list, stt, D3D12_RESOURCE_STATE_COPY_SOURCE);
            for (uint32_t z = 0; z < d; ++z)
                for (uint32_t y = 0; y < rows; ++y)
                    list->CopyBufferRegion(mem.res, base + uint64_t(tight) * (z * slicesRows + y), stage, uint64_t(pitch) * (z * rows + y), widthBytes);
        }
    }
}

const char* kBlitHlsl =
    "Texture2D<float4> src : register(t0);\n"
    "SamplerState pointS : register(s0);\n"
    "SamplerState linearS : register(s1);\n"
    "cbuffer C : register(b0) { float4 uv; float filter; float layer; };\n"
    "struct V { float4 p : SV_Position; float2 t : TEXCOORD0; };\n"
    "V vs(uint id : SV_VertexID) { float2 q = float2((id << 1) & 2, id & 2); V o; o.p = float4(q * float2(2, -2) + float2(-1, 1), 0, 1); o.t = lerp(uv.xy, uv.zw, q); return o; }\n"
    "float4 ps(V i) : SV_Target { return filter > 0.5 ? src.SampleLevel(linearS, i.t, 0) : src.SampleLevel(pointS, i.t, 0); }\n";

bool BlitInit(XDevice* dev) {
    if (dev->blitRoot) return true;
    D3D12_DESCRIPTOR_RANGE range{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, 0};
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable = {1, &range};
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[1].Constants = {0, 0, 6};
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (int i = 0; i < 2; ++i) {
        samplers[i].Filter = i ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = UINT(i);
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC rs{2, params, 2, samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err))) return false;
    HRESULT hr = dev->device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&dev->blitRoot));
    blob->Release();
    return SUCCEEDED(hr);
}

ID3D12PipelineState* BlitPso(XDevice* dev, DXGI_FORMAT rt) {
    auto it = dev->blitPso.find(int(rt));
    if (it != dev->blitPso.end()) return it->second;
    ID3DBlob *vs = nullptr, *ps = nullptr, *err = nullptr;
    if (FAILED(D3DCompile(kBlitHlsl, std::strlen(kBlitHlsl), "blit", nullptr, nullptr, "vs", "vs_5_1", 0, 0, &vs, &err))) return nullptr;
    if (FAILED(D3DCompile(kBlitHlsl, std::strlen(kBlitHlsl), "blit", nullptr, nullptr, "ps", "ps_5_1", 0, 0, &ps, &err))) { vs->Release(); return nullptr; }
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = dev->blitRoot;
    d.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    d.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    d.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    d.SampleMask = UINT_MAX;
    d.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    d.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    d.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    d.NumRenderTargets = 1;
    d.RTVFormats[0] = rt;
    d.SampleDesc.Count = 1;
    ID3D12PipelineState* pso = nullptr;
    dev->device->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(&pso));
    vs->Release();
    ps->Release();
    dev->blitPso[int(rt)] = pso;
    return pso;
}

VkResult Fill(VkResult r) { return r; }

std::map<std::string, PFN_vkVoidFunction>& Table();

}

XVK VkResult VKAPI_CALL vkEnumerateInstanceVersion(uint32_t* v) { *v = VK_API_VERSION_1_3; return VK_SUCCESS; }

XVK VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties(uint32_t* n, VkLayerProperties*) { *n = 0; return VK_SUCCESS; }

const char* kInstanceExt[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME};
const char* kDeviceExt[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, VK_KHR_FRAGMENT_SHADER_BARYCENTRIC_EXTENSION_NAME, VK_EXT_MESH_SHADER_EXTENSION_NAME, VK_KHR_SPIRV_1_4_EXTENSION_NAME, VK_EXT_DEPTH_CLIP_CONTROL_EXTENSION_NAME, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME};

template <size_t N> VkResult ListExtensions(const char* const (&names)[N], uint32_t* n, VkExtensionProperties* out) {
    if (!out) { *n = N; return VK_SUCCESS; }
    uint32_t c = std::min<uint32_t>(*n, N);
    for (uint32_t i = 0; i < c; ++i) { std::memset(&out[i], 0, sizeof out[i]); std::strcpy(out[i].extensionName, names[i]); out[i].specVersion = 1; }
    *n = c;
    return c < N ? VK_INCOMPLETE : VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char*, uint32_t* n, VkExtensionProperties* p) { return ListExtensions(kInstanceExt, n, p); }

XVK VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo* info, const VkAllocationCallbacks*, VkInstance* out) {
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        bool known = false;
        for (auto* e : kInstanceExt) known |= std::strcmp(e, info->ppEnabledExtensionNames[i]) == 0;
        if (!known) { Log("instance extension missing: %s", info->ppEnabledExtensionNames[i]); return VK_ERROR_EXTENSION_NOT_PRESENT; }
    }
    auto* inst = new XInstance;
    if (std::getenv("XVK_DEBUG")) {
        ID3D12Debug* debug = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) { debug->EnableDebugLayer(); debug->Release(); Log("d3d12 debug layer on"); }
    }
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&inst->factory)))) { delete inst; return VK_ERROR_INITIALIZATION_FAILED; }
    IDXGIAdapter1* a = nullptr;
    for (UINT i = 0; inst->factory->EnumAdapters1(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 d;
        a->GetDesc1(&d);
        if ((d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) || FAILED(D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr))) { a->Release(); continue; }
        auto* p = new XPhysical{inst, a, d};
        ID3D12Device* probe = nullptr;
        D3D12_FEATURE_DATA_D3D12_OPTIONS1 o1{};
        if (SUCCEEDED(D3D12CreateDevice(a, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&probe)))) {
            if (SUCCEEDED(probe->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &o1, sizeof o1))) p->int64 = o1.Int64ShaderOps != FALSE;
            for (const auto& fi : kFormats) {
                D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{fi.dx};
                if (!fi.depth && SUCCEEDED(probe->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof fs))) p->support2[int(fi.vk)] = UINT(fs.Support2);
            }
            probe->Release();
        }
        inst->physicals.push_back(p);
    }
    *out = reinterpret_cast<VkInstance>(inst);
    Log("instance: %zu adapters", inst->physicals.size());
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks*) {
    auto* inst = reinterpret_cast<XInstance*>(instance);
    if (!inst) return;
    for (auto* p : inst->physicals) { p->adapter->Release(); delete p; }
    Release(inst->factory);
    delete inst;
}

XVK VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* n, VkPhysicalDevice* out) {
    auto* inst = reinterpret_cast<XInstance*>(instance);
    const uint32_t total = uint32_t(inst->physicals.size());
    if (!out) { *n = total; return VK_SUCCESS; }
    uint32_t c = std::min(*n, total);
    for (uint32_t i = 0; i < c; ++i) out[i] = reinterpret_cast<VkPhysicalDevice>(inst->physicals[i]);
    *n = c;
    return c < total ? VK_INCOMPLETE : VK_SUCCESS;
}

XVK void VKAPI_CALL vkGetPhysicalDeviceProperties(VkPhysicalDevice pd, VkPhysicalDeviceProperties* p) {
    auto* phys = reinterpret_cast<XPhysical*>(pd);
    std::memset(p, 0, sizeof *p);
    p->apiVersion = VK_API_VERSION_1_3;
    p->driverVersion = 1;
    p->vendorID = phys->desc.VendorId;
    p->deviceID = phys->desc.DeviceId;
    p->deviceType = phys->desc.DedicatedVideoMemory ? VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU : VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
    WideCharToMultiByte(CP_UTF8, 0, phys->desc.Description, -1, p->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 8, nullptr, nullptr);
    std::strcat(p->deviceName, " (xvk)");
    auto& l = p->limits;
    l.maxImageDimension1D = l.maxImageDimension2D = 16384;
    l.maxImageDimension3D = 2048;
    l.maxImageDimensionCube = 16384;
    l.maxImageArrayLayers = 2048;
    l.maxTexelBufferElements = 1u << 27;
    l.maxUniformBufferRange = 65536;
    l.maxStorageBufferRange = 1u << 31;
    l.maxPushConstantsSize = 128;
    l.maxTessellationPatchSize = 32;
    l.maxTessellationGenerationLevel = 64;
    l.maxTessellationControlPerVertexInputComponents = 128;
    l.maxTessellationControlPerVertexOutputComponents = 128;
    l.maxTessellationControlPerPatchOutputComponents = 120;
    l.maxTessellationControlTotalOutputComponents = 4096;
    l.maxTessellationEvaluationInputComponents = 128;
    l.maxTessellationEvaluationOutputComponents = 128;
    l.maxGeometryShaderInvocations = 32;
    l.maxGeometryInputComponents = 128;
    l.maxGeometryOutputComponents = 128;
    l.maxGeometryOutputVertices = 1024;
    l.maxGeometryTotalOutputComponents = 1024;
    l.maxMemoryAllocationCount = 1u << 20;
    l.maxSamplerAllocationCount = 2048;
    l.bufferImageGranularity = 65536;
    l.maxBoundDescriptorSets = 8;
    l.maxPerStageDescriptorSamplers = 16;
    l.maxPerStageDescriptorUniformBuffers = 14;
    l.maxPerStageDescriptorStorageBuffers = 64;
    l.maxPerStageDescriptorSampledImages = 128;
    l.maxPerStageDescriptorStorageImages = 64;
    l.maxPerStageResources = 512;
    l.maxDescriptorSetSamplers = 2048;
    l.maxDescriptorSetUniformBuffers = 14 * 6;
    l.maxDescriptorSetStorageBuffers = 1024;
    l.maxDescriptorSetSampledImages = 4096;
    l.maxDescriptorSetStorageImages = 1024;
    l.maxVertexInputAttributes = 32;
    l.maxVertexInputBindings = 32;
    l.maxVertexInputAttributeOffset = 2047;
    l.maxVertexInputBindingStride = 2048;
    l.maxVertexOutputComponents = 128;
    l.maxFragmentInputComponents = 128;
    l.maxFragmentOutputAttachments = 8;
    l.maxComputeSharedMemorySize = 32768;
    l.maxComputeWorkGroupCount[0] = l.maxComputeWorkGroupCount[1] = l.maxComputeWorkGroupCount[2] = 65535;
    l.maxComputeWorkGroupInvocations = 1024;
    l.maxComputeWorkGroupSize[0] = l.maxComputeWorkGroupSize[1] = 1024;
    l.maxComputeWorkGroupSize[2] = 64;
    l.maxDrawIndexedIndexValue = UINT32_MAX;
    l.maxDrawIndirectCount = UINT32_MAX;
    l.maxSamplerLodBias = 15.99f;
    l.maxSamplerAnisotropy = 16;
    l.maxViewports = 16;
    l.maxViewportDimensions[0] = l.maxViewportDimensions[1] = 16384;
    l.viewportBoundsRange[0] = -32768;
    l.viewportBoundsRange[1] = 32767;
    l.minMemoryMapAlignment = 64;
    l.minTexelBufferOffsetAlignment = 16;
    l.minUniformBufferOffsetAlignment = 256;
    l.minStorageBufferOffsetAlignment = 16;
    l.maxFramebufferWidth = l.maxFramebufferHeight = 16384;
    l.maxFramebufferLayers = 2048;
    l.framebufferColorSampleCounts = l.framebufferDepthSampleCounts = l.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    l.framebufferNoAttachmentsSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l.maxColorAttachments = 8;
    l.sampledImageColorSampleCounts = l.sampledImageIntegerSampleCounts = l.sampledImageDepthSampleCounts = l.sampledImageStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    l.storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    l.maxSampleMaskWords = 1;
    l.timestampComputeAndGraphics = VK_TRUE;
    l.timestampPeriod = 1;
    l.maxClipDistances = l.maxCullDistances = 8;
    l.maxCombinedClipAndCullDistances = 8;
    l.pointSizeRange[0] = l.pointSizeRange[1] = 1;
    l.lineWidthRange[0] = l.lineWidthRange[1] = 1;
    l.optimalBufferCopyOffsetAlignment = 512;
    l.optimalBufferCopyRowPitchAlignment = 256;
    l.nonCoherentAtomSize = 64;
    p->sparseProperties = {};
}

XVK void VKAPI_CALL vkGetPhysicalDeviceProperties2(VkPhysicalDevice pd, VkPhysicalDeviceProperties2* p) {
    vkGetPhysicalDeviceProperties(pd, &p->properties);
    for (auto* s = static_cast<VkBaseOutStructure*>(p->pNext); s; s = s->pNext) {
        switch (s->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES: {
            auto* f = reinterpret_cast<VkPhysicalDeviceFloatControlsProperties*>(s);
            f->denormBehaviorIndependence = f->roundingModeIndependence = VK_SHADER_FLOAT_CONTROLS_INDEPENDENCE_NONE;
            f->shaderSignedZeroInfNanPreserveFloat32 = VK_TRUE;
            f->shaderRoundingModeRTEFloat32 = VK_TRUE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
            auto* g = reinterpret_cast<VkPhysicalDeviceSubgroupProperties*>(s);
            g->subgroupSize = 1;
            g->supportedStages = VK_SHADER_STAGE_ALL;
            g->supportedOperations = VK_SUBGROUP_FEATURE_BASIC_BIT;
            g->quadOperationsInAllStages = VK_FALSE;
            break;
        }
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT:
            reinterpret_cast<VkPhysicalDeviceExternalMemoryHostPropertiesEXT*>(s)->minImportedHostPointerAlignment = 65536;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT: {
            auto* m = reinterpret_cast<VkPhysicalDeviceMeshShaderPropertiesEXT*>(s);
            m->maxMeshWorkGroupTotalCount = 1u << 22;
            m->maxMeshWorkGroupCount[0] = m->maxMeshWorkGroupCount[1] = m->maxMeshWorkGroupCount[2] = 65535;
            m->maxMeshWorkGroupInvocations = 256;
            m->maxMeshWorkGroupSize[0] = m->maxMeshWorkGroupSize[1] = m->maxMeshWorkGroupSize[2] = 256;
            m->maxMeshSharedMemorySize = 28672;
            m->maxMeshPayloadAndSharedMemorySize = 28672;
            m->maxMeshOutputMemorySize = 32768;
            m->maxMeshPayloadAndOutputMemorySize = 32768;
            m->maxMeshOutputComponents = 128;
            m->maxMeshOutputVertices = 256;
            m->maxMeshOutputPrimitives = 256;
            m->maxMeshOutputLayers = 1;
            m->maxMeshMultiviewViewCount = 1;
            m->meshOutputPerVertexGranularity = 1;
            m->meshOutputPerPrimitiveGranularity = 1;
            m->maxPreferredMeshWorkGroupInvocations = 64;
            break;
        }
        default: Log("properties2 chain: sType %d left unchanged", int(s->sType)); break;
        }
    }
}

XVK void VKAPI_CALL vkGetPhysicalDeviceFeatures(VkPhysicalDevice pd, VkPhysicalDeviceFeatures* f) {
    std::memset(f, 0, sizeof *f);
    f->shaderInt64 = std::getenv("XVK_INT64") ? VK_TRUE : VK_FALSE;
    f->geometryShader = VK_TRUE;
    f->tessellationShader = VK_TRUE;
    f->textureCompressionBC = VK_TRUE;
    f->samplerAnisotropy = VK_TRUE;
    f->fullDrawIndexUint32 = VK_TRUE;
    f->vertexPipelineStoresAndAtomics = VK_TRUE;
    f->fragmentStoresAndAtomics = VK_TRUE;
    f->imageCubeArray = VK_TRUE;
    f->independentBlend = VK_TRUE;
    f->depthClamp = VK_TRUE;
    f->depthBiasClamp = VK_TRUE;
    f->multiViewport = VK_TRUE;
    f->occlusionQueryPrecise = VK_TRUE;
    f->shaderClipDistance = VK_TRUE;
    f->shaderCullDistance = VK_TRUE;
    f->shaderStorageImageExtendedFormats = VK_TRUE;
}

XVK void VKAPI_CALL vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd, VkPhysicalDeviceFeatures2* f) {
    vkGetPhysicalDeviceFeatures(pd, &f->features);
    for (auto* s = static_cast<VkBaseOutStructure*>(f->pNext); s; s = s->pNext) {
        switch (s->sType) {
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES:
            reinterpret_cast<VkPhysicalDeviceBufferDeviceAddressFeatures*>(s)->bufferDeviceAddress = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES:
            reinterpret_cast<VkPhysicalDeviceTimelineSemaphoreFeatures*>(s)->timelineSemaphore = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT:
            reinterpret_cast<VkPhysicalDeviceMeshShaderFeaturesEXT*>(s)->meshShader = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADER_BARYCENTRIC_FEATURES_KHR:
            reinterpret_cast<VkPhysicalDeviceFragmentShaderBarycentricFeaturesKHR*>(s)->fragmentShaderBarycentric = VK_TRUE;
            break;
        case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_CONTROL_FEATURES_EXT:
            reinterpret_cast<VkPhysicalDeviceDepthClipControlFeaturesEXT*>(s)->depthClipControl = VK_TRUE;
            break;
        default: Log("features2 chain: sType %d left at zero", int(s->sType)); break;
        }
    }
}

void WriteBdaView(XDevice* dev, UINT slot, ID3D12Resource* res, uint64_t size) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};
    uv.Format = DXGI_FORMAT_R32_TYPELESS;
    uv.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uv.Buffer.NumElements = UINT(res ? std::max<uint64_t>(size / 4, 1) : 0);
    uv.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    dev->device->CreateUnorderedAccessView(res, nullptr, &uv, dev->srv.Cpu(dev->bdaBase + slot));
    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R32_TYPELESS;
    sv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Buffer.NumElements = uv.Buffer.NumElements;
    sv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    dev->device->CreateShaderResourceView(res, &sv, dev->srv.Cpu(dev->bdaBase + dev->bdaSlots + slot));
}

XVK VkDeviceAddress VKAPI_CALL vkGetBufferDeviceAddressKHR(VkDevice, const VkBufferDeviceAddressInfo* info) {
    auto* b = Obj<XBuffer>(uint64_t(info->buffer));
    if (!b->memory || !EnsureWhole(b->memory)) return 0;
    XMemory* m = b->memory;
    XDevice* dev = m->device;
    std::lock_guard<std::mutex> g(dev->lock);
    if (m->bda < 0) {
        UINT slot;
        if (!dev->bdaFree.empty()) { slot = dev->bdaFree.back(); dev->bdaFree.pop_back(); }
        else if (dev->bdaNext < dev->bdaSlots) slot = dev->bdaNext++;
        else { Log("device address table full (%u allocations)", dev->bdaSlots); return 0; }
        m->bda = int(slot);
        WriteBdaView(dev, slot, m->whole.res, m->size);
    }
    return (uint64_t(m->bda + 1) << 32) | b->offset;
}

XVK void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice, uint32_t* n, VkQueueFamilyProperties* p) {
    if (!p) { *n = 1; return; }
    *n = 1;
    p[0] = {};
    p[0].queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
    p[0].queueCount = 1;
    p[0].timestampValidBits = 64;
    p[0].minImageTransferGranularity = {1, 1, 1};
}

XVK void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties* m) {
    auto* phys = reinterpret_cast<XPhysical*>(pd);
    std::memset(m, 0, sizeof *m);
    m->memoryTypeCount = 2;
    m->memoryTypes[0] = {VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0};
    m->memoryTypes[1] = {VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, 1};
    m->memoryHeapCount = 2;
    const uint64_t local = phys->desc.DedicatedVideoMemory ? phys->desc.DedicatedVideoMemory : phys->desc.SharedSystemMemory / 2;
    m->memoryHeaps[0] = {local, VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
    m->memoryHeaps[1] = {phys->desc.SharedSystemMemory, 0};
}

XVK void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2* m) { vkGetPhysicalDeviceMemoryProperties(pd, &m->memoryProperties); }

bool IntegerFormat(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8_UINT: case DXGI_FORMAT_R8_SINT: case DXGI_FORMAT_R8G8_UINT: case DXGI_FORMAT_R8G8_SINT: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SINT:
    case DXGI_FORMAT_R16_UINT: case DXGI_FORMAT_R16_SINT: case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R16G16_SINT: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SINT:
    case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: case DXGI_FORMAT_R32G32B32_UINT: case DXGI_FORMAT_R32G32B32_SINT:
    case DXGI_FORMAT_R32G32B32A32_UINT: case DXGI_FORMAT_R32G32B32A32_SINT: case DXGI_FORMAT_R10G10B10A2_UINT: return true;
    default: return false;
    }
}

namespace {
DXGI_FORMAT VertexFormat(VkFormat f);
}

bool VertexInputFormat(VkFormat format) {
    switch (VertexFormat(format)) {
    case DXGI_FORMAT_UNKNOWN: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_D32_FLOAT: case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return false;
    default: break;
    }
    const FormatInfo* fi = Format(format);
    return !fi || fi->block == 1;
}

XVK void VKAPI_CALL vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice pd, VkFormat format, VkFormatProperties* p) {
    *p = {};
    if (VertexInputFormat(format)) p->bufferFeatures = VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
    const FormatInfo* fi = Format(format);
    if (!fi) return;
    VkFormatFeatureFlags f = VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_BLIT_SRC_BIT;
    if (fi->depth) f |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    else if (fi->block == 1) f |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (!fi->depth && fi->block == 1 && !IntegerFormat(fi->dx)) f |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    else f |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const auto& support2 = reinterpret_cast<XPhysical*>(pd)->support2;
    const auto it = support2.find(int(format));
    const UINT storage = D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD | D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE;
    if (std::getenv("XVK_STORAGE_IMAGE") && it != support2.end() && (it->second & storage) == storage) f |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    p->optimalTilingFeatures = f;
}

bool IsDepthFormat(VkFormat f) {
    return f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_X8_D24_UNORM_PACK32 || f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_S8_UINT || f == VK_FORMAT_D16_UNORM_S8_UINT || f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

XVK VkResult VKAPI_CALL vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice, VkFormat format, VkImageType type, VkImageTiling tiling, VkImageUsageFlags, VkImageCreateFlags, VkImageFormatProperties* p) {
    if (!Format(format) || tiling != VK_IMAGE_TILING_OPTIMAL) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    if (type == VK_IMAGE_TYPE_3D) {
        if (IsDepthFormat(format)) return VK_ERROR_FORMAT_NOT_SUPPORTED;
        *p = {{2048, 2048, 2048}, 12, 1, VK_SAMPLE_COUNT_1_BIT, 1ull << 40};
    } else if (type == VK_IMAGE_TYPE_1D) {
        *p = {{16384, 1, 1}, 15, 2048, VK_SAMPLE_COUNT_1_BIT, 1ull << 40};
    } else {
        *p = {{16384, 16384, 1}, 15, 2048, VK_SAMPLE_COUNT_1_BIT, 1ull << 40};
    }
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice, const char*, uint32_t* n, VkExtensionProperties* p) { return ListExtensions(kDeviceExt, n, p); }

XVK VkResult VKAPI_CALL vkEnumerateDeviceLayerProperties(VkPhysicalDevice, uint32_t* n, VkLayerProperties*) { *n = 0; return VK_SUCCESS; }

void WaitIdle(XQueue& q);

void HostProbe(XDevice* dev) {
    const SIZE_T chunk = 2u << 20;
    unsigned char* a = static_cast<unsigned char*>(VirtualAlloc(nullptr, chunk, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    unsigned char* b = static_cast<unsigned char*>(VirtualAlloc(nullptr, chunk, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!a || !b) { Log("hostprobe: VirtualAlloc failed"); return; }
    std::memset(a, 0x11, chunk);
    std::memset(b, 0x22, chunk);
    ID3D12Device3* dev3 = nullptr;
    if (FAILED(dev->device->QueryInterface(IID_PPV_ARGS(&dev3)))) { Log("hostprobe: no ID3D12Device3"); return; }
    ID3D12Heap* ha = nullptr;
    ID3D12Heap* hb = nullptr;
    const HRESULT hra = dev3->OpenExistingHeapFromAddress(a, IID_PPV_ARGS(&ha));
    const HRESULT hrb = dev3->OpenExistingHeapFromAddress(b, IID_PPV_ARGS(&hb));
    ID3D12Heap* hsub = nullptr;
    const HRESULT hrs = dev3->OpenExistingHeapFromAddress(a + (64u << 10), IID_PPV_ARGS(&hsub));
    HANDLE section = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, DWORD(chunk), nullptr);
    ID3D12Heap* hfile = nullptr;
    const HRESULT hrf = section ? dev3->OpenExistingHeapFromFileMapping(section, IID_PPV_ARGS(&hfile)) : E_HANDLE;
    Log("hostprobe: section %d, open file mapping %08x", section != nullptr, unsigned(hrf));
    if (hfile) {
        const D3D12_HEAP_DESC fd = hfile->GetDesc();
        Log("hostprobe: file heap size %llu flags %08x cpu %d pool %d", static_cast<unsigned long long>(fd.SizeInBytes), unsigned(fd.Flags), int(fd.Properties.CPUPageProperty), int(fd.Properties.MemoryPoolPreference));
        hfile->Release();
    }
    if (section) CloseHandle(section);
    dev3->Release();
    if (hsub) hsub->Release();
    Log("hostprobe: open whole 2MB a %08x b %08x, open at +64K %08x", unsigned(hra), unsigned(hrb), unsigned(hrs));
    if (ha) {
        const D3D12_HEAP_DESC hd = ha->GetDesc();
        Log("hostprobe: heap size %llu flags %08x type %d cpu %d pool %d", static_cast<unsigned long long>(hd.SizeInBytes), unsigned(hd.Flags), int(hd.Properties.Type), int(hd.Properties.CPUPageProperty), int(hd.Properties.MemoryPoolPreference));
    }
    if (!ha || !hb) { if (ha) ha->Release(); if (hb) hb->Release(); return; }
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = 2 * chunk;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* reserved = nullptr;
    const HRESULT hrr = dev->device->CreateReservedResource(&rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&reserved));
    ID3D12Resource* placed = nullptr;
    D3D12_RESOURCE_DESC pd = rd;
    pd.Width = chunk;
    const HRESULT hrp = dev->device->CreatePlacedResource(ha, 0, &pd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&placed));
    if (placed) placed->Release();
    Log("hostprobe: reserved 4MB buffer %08x, placed 2MB buffer in opened heap %08x", unsigned(hrr), unsigned(hrp));
    if (FAILED(hrr)) { ha->Release(); hb->Release(); return; }
    const UINT tiles = UINT(chunk / D3D12_TILED_RESOURCE_TILE_SIZE_IN_BYTES);
    ID3D12Heap* heaps[2] = {ha, hb};
    for (UINT i = 0; i < 2; ++i) {
        D3D12_TILED_RESOURCE_COORDINATE c{i * tiles, 0, 0, 0};
        D3D12_TILE_REGION_SIZE s{tiles, FALSE, 0, 0, 0};
        D3D12_TILE_RANGE_FLAGS f = D3D12_TILE_RANGE_FLAG_NONE;
        UINT start = 0;
        dev->queue.queue->UpdateTileMappings(reserved, 1, &c, &s, heaps[i], 1, &f, &start, &tiles, D3D12_TILE_MAPPING_FLAG_NONE);
    }
    D3D12_HEAP_PROPERTIES rb{D3D12_HEAP_TYPE_READBACK};
    D3D12_HEAP_PROPERTIES up{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC sd = rd;
    sd.Width = 65536;
    ID3D12Resource* readback = nullptr;
    ID3D12Resource* upload = nullptr;
    dev->device->CreateCommittedResource(&rb, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback));
    dev->device->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &sd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    dev->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    if (alloc) dev->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list));
    if (!readback || !upload || !list) {
        Log("hostprobe: helper resources failed");
    } else {
        void* p = nullptr;
        upload->Map(0, nullptr, &p);
        std::memset(p, 0x33, 64);
        upload->Unmap(0, nullptr);
        list->CopyBufferRegion(readback, 0, reserved, chunk - 16, 32);
        list->CopyBufferRegion(reserved, chunk + 4096, upload, 0, 64);
        list->Close();
        ID3D12CommandList* lists[] = {list};
        dev->queue.queue->ExecuteCommandLists(1, lists);
        WaitIdle(dev->queue);
        D3D12_RANGE r{0, 32};
        unsigned char* q = nullptr;
        readback->Map(0, &r, reinterpret_cast<void**>(&q));
        int readOk = q ? 1 : 0;
        for (int i = 0; q && i < 32; ++i) readOk &= q[i] == (i < 16 ? 0x11 : 0x22);
        Log("hostprobe: GPU read across the two allocations %s (%02x %02x)", readOk ? "OK" : "WRONG", q ? q[0] : 0, q ? q[31] : 0);
        readback->Unmap(0, nullptr);
        int writeOk = 1;
        for (int i = 0; i < 64; ++i) writeOk &= b[4096 + i] == 0x33;
        Log("hostprobe: GPU write into the second allocation seen by CPU %s (%02x)", writeOk ? "OK" : "WRONG", b[4096]);
    }
    if (list) list->Release();
    if (alloc) alloc->Release();
    if (readback) readback->Release();
    if (upload) upload->Release();
    reserved->Release();
    ha->Release();
    hb->Release();
}

XVK VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice pd, const VkDeviceCreateInfo* info, const VkAllocationCallbacks*, VkDevice* out) {
    auto* phys = reinterpret_cast<XPhysical*>(pd);
    for (uint32_t i = 0; i < info->enabledExtensionCount; ++i) {
        bool known = false;
        for (auto* e : kDeviceExt) known |= std::strcmp(e, info->ppEnabledExtensionNames[i]) == 0;
        if (!known) { Log("device extension missing: %s", info->ppEnabledExtensionNames[i]); return VK_ERROR_EXTENSION_NOT_PRESENT; }
    }
    auto* dev = new XDevice;
    dev->physical = phys;
    if (std::getenv("XVK_DRED")) {
        ID3D12DeviceRemovedExtendedDataSettings* dred = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dred)))) {
            dred->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            dred->Release();
            Log("dred on");
        } else {
            Log("dred settings unavailable");
        }
    }
    if (FAILED(D3D12CreateDevice(phys->adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev->device)))) { delete dev; return VK_ERROR_INITIALIZATION_FAILED; }
    ID3D12InfoQueue1* iq = nullptr;
    if (std::getenv("XVK_DEBUG") && SUCCEEDED(dev->device->QueryInterface(IID_PPV_ARGS(&iq)))) {
        DWORD cookie = 0;
        iq->RegisterMessageCallback([](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY s, D3D12_MESSAGE_ID, LPCSTR text, void*) { if (s <= D3D12_MESSAGE_SEVERITY_WARNING) Log("d3d12: %s", text); }, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
        ID3D12InfoQueue* q0 = nullptr;
        if (SUCCEEDED(iq->QueryInterface(IID_PPV_ARGS(&q0)))) { q0->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE); q0->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE); q0->Release(); }
        Log("d3d12 message callback %s", cookie ? "on" : "off");
    }
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    if (const char* p = std::getenv("XVK_QUEUE_PRIORITY")) qd.Priority = std::strcmp(p, "realtime") == 0 ? D3D12_COMMAND_QUEUE_PRIORITY_GLOBAL_REALTIME : D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
    if (FAILED(dev->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&dev->queue.queue)))) {
        Log("queue priority %d refused, using normal", int(qd.Priority));
        qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        dev->device->CreateCommandQueue(&qd, IID_PPV_ARGS(&dev->queue.queue));
    }
    dev->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&dev->queue.idle));
    dev->queue.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    dev->queue.device = dev;
    {
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        double worst = 0;
        QueryPerformanceCounter(&a);
        for (int i = 0; i < 20; ++i) {
            LARGE_INTEGER s, e;
            QueryPerformanceCounter(&s);
            WaitIdle(dev->queue);
            QueryPerformanceCounter(&e);
            worst = std::max(worst, double(e.QuadPart - s.QuadPart) * 1000.0 / double(freq.QuadPart));
        }
        QueryPerformanceCounter(&b);
        Log("empty signal round trip: avg %.2f ms, worst %.2f ms, queue priority %d", double(b.QuadPart - a.QuadPart) * 1000.0 / double(freq.QuadPart) / 20, worst, int(qd.Priority));
    }
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS12 o12{};
        const HRESULT h12 = dev->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &o12, sizeof o12);
        D3D12_FEATURE_DATA_D3D12_OPTIONS3 o3{};
        const HRESULT h3 = dev->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS3, &o3, sizeof o3);
        ID3D12Device10* d10 = nullptr;
        const bool has10 = SUCCEEDED(dev->device->QueryInterface(IID_PPV_ARGS(&d10)));
        if (d10) d10->Release();
        D3D12_FEATURE_DATA_D3D12_OPTIONS opt{};
        dev->device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &opt, sizeof opt);
        Log("casting: options12 %08x relaxed %d enhanced barriers %d | options3 %08x fully typed casting %d | device10 %d | tiled tier %d", unsigned(h12), SUCCEEDED(h12) ? int(o12.RelaxedFormatCastingSupported) : -1, SUCCEEDED(h12) ? int(o12.EnhancedBarriersSupported) : -1, unsigned(h3), SUCCEEDED(h3) ? int(o3.CastingFullyTypedFormatSupported) : -1, int(has10), int(opt.TiledResourcesTier));
    }
    if (std::getenv("XVK_HOST_PROBE")) HostProbe(dev);
    auto ring =[&](DescriptorRing& r, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT count, bool visible) {
        D3D12_DESCRIPTOR_HEAP_DESC d{type, count, visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE};
        dev->device->CreateDescriptorHeap(&d, IID_PPV_ARGS(&r.heap));
        r.size = dev->device->GetDescriptorHandleIncrementSize(type);
        r.count = count;
    };
    ring(dev->rtv, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 4096, false);
    ring(dev->dsv, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1024, false);
    ring(dev->srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 262144, true);
    ring(dev->smp, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048, true);
    dev->bdaSlots = 16384;
    dev->srv.count -= 2 * dev->bdaSlots;
    dev->bdaBase = dev->srv.count;
    for (UINT i = 0; i < dev->bdaSlots; ++i) WriteBdaView(dev, i, nullptr, 0);
    *out = reinterpret_cast<VkDevice>(dev);
    Log("device created on %ls", phys->desc.Description);
    return VK_SUCCESS;
}

static void StallDump(const char* where, XSemaphore* s, uint64_t want) {
    const unsigned long long now = GetTickCount64();
    unsigned long long last = g_stallLog.load();
    if (now - last < 10000 || !g_stallLog.compare_exchange_strong(last, now)) return;
    if (s) Log("stall: %s %p wants %llu completed %llu queued %llu timeline %d submits %llu", where, static_cast<void*>(s), static_cast<unsigned long long>(want), static_cast<unsigned long long>(s->fence->GetCompletedValue()), static_cast<unsigned long long>(s->queued), int(s->timeline), static_cast<unsigned long long>(g_submits));
    else Log("stall: %s wants %llu submits %llu", where, static_cast<unsigned long long>(want), static_cast<unsigned long long>(g_submits));
    for (const auto& w : g_waits) {
        if (!w.sem) continue;
        const uint64_t done = w.sem->fence->GetCompletedValue();
        Log("stall:   queue wait #%llu sem %p value %llu completed %llu queued %llu%s", static_cast<unsigned long long>(w.submit), static_cast<void*>(w.sem), static_cast<unsigned long long>(w.value), static_cast<unsigned long long>(done), static_cast<unsigned long long>(w.sem->queued), done < w.value ? " BLOCKED" : "");
    }
}

bool SpinFence(ID3D12Fence* fence, uint64_t value) {
    static const int limit = [] { const char* e = std::getenv("XVK_SPIN_US"); return e ? std::atoi(e) : 0; }();
    if (limit <= 0) return fence->GetCompletedValue() >= value;
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);
    for (;;) {
        if (fence->GetCompletedValue() >= value) return true;
        QueryPerformanceCounter(&now);
        if ((now.QuadPart - start.QuadPart) * 1000000 / freq.QuadPart >= limit) return false;
        YieldProcessor();
    }
}

void WaitIdle(XQueue& q) {
    PerfTimer perfTimer(g_perf.waitUs); ++g_perf.waits;
    const uint64_t v = ++q.idleValue;
    q.queue->Signal(q.idle, v);
    if (!SpinFence(q.idle, v)) { q.idle->SetEventOnCompletion(v, q.event); while (WaitForSingleObject(q.event, 10000) == WAIT_TIMEOUT) StallDump("queue idle", nullptr, v); }
}

XVK VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice device) { WaitIdle(reinterpret_cast<XDevice*>(device)->queue); return VK_SUCCESS; }

XVK void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks*) {
    auto* dev = reinterpret_cast<XDevice*>(device);
    if (!dev) return;
    WaitIdle(dev->queue);
    DropSpareImages(dev->device);
    for (auto& [a, l] : dev->spareLists) { Release(l); Release(a); }
    dev->spareLists.clear();
    for (auto& [k, p] : dev->blitPso) if (p) p->Release();
    Release(dev->blitRoot);
    Release(dev->rtv.heap);
    Release(dev->dsv.heap);
    Release(dev->srv.heap);
    Release(dev->queue.idle);
    Release(dev->queue.queue);
    CloseHandle(dev->queue.event);
    Release(dev->device);
    delete dev;
}

XVK void VKAPI_CALL vkGetDeviceQueue(VkDevice device, uint32_t, uint32_t, VkQueue* q) { *q = reinterpret_cast<VkQueue>(&reinterpret_cast<XDevice*>(device)->queue); }

XVK VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue queue) { WaitIdle(*reinterpret_cast<XQueue*>(queue)); return VK_SUCCESS; }

XVK VkResult VKAPI_CALL vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* info, const VkAllocationCallbacks*, VkDeviceMemory* out) {
    if (info->memoryTypeIndex > 1) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    auto* m = new XMemory{reinterpret_cast<XDevice*>(device), info->memoryTypeIndex, info->allocationSize};
    for (auto* s = static_cast<const VkBaseInStructure*>(info->pNext); s; s = s->pNext) {
        if (s->sType != VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT) continue;
        const VkResult r = ImportHost(m, reinterpret_cast<const VkImportMemoryHostPointerInfoEXT*>(s)->pHostPointer);
        if (r != VK_SUCCESS) { delete m; return r; }
    }
    if (m->type == 1 && !EnsureWhole(m)) { delete m; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    *out = VkDeviceMemory(Handle(m));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkFreeMemory(VkDevice, VkDeviceMemory memory, const VkAllocationCallbacks*) {
    auto* m = Obj<XMemory>(uint64_t(memory));
    if (!m) return;
    if (m->bda >= 0) {
        std::lock_guard<std::mutex> g(m->device->lock);
        WriteBdaView(m->device, UINT(m->bda), nullptr, 0);
        m->device->bdaFree.push_back(UINT(m->bda));
    }
    Release(m->whole.res);
    Release(m->imported);
    delete m;
}

XVK VkResult VKAPI_CALL vkMapMemory(VkDevice, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize, VkMemoryMapFlags, void** data) {
    auto* m = Obj<XMemory>(uint64_t(memory));
    if (!m->mapped) return VK_ERROR_MEMORY_MAP_FAILED;
    *data = static_cast<char*>(m->mapped) + offset;
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkUnmapMemory(VkDevice, VkDeviceMemory) {}
XVK VkResult VKAPI_CALL vkFlushMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange*) { return VK_SUCCESS; }
XVK VkResult VKAPI_CALL vkInvalidateMappedMemoryRanges(VkDevice, uint32_t, const VkMappedMemoryRange*) { return VK_SUCCESS; }

XVK VkResult VKAPI_CALL vkCreateBuffer(VkDevice, const VkBufferCreateInfo* info, const VkAllocationCallbacks*, VkBuffer* out) {
    *out = VkBuffer(Handle(new XBuffer{info->size}));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyBuffer(VkDevice, VkBuffer buffer, const VkAllocationCallbacks*) { delete Obj<XBuffer>(uint64_t(buffer)); }

XVK void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice, VkBuffer buffer, VkMemoryRequirements* r) {
    r->size = AlignUp(Obj<XBuffer>(uint64_t(buffer))->size, 256);
    r->alignment = 256;
    r->memoryTypeBits = 3;
}

XVK VkResult VKAPI_CALL vkBindBufferMemory(VkDevice, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize offset) {
    auto* b = Obj<XBuffer>(uint64_t(buffer));
    b->memory = Obj<XMemory>(uint64_t(memory));
    b->offset = offset;
    return EnsureWhole(b->memory) ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

DXGI_FORMAT CastFamily(DXGI_FORMAT f) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_TYPELESS: case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SNORM: case DXGI_FORMAT_R8G8B8A8_SINT: return DXGI_FORMAT_R8G8B8A8_TYPELESS;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS: case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_TYPELESS;
    case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_UINT: case DXGI_FORMAT_R32_SINT: return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_UINT: return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    case DXGI_FORMAT_R16G16_TYPELESS: case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_UNORM: case DXGI_FORMAT_R16G16_UINT: case DXGI_FORMAT_R16G16_SNORM: case DXGI_FORMAT_R16G16_SINT: return DXGI_FORMAT_R16G16_TYPELESS;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_UNORM: case DXGI_FORMAT_R16G16B16A16_UINT: case DXGI_FORMAT_R16G16B16A16_SNORM: case DXGI_FORMAT_R16G16B16A16_SINT: return DXGI_FORMAT_R16G16B16A16_TYPELESS;
    case DXGI_FORMAT_R32G32_TYPELESS: case DXGI_FORMAT_R32G32_FLOAT: case DXGI_FORMAT_R32G32_UINT: case DXGI_FORMAT_R32G32_SINT: return DXGI_FORMAT_R32G32_TYPELESS;
    default: return f;
    }
}

bool AliasViews() {
    static const bool on = std::getenv("XVK_ALIAS_VIEWS") != nullptr;
    return on;
}

Tracked& TrackFor(XImage* img, DXGI_FORMAT view) {
    if (view == DXGI_FORMAT_UNKNOWN || CastFamily(view) == CastFamily(img->desc.Format)) return img->t;
    if (!img->heap) {
        static std::atomic<int> missed{0};
        if (AliasViews() && missed.fetch_add(1) < 8) Log("alias view missing heap: %ux%u base fmt %d view fmt %d owned %d flags %x", unsigned(img->desc.Width), img->desc.Height, int(img->desc.Format), int(view), int(img->owned), unsigned(img->desc.Flags));
        return img->t;
    }
    const DXGI_FORMAT family = CastFamily(view);
    for (auto& a : img->aliases) if (a.first == family) return a.second.res ? a.second : img->t;
    D3D12_RESOURCE_DESC d = img->desc;
    d.Format = family;
    ID3D12Resource* r = nullptr;
    HRESULT hr = img->d3d->CreatePlacedResource(img->heap, 0, &d, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&r));
    static std::atomic<int> logged{0};
    if (logged.fetch_add(1) < 24) Log("alias view %ux%u base fmt %d view fmt %d -> %s %08x", unsigned(img->desc.Width), img->desc.Height, int(img->desc.Format), int(view), SUCCEEDED(hr) ? "placed" : "refused", unsigned(hr));
    img->aliases.emplace_back(family, Tracked{SUCCEEDED(hr) ? r : nullptr, D3D12_RESOURCE_STATE_COMMON});
    return r ? img->aliases.back().second : img->t;
}

XVK VkResult VKAPI_CALL vkCreateImage(VkDevice, const VkImageCreateInfo* info, const VkAllocationCallbacks*, VkImage* out) {
    const FormatInfo* fi = Format(info->format);
    if (!fi) { Log("image format %d unsupported", int(info->format)); return VK_ERROR_FORMAT_NOT_SUPPORTED; }
    auto* img = new XImage;
    img->format = info->format;
    img->extent = info->extent;
    img->mips = info->mipLevels;
    img->layers = info->arrayLayers;
    img->aspect = fi->depth ? (VK_IMAGE_ASPECT_DEPTH_BIT | (fi->stencil ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)) : VK_IMAGE_ASPECT_COLOR_BIT;
    auto& d = img->desc;
    if (info->imageType == VK_IMAGE_TYPE_3D && IsDepthFormat(info->format)) Log("3d depth image requested: format %d extent %ux%ux%u", int(info->format), info->extent.width, info->extent.height, info->extent.depth);
    d.Dimension = info->imageType == VK_IMAGE_TYPE_3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : info->imageType == VK_IMAGE_TYPE_1D ? D3D12_RESOURCE_DIMENSION_TEXTURE1D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = info->extent.width;
    d.Height = info->extent.height;
    d.DepthOrArraySize = UINT16(info->imageType == VK_IMAGE_TYPE_3D ? info->extent.depth : info->arrayLayers);
    d.MipLevels = UINT16(info->mipLevels);
    d.Format = fi->vk == VK_FORMAT_D32_SFLOAT ? DXGI_FORMAT_R32_TYPELESS : fi->vk == VK_FORMAT_D16_UNORM ? DXGI_FORMAT_R16_TYPELESS : fi->vk == VK_FORMAT_D24_UNORM_S8_UINT ? DXGI_FORMAT_R24G8_TYPELESS : fi->dx;
    d.SampleDesc.Count = UINT(info->samples);
    if (fi->depth) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    else if (fi->block == 1) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if ((info->usage & VK_IMAGE_USAGE_STORAGE_BIT) && !fi->depth) d.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    *out = VkImage(Handle(img));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyImage(VkDevice, VkImage image, const VkAllocationCallbacks*) {
    auto* img = Obj<XImage>(uint64_t(image));
    if (!img || !img->owned) return;
    GiveSpareImage(img);
    delete img;
}

XVK void VKAPI_CALL vkGetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* r) {
    auto* img = Obj<XImage>(uint64_t(image));
    auto info = reinterpret_cast<XDevice*>(device)->device->GetResourceAllocationInfo(0, 1, &img->desc);
    r->size = info.SizeInBytes;
    r->alignment = info.Alignment;
    r->memoryTypeBits = 1;
}

XVK VkResult VKAPI_CALL vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory, VkDeviceSize) {
    auto* img = Obj<XImage>(uint64_t(image));
    ID3D12Device* d3d = reinterpret_cast<XDevice*>(device)->device;
    img->d3d = d3d;
    if (TakeSpareImage(img)) {
        img->fresh = (img->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0 && img->heap != nullptr;
        return VK_SUCCESS;
    }
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_DEFAULT};
    if (AliasViews() && img->desc.SampleDesc.Count == 1 && !(img->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL) && CastFamily(img->desc.Format) != img->desc.Format) {
        const D3D12_RESOURCE_ALLOCATION_INFO ai = d3d->GetResourceAllocationInfo(0, 1, &img->desc);
        D3D12_HEAP_DESC hd{};
        hd.SizeInBytes = ai.SizeInBytes;
        hd.Properties = heap;
        hd.Alignment = ai.Alignment;
        hd.Flags = (img->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) ? D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES : D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
        if (SUCCEEDED(d3d->CreateHeap(&hd, IID_PPV_ARGS(&img->heap))) && FAILED(d3d->CreatePlacedResource(img->heap, 0, &img->desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&img->t.res)))) Release(img->heap);
        if (img->t.res) { img->fresh = (img->desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) != 0; return VK_SUCCESS; }
    }
    HRESULT hr = d3d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &img->desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&img->t.res));
    if (FAILED(hr)) { Log("image %ux%u fmt %d failed %08x", unsigned(img->desc.Width), img->desc.Height, int(img->format), unsigned(hr)); return VK_ERROR_OUT_OF_DEVICE_MEMORY; }
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkCreateImageView(VkDevice, const VkImageViewCreateInfo* info, const VkAllocationCallbacks*, VkImageView* out) {
    *out = VkImageView(Handle(new XImageView{Obj<XImage>(uint64_t(info->image)), info->format, info->subresourceRange}));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyImageView(VkDevice, VkImageView view, const VkAllocationCallbacks*) { delete Obj<XImageView>(uint64_t(view)); }

XVK VkResult VKAPI_CALL vkCreateCommandPool(VkDevice, const VkCommandPoolCreateInfo*, const VkAllocationCallbacks*, VkCommandPool* out) {
    *out = VkCommandPool(Handle(new XCommandPool));
    return VK_SUCCESS;
}

void FreeCommandBuffer(XCommandBuffer* cb) {
    ClearStaging(cb);
    if (cb->open) cb->list->Close();
    {
        std::lock_guard<std::mutex> guard(cb->device->spareLock);
        cb->device->spareLists.emplace_back(cb->allocator, cb->list);
    }
    delete cb;
}

XVK void VKAPI_CALL vkDestroyCommandPool(VkDevice, VkCommandPool pool, const VkAllocationCallbacks*) {
    auto* p = Obj<XCommandPool>(uint64_t(pool));
    if (!p) return;
    for (auto* cb : p->buffers) FreeCommandBuffer(cb);
    delete p;
}

XVK VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* info, VkCommandBuffer* out) {
    auto* dev = reinterpret_cast<XDevice*>(device);
    auto* pool = Obj<XCommandPool>(uint64_t(info->commandPool));
    for (uint32_t i = 0; i < info->commandBufferCount; ++i) {
        auto* cb = new XCommandBuffer;
        cb->device = dev;
        {
            std::lock_guard<std::mutex> guard(dev->spareLock);
            if (!dev->spareLists.empty()) {
                cb->allocator = dev->spareLists.back().first;
                cb->list = dev->spareLists.back().second;
                dev->spareLists.pop_back();
            }
        }
        if (!cb->list) {
            dev->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&cb->allocator));
            dev->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cb->allocator, nullptr, IID_PPV_ARGS(&cb->list));
            cb->list->Close();
        }
        pool->buffers.push_back(cb);
        out[i] = reinterpret_cast<VkCommandBuffer>(cb);
    }
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkFreeCommandBuffers(VkDevice, VkCommandPool pool, uint32_t n, const VkCommandBuffer* buffers) {
    auto* p = Obj<XCommandPool>(uint64_t(pool));
    for (uint32_t i = 0; i < n; ++i) {
        auto* cb = reinterpret_cast<XCommandBuffer*>(buffers[i]);
        if (!cb) continue;
        p->buffers.erase(std::remove(p->buffers.begin(), p->buffers.end(), cb), p->buffers.end());
        FreeCommandBuffer(cb);
    }
}

XVK VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer buffer, const VkCommandBufferBeginInfo*) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    ClearStaging(cb);
    cb->allocator->Reset();
    cb->list->Reset(cb->allocator, nullptr);
    cb->open = true;
    ++cb->epoch;
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer buffer, VkCommandBufferResetFlags) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    if (cb->open) { cb->list->Close(); cb->open = false; }
    ClearStaging(cb);
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer buffer) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    cb->open = false;
    return SUCCEEDED(cb->list->Close()) ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

XVK void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer buffer, VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t, const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t imageCount, const VkImageMemoryBarrier* images) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    D3D12_RESOURCE_BARRIER uav{};
    uav.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    cb->list->ResourceBarrier(1, &uav);
    for (uint32_t i = 0; i < imageCount; ++i)
        if (images[i].newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) Use(cb->list, Obj<XImage>(uint64_t(images[i].image))->t, D3D12_RESOURCE_STATE_PRESENT);
}

XVK void VKAPI_CALL vkCmdCopyBuffer(VkCommandBuffer buffer, VkBuffer src, VkBuffer dst, uint32_t n, const VkBufferCopy* regions) {
    if (SkipWork('c')) return;
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto *s = Obj<XBuffer>(uint64_t(src)), *d = Obj<XBuffer>(uint64_t(dst));
    if (s->memory == d->memory) {
        for (uint32_t i = 0; i < n; ++i) {
            g_perf.bytesSame += regions[i].size;
            void* cpu = nullptr;
            ID3D12Resource* tmp = Staging(cb, regions[i].size, &cpu);
            Tracked tt{tmp, D3D12_RESOURCE_STATE_COMMON};
            Use(cb->list, tt, D3D12_RESOURCE_STATE_COPY_DEST);
            Use(cb->list, s->memory->whole, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cb->list->CopyBufferRegion(tmp, 0, s->memory->whole.res, s->offset + regions[i].srcOffset, regions[i].size);
            Use(cb->list, tt, D3D12_RESOURCE_STATE_COPY_SOURCE);
            Use(cb->list, d->memory->whole, D3D12_RESOURCE_STATE_COPY_DEST);
            cb->list->CopyBufferRegion(d->memory->whole.res, d->offset + regions[i].dstOffset, tmp, 0, regions[i].size);
        }
        return;
    }
    Use(cb->list, s->memory->whole, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Use(cb->list, d->memory->whole, D3D12_RESOURCE_STATE_COPY_DEST);
    for (uint32_t i = 0; i < n; ++i) {
        g_perf.bytesCopy += regions[i].size;
        cb->list->CopyBufferRegion(d->memory->whole.res, d->offset + regions[i].dstOffset, s->memory->whole.res, s->offset + regions[i].srcOffset, regions[i].size);
    }
}

XVK void VKAPI_CALL vkCmdUpdateBuffer(VkCommandBuffer buffer, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, const void* data) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* d = Obj<XBuffer>(uint64_t(dst));
    void* cpu = nullptr;
    g_perf.bytesUpdate += size;
    ID3D12Resource* tmp = Staging(cb, size, &cpu);
    std::memcpy(cpu, data, size_t(size));
    Use(cb->list, d->memory->whole, D3D12_RESOURCE_STATE_COPY_DEST);
    cb->list->CopyBufferRegion(d->memory->whole.res, d->offset + offset, tmp, 0, size);
}

XVK void VKAPI_CALL vkCmdFillBuffer(VkCommandBuffer buffer, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, uint32_t value) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* d = Obj<XBuffer>(uint64_t(dst));
    if (size == VK_WHOLE_SIZE) size = (d->size - offset) & ~3ull;
    g_perf.bytesFill += size;
    void* cpu = nullptr;
    ID3D12Resource* tmp = Staging(cb, size, &cpu);
    auto* words = static_cast<uint32_t*>(cpu);
    for (uint64_t i = 0; i < size / 4; ++i) words[i] = value;
    Use(cb->list, d->memory->whole, D3D12_RESOURCE_STATE_COPY_DEST);
    cb->list->CopyBufferRegion(d->memory->whole.res, d->offset + offset, tmp, 0, size);
}

XVK void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer buffer, VkBuffer src, VkImage dst, VkImageLayout, uint32_t n, const VkBufferImageCopy* regions) {
    if (SkipWork('i')) return;
    for (uint32_t i = 0; i < n; ++i) CopyBufferImage(reinterpret_cast<XCommandBuffer*>(buffer), Obj<XBuffer>(uint64_t(src)), Obj<XImage>(uint64_t(dst)), regions[i], true);
}

XVK void VKAPI_CALL vkCmdCopyImageToBuffer(VkCommandBuffer buffer, VkImage src, VkImageLayout, VkBuffer dst, uint32_t n, const VkBufferImageCopy* regions) {
    for (uint32_t i = 0; i < n; ++i) CopyBufferImage(reinterpret_cast<XCommandBuffer*>(buffer), Obj<XBuffer>(uint64_t(dst)), Obj<XImage>(uint64_t(src)), regions[i], false);
}

XVK void VKAPI_CALL vkCmdCopyImage(VkCommandBuffer buffer, VkImage src, VkImageLayout, VkImage dst, VkImageLayout, uint32_t n, const VkImageCopy* regions) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto *s = Obj<XImage>(uint64_t(src)), *d = Obj<XImage>(uint64_t(dst));
    Use(cb->list, s->t, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Use(cb->list, d->t, D3D12_RESOURCE_STATE_COPY_DEST);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& r = regions[i];
        for (uint32_t l = 0; l < r.srcSubresource.layerCount; ++l) {
            D3D12_TEXTURE_COPY_LOCATION a{d->t.res, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX}, b{s->t.res, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
            a.SubresourceIndex = Subresource(d, r.dstSubresource.mipLevel, r.dstSubresource.baseArrayLayer + l, 0);
            b.SubresourceIndex = Subresource(s, r.srcSubresource.mipLevel, r.srcSubresource.baseArrayLayer + l, 0);
            D3D12_BOX box{UINT(r.srcOffset.x), UINT(r.srcOffset.y), UINT(r.srcOffset.z), UINT(r.srcOffset.x) + r.extent.width, UINT(r.srcOffset.y) + r.extent.height, UINT(r.srcOffset.z) + std::max(1u, r.extent.depth)};
            cb->list->CopyTextureRegion(&a, UINT(r.dstOffset.x), UINT(r.dstOffset.y), UINT(r.dstOffset.z), &b, &box);
        }
    }
}

D3D12_RENDER_TARGET_VIEW_DESC RtvFor(const D3D12_RESOURCE_DESC& d, DXGI_FORMAT format, uint32_t mip, uint32_t layer, uint32_t count) {
    D3D12_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = format;
    static const bool viewHack = std::getenv("XVK_RGBA_VIEW_HACK") != nullptr;
    const auto rgbaFamily = [](DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB; };
    const auto bgraFamily = [](DXGI_FORMAT f) { return f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB; };
    if (viewHack && ((rgbaFamily(d.Format) && bgraFamily(format)) || (bgraFamily(d.Format) && rgbaFamily(format)))) rv.Format = d.Format;
    if (d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        rv.Texture3D = {mip, layer, count};
    } else if (d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY;
        rv.Texture1DArray = {mip, layer, count};
    } else if (d.SampleDesc.Count > 1) {
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        rv.Texture2DMSArray = {layer, count};
    } else {
        rv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        rv.Texture2DArray = {mip, layer, count, 0};
    }
    return rv;
}

D3D12_DEPTH_STENCIL_VIEW_DESC DsvFor(const D3D12_RESOURCE_DESC& d, DXGI_FORMAT format, uint32_t mip, uint32_t layer, uint32_t count) {
    D3D12_DEPTH_STENCIL_VIEW_DESC dv{};
    dv.Format = format;
    if (d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE1D) {
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY;
        dv.Texture1DArray = {mip, layer, count};
    } else if (d.SampleDesc.Count > 1) {
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        dv.Texture2DMSArray = {layer, count};
    } else {
        dv.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dv.Texture2DArray = {mip, layer, count};
    }
    return dv;
}

XVK void VKAPI_CALL vkCmdBlitImage(VkCommandBuffer buffer, VkImage src, VkImageLayout, VkImage dst, VkImageLayout, uint32_t n, const VkImageBlit* regions, VkFilter filter) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* dev = cb->device;
    auto *s = Obj<XImage>(uint64_t(src)), *d = Obj<XImage>(uint64_t(dst));
    std::lock_guard<std::mutex> g(dev->lock);
    if (!BlitInit(dev)) return;
    ID3D12PipelineState* pso = BlitPso(dev, d->desc.Format);
    if (!pso) return;
    Use(cb->list, s->t, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Use(cb->list, d->t, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cb->list->SetDescriptorHeaps(1, &dev->srv.heap);
    cb->list->SetGraphicsRootSignature(dev->blitRoot);
    cb->list->SetPipelineState(pso);
    cb->list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& r = regions[i];
        const uint32_t sm = r.srcSubresource.mipLevel, dm = r.dstSubresource.mipLevel;
        const float sw = float(std::max(1u, s->extent.width >> sm)), sh = float(std::max(1u, s->extent.height >> sm));
        UINT srvIndex = dev->srv.Take();
        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = s->desc.Format;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2DArray = {sm, 1, r.srcSubresource.baseArrayLayer, 1, 0, 0};
        dev->device->CreateShaderResourceView(s->t.res, &sv, dev->srv.Cpu(srvIndex));
        UINT rtvIndex = dev->rtv.Take();
        D3D12_RENDER_TARGET_VIEW_DESC rv = RtvFor(d->desc, d->desc.Format, dm, r.dstSubresource.baseArrayLayer, 1);
        dev->device->CreateRenderTargetView(d->t.res, &rv, dev->rtv.Cpu(rtvIndex));
        auto rtv = dev->rtv.Cpu(rtvIndex);
        cb->list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        const LONG x0 = std::min(r.dstOffsets[0].x, r.dstOffsets[1].x), x1 = std::max(r.dstOffsets[0].x, r.dstOffsets[1].x);
        const LONG y0 = std::min(r.dstOffsets[0].y, r.dstOffsets[1].y), y1 = std::max(r.dstOffsets[0].y, r.dstOffsets[1].y);
        float u0 = r.srcOffsets[0].x / sw, u1 = r.srcOffsets[1].x / sw, v0 = r.srcOffsets[0].y / sh, v1 = r.srcOffsets[1].y / sh;
        if (r.dstOffsets[0].x > r.dstOffsets[1].x) std::swap(u0, u1);
        if (r.dstOffsets[0].y > r.dstOffsets[1].y) std::swap(v0, v1);
        D3D12_VIEWPORT vp{float(x0), float(y0), float(x1 - x0), float(y1 - y0), 0, 1};
        D3D12_RECT sc{x0, y0, x1, y1};
        cb->list->RSSetViewports(1, &vp);
        cb->list->RSSetScissorRects(1, &sc);
        float k[6] = {u0, v0, u1, v1, filter == VK_FILTER_LINEAR ? 1.0f : 0.0f, 0.0f};
        cb->list->SetGraphicsRoot32BitConstants(1, 6, k, 0);
        cb->list->SetGraphicsRootDescriptorTable(0, dev->srv.Gpu(srvIndex));
        cb->list->DrawInstanced(3, 1, 0, 0);
    }
}

XVK void VKAPI_CALL vkCmdClearColorImage(VkCommandBuffer buffer, VkImage image, VkImageLayout, const VkClearColorValue* color, uint32_t n, const VkImageSubresourceRange* ranges) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* dev = cb->device;
    auto* img = Obj<XImage>(uint64_t(image));
    Use(cb->list, img->t, D3D12_RESOURCE_STATE_RENDER_TARGET);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& r = ranges[i];
        const uint32_t mips = r.levelCount == VK_REMAINING_MIP_LEVELS ? img->mips - r.baseMipLevel : r.levelCount;
        const uint32_t layers = r.layerCount == VK_REMAINING_ARRAY_LAYERS ? img->layers - r.baseArrayLayer : r.layerCount;
        for (uint32_t m = 0; m < mips; ++m) {
            UINT idx = dev->rtv.Take();
            const uint32_t slices = img->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? std::max(1u, img->extent.depth >> (r.baseMipLevel + m)) : layers;
            D3D12_RENDER_TARGET_VIEW_DESC rv = RtvFor(img->desc, img->desc.Format, r.baseMipLevel + m, img->desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 0 : r.baseArrayLayer, slices);
            dev->device->CreateRenderTargetView(img->t.res, &rv, dev->rtv.Cpu(idx));
            cb->list->ClearRenderTargetView(dev->rtv.Cpu(idx), color->float32, 0, nullptr);
        }
    }
}

XVK void VKAPI_CALL vkCmdClearDepthStencilImage(VkCommandBuffer buffer, VkImage image, VkImageLayout, const VkClearDepthStencilValue* value, uint32_t n, const VkImageSubresourceRange* ranges) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto* dev = cb->device;
    auto* img = Obj<XImage>(uint64_t(image));
    Use(cb->list, img->t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& r = ranges[i];
        const uint32_t mips = r.levelCount == VK_REMAINING_MIP_LEVELS ? img->mips - r.baseMipLevel : r.levelCount;
        const uint32_t layers = r.layerCount == VK_REMAINING_ARRAY_LAYERS ? img->layers - r.baseArrayLayer : r.layerCount;
        D3D12_CLEAR_FLAGS flags = D3D12_CLEAR_FLAGS(((r.aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) ? D3D12_CLEAR_FLAG_DEPTH : 0) | ((r.aspectMask & VK_IMAGE_ASPECT_STENCIL_BIT) ? D3D12_CLEAR_FLAG_STENCIL : 0));
        for (uint32_t m = 0; m < mips; ++m) {
            UINT idx = dev->dsv.Take();
            D3D12_DEPTH_STENCIL_VIEW_DESC dv = DsvFor(img->desc, Format(img->format)->dx, r.baseMipLevel + m, r.baseArrayLayer, layers);
            dev->device->CreateDepthStencilView(img->t.res, &dv, dev->dsv.Cpu(idx));
            cb->list->ClearDepthStencilView(dev->dsv.Cpu(idx), flags, value->depth, UINT8(value->stencil), 0, nullptr);
        }
    }
}

XVK void VKAPI_CALL vkCmdResolveImage(VkCommandBuffer buffer, VkImage src, VkImageLayout, VkImage dst, VkImageLayout, uint32_t n, const VkImageResolve* regions) {
    auto* cb = reinterpret_cast<XCommandBuffer*>(buffer);
    auto *s = Obj<XImage>(uint64_t(src)), *d = Obj<XImage>(uint64_t(dst));
    Use(cb->list, s->t, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
    Use(cb->list, d->t, D3D12_RESOURCE_STATE_RESOLVE_DEST);
    for (uint32_t i = 0; i < n; ++i)
        cb->list->ResolveSubresource(d->t.res, Subresource(d, regions[i].dstSubresource.mipLevel, regions[i].dstSubresource.baseArrayLayer, 0), s->t.res, Subresource(s, regions[i].srcSubresource.mipLevel, regions[i].srcSubresource.baseArrayLayer, 0), d->desc.Format);
}

XVK VkResult VKAPI_CALL vkCreateSemaphore(VkDevice device, const VkSemaphoreCreateInfo* info, const VkAllocationCallbacks*, VkSemaphore* out) {
    auto* s = new XSemaphore;
    for (auto* p = static_cast<const VkBaseInStructure*>(info->pNext); p; p = p->pNext)
        if (p->sType == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
            auto* t = reinterpret_cast<const VkSemaphoreTypeCreateInfo*>(p);
            s->timeline = t->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE;
            s->value = t->initialValue;
        }
    reinterpret_cast<XDevice*>(device)->device->CreateFence(s->value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s->fence));
    *out = VkSemaphore(Handle(s));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroySemaphore(VkDevice, VkSemaphore semaphore, const VkAllocationCallbacks*) {
    auto* s = Obj<XSemaphore>(uint64_t(semaphore));
    if (!s) return;
    Release(s->fence);
    delete s;
}

XVK VkResult VKAPI_CALL vkGetSemaphoreCounterValueKHR(VkDevice, VkSemaphore semaphore, uint64_t* value) { *value = Obj<XSemaphore>(uint64_t(semaphore))->fence->GetCompletedValue(); return VK_SUCCESS; }

XVK VkResult VKAPI_CALL vkWaitSemaphoresKHR(VkDevice, const VkSemaphoreWaitInfo* info, uint64_t timeout) {
    PerfTimer perfTimer(g_perf.waitUs); ++g_perf.waits;
    std::vector<HANDLE> events;
    for (uint32_t i = 0; i < info->semaphoreCount; ++i) {
        auto* s = Obj<XSemaphore>(uint64_t(info->pSemaphores[i]));
        if (SpinFence(s->fence, info->pValues[i])) { if (info->flags & VK_SEMAPHORE_WAIT_ANY_BIT) { for (auto e : events) CloseHandle(e); return VK_SUCCESS; } continue; }
        HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        s->fence->SetEventOnCompletion(info->pValues[i], e);
        events.push_back(e);
    }
    DWORD r = WAIT_OBJECT_0;
    const ULONGLONG end = timeout == UINT64_MAX ? ~0ull : GetTickCount64() + timeout / 1000000;
    while (!events.empty()) {
        const ULONGLONG now = GetTickCount64();
        const ULONGLONG left = end > now ? end - now : 0;
        r = WaitForMultipleObjects(DWORD(events.size()), events.data(), (info->flags & VK_SEMAPHORE_WAIT_ANY_BIT) ? FALSE : TRUE, DWORD(std::min<ULONGLONG>(left, 10000)));
        if (r != WAIT_TIMEOUT) break;
        for (uint32_t i = 0; i < info->semaphoreCount; ++i) StallDump("wait sem", Obj<XSemaphore>(uint64_t(info->pSemaphores[i])), info->pValues[i]);
        if (left <= 10000) break;
    }
    for (auto e : events) CloseHandle(e);
    return r == WAIT_TIMEOUT ? VK_TIMEOUT : VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkCreateFence(VkDevice device, const VkFenceCreateInfo* info, const VkAllocationCallbacks*, VkFence* out) {
    auto* f = new XFence;
    f->signaled = (info->flags & VK_FENCE_CREATE_SIGNALED_BIT) != 0;
    reinterpret_cast<XDevice*>(device)->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&f->fence));
    *out = VkFence(Handle(f));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroyFence(VkDevice, VkFence fence, const VkAllocationCallbacks*) {
    auto* f = Obj<XFence>(uint64_t(fence));
    if (!f) return;
    Release(f->fence);
    delete f;
}

bool FenceDone(XFence* f) { return f->signaled || (f->target && f->fence->GetCompletedValue() >= f->target); }

XVK VkResult VKAPI_CALL vkGetFenceStatus(VkDevice, VkFence fence) { return FenceDone(Obj<XFence>(uint64_t(fence))) ? VK_SUCCESS : VK_NOT_READY; }

XVK VkResult VKAPI_CALL vkResetFences(VkDevice, uint32_t n, const VkFence* fences) {
    for (uint32_t i = 0; i < n; ++i) { auto* f = Obj<XFence>(uint64_t(fences[i])); f->signaled = false; f->target = 0; }
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkWaitForFences(VkDevice, uint32_t n, const VkFence* fences, VkBool32 all, uint64_t timeout) {
    PerfTimer perfTimer(g_perf.waitUs); ++g_perf.waits;
    const ULONGLONG start = GetTickCount64();
    const ULONGLONG end = timeout == UINT64_MAX ? ~0ull : start + timeout / 1000000;
    HANDLE e = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    VkResult result = VK_SUCCESS;
    for (;;) {
        uint32_t done = 0;
        XFence* pending = nullptr;
        for (uint32_t i = 0; i < n; ++i) { auto* f = Obj<XFence>(uint64_t(fences[i])); if (FenceDone(f)) ++done; else if (!pending && f->target) pending = f; }
        if (all ? done == n : done > 0) break;
        const ULONGLONG now = GetTickCount64();
        if (now >= end) { result = VK_TIMEOUT; break; }
        if (now - start >= 10000) StallDump("fence", nullptr, pending ? pending->target : 0);
        if (pending && SpinFence(pending->fence, pending->target)) continue;
        if (pending) { pending->fence->SetEventOnCompletion(pending->target, e); WaitForSingleObject(e, DWORD(std::min<ULONGLONG>(end - now, 50))); }
        else Sleep(1);
    }
    CloseHandle(e);
    return result;
}

struct GpuTimer {
    static constexpr uint32_t kSlots = 64;
    std::mutex lock;
    bool tried = false, ready = false;
    ID3D12QueryHeap* heap = nullptr;
    ID3D12Resource* readback = nullptr;
    ID3D12Fence* fence = nullptr;
    const uint64_t* ticks = nullptr;
    uint64_t freq = 0, value = 0, next = 0;
    ID3D12CommandAllocator* allocators[kSlots] = {};
    ID3D12GraphicsCommandList* begin[kSlots] = {};
    ID3D12GraphicsCommandList* end[kSlots] = {};
    uint64_t used[kSlots] = {};

    bool Init(ID3D12Device* device, ID3D12CommandQueue* queue) {
        tried = true;
        const char* env = std::getenv("XVK_GPU_TIME");
        if (!env || !*env || *env == '0') return false;
        if (FAILED(queue->GetTimestampFrequency(&freq)) || !freq) return false;
        D3D12_QUERY_HEAP_DESC hd = {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, kSlots * 2, 0};
        if (FAILED(device->CreateQueryHeap(&hd, IID_PPV_ARGS(&heap)))) return false;
        D3D12_HEAP_PROPERTIES hp = {D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = kSlots * 2 * sizeof(uint64_t);
        rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1; rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
        void* mapped = nullptr;
        if (FAILED(readback->Map(0, nullptr, &mapped))) return false;
        ticks = static_cast<const uint64_t*>(mapped);
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return false;
        for (uint32_t i = 0; i < kSlots; ++i) {
            if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocators[i])))) return false;
            if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i], nullptr, IID_PPV_ARGS(&begin[i])))) return false;
            begin[i]->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, i * 2);
            begin[i]->Close();
            if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocators[i], nullptr, IID_PPV_ARGS(&end[i])))) return false;
            end[i]->EndQuery(heap, D3D12_QUERY_TYPE_TIMESTAMP, i * 2 + 1);
            end[i]->ResolveQueryData(heap, D3D12_QUERY_TYPE_TIMESTAMP, i * 2, 2, readback, i * 2 * sizeof(uint64_t));
            end[i]->Close();
        }
        Log("gpu timer: on, %u slots, %llu ticks/s", kSlots, (unsigned long long)freq);
        return true;
    }

    void Harvest(uint32_t i) {
        if (!used[i]) return;
        const uint64_t a = ticks[i * 2], b = ticks[i * 2 + 1];
        if (b > a) g_perf.gpuUs += (b - a) * 1000000ull / freq;
        ++g_perf.gpuTimed;
        used[i] = 0;
    }

    void Execute(ID3D12Device* device, ID3D12CommandQueue* queue, std::vector<ID3D12CommandList*>& lists) {
        std::lock_guard<std::mutex> guard(lock);
        if (!tried) ready = Init(device, queue);
        if (!ready || lists.empty()) { if (!lists.empty()) queue->ExecuteCommandLists(UINT(lists.size()), lists.data()); return; }
        const uint32_t i = uint32_t(next % kSlots);
        const uint64_t done = fence->GetCompletedValue();
        if (used[i] && used[i] > done) { ++g_perf.gpuSkipped; queue->ExecuteCommandLists(UINT(lists.size()), lists.data()); return; }
        Harvest(i);
        ++next;
        lists.insert(lists.begin(), begin[i]);
        lists.push_back(end[i]);
        queue->ExecuteCommandLists(UINT(lists.size()), lists.data());
        queue->Signal(fence, ++value);
        used[i] = value;
        for (uint32_t k = 0; k < kSlots; ++k) if (used[k] && used[k] <= done) Harvest(k);
    }
};
GpuTimer g_gpuTimer;

XVK VkResult VKAPI_CALL vkQueueSubmit(VkQueue queue, uint32_t n, const VkSubmitInfo* submits, VkFence fence) {
    PerfTimer perfTimer(g_perf.submitUs); ++g_perf.submits;
    auto* q = reinterpret_cast<XQueue*>(queue);
    for (uint32_t i = 0; i < n; ++i) {
        const auto& s = submits[i];
        const VkTimelineSemaphoreSubmitInfo* tl = nullptr;
        for (auto* p = static_cast<const VkBaseInStructure*>(s.pNext); p; p = p->pNext)
            if (p->sType == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) tl = reinterpret_cast<const VkTimelineSemaphoreSubmitInfo*>(p);
        for (uint32_t w = 0; w < s.waitSemaphoreCount; ++w) {
            auto* sem = Obj<XSemaphore>(uint64_t(s.pWaitSemaphores[w]));
            const uint64_t want = sem->timeline && tl && w < tl->waitSemaphoreValueCount ? tl->pWaitSemaphoreValues[w] : sem->value;
            q->queue->Wait(sem->fence, want);
            g_waits[g_submits % 16] = {sem, want, g_submits};
        }
        ++g_submits;
        std::vector<ID3D12CommandList*> lists;
        for (uint32_t c = 0; c < s.commandBufferCount; ++c) lists.push_back(reinterpret_cast<XCommandBuffer*>(s.pCommandBuffers[c])->list);
        g_gpuTimer.Execute(q->device->device, q->queue, lists);
        for (uint32_t g = 0; g < s.signalSemaphoreCount; ++g) {
            auto* sem = Obj<XSemaphore>(uint64_t(s.pSignalSemaphores[g]));
            if (sem->timeline && tl && g < tl->signalSemaphoreValueCount) sem->value = tl->pSignalSemaphoreValues[g];
            else if (!sem->timeline) ++sem->value;
            q->queue->Signal(sem->fence, sem->value);
            sem->queued = std::max(sem->queued, sem->value);
        }
    }
    if (fence) {
        auto* f = Obj<XFence>(uint64_t(fence));
        f->signaled = false;
        f->target = ++q->idleValue;
        q->queue->Signal(q->idle, f->target);
        if (f->fence != q->idle) { Release(f->fence); q->idle->AddRef(); f->fence = q->idle; }
    }
    return VK_SUCCESS;
}

IUnknown* CoreWindowUnknown() {
    static IUnknown* window = []() -> IUnknown* {
        if (!std::getenv("XVK_COREWINDOW")) return nullptr;
        HMODULE combase = LoadLibraryW(L"combase.dll");
        if (!combase) { Log("corewindow: no combase"); return nullptr; }
        using MakeRef = HRESULT(WINAPI*)(PCWSTR, UINT32, HSTRING_HEADER*, HSTRING*);
        using Factory = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
        auto makeRef = reinterpret_cast<MakeRef>(GetProcAddress(combase, "WindowsCreateStringReference"));
        auto factory = reinterpret_cast<Factory>(GetProcAddress(combase, "RoGetActivationFactory"));
        if (!makeRef || !factory) { Log("corewindow: no winrt exports"); return nullptr; }
        static const wchar_t kName[] = L"Windows.ApplicationModel.Core.CoreApplication";
        HSTRING_HEADER header{};
        HSTRING name = nullptr;
        if (FAILED(makeRef(kName, UINT32(wcslen(kName)), &header, &name))) return nullptr;
        ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication* app = nullptr;
        HRESULT hr = factory(name, __uuidof(ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication), reinterpret_cast<void**>(&app));
        if (FAILED(hr)) { Log("corewindow: factory %08x", unsigned(hr)); return nullptr; }
        ABI::Windows::ApplicationModel::Core::ICoreApplicationView* view = nullptr;
        hr = app->get_MainView(&view);
        app->Release();
        if (FAILED(hr) || !view) { Log("corewindow: main view %08x", unsigned(hr)); return nullptr; }
        ABI::Windows::UI::Core::ICoreWindow* core = nullptr;
        hr = view->get_CoreWindow(&core);
        view->Release();
        if (FAILED(hr) || !core) { Log("corewindow: window %08x", unsigned(hr)); return nullptr; }
        Log("corewindow: presenting to the app window");
        return core;
    }();
    return window;
}

XVK VkResult VKAPI_CALL vkCreateWin32SurfaceKHR(VkInstance, const VkWin32SurfaceCreateInfoKHR* info, const VkAllocationCallbacks*, VkSurfaceKHR* out) {
    *out = VkSurfaceKHR(Handle(new XSurface{info->hwnd}));
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR surface, const VkAllocationCallbacks*) { delete Obj<XSurface>(uint64_t(surface)); }

XVK VkBool32 VKAPI_CALL vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice, uint32_t) { return VK_TRUE; }

XVK VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR, VkBool32* s) { *s = VK_TRUE; return VK_SUCCESS; }

XVK VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* c) {
    RECT r{};
    if (CoreWindowUnknown()) r = {0, 0, 1920, 1080};
    else GetClientRect(Obj<XSurface>(uint64_t(surface))->hwnd, &r);
    *c = {};
    c->minImageCount = 2;
    c->maxImageCount = 3;
    c->currentExtent = {UINT(std::max<LONG>(1, r.right - r.left)), UINT(std::max<LONG>(1, r.bottom - r.top))};
    c->minImageExtent = {1, 1};
    c->maxImageExtent = {16384, 16384};
    c->maxImageArrayLayers = 1;
    c->supportedTransforms = c->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    c->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    c->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    return VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t* n, VkSurfaceFormatKHR* f) {
    const VkSurfaceFormatKHR all[] = {{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}, {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}};
    if (!f) { *n = 2; return VK_SUCCESS; }
    uint32_t c = std::min(*n, 2u);
    for (uint32_t i = 0; i < c; ++i) f[i] = all[i];
    *n = c;
    return c < 2 ? VK_INCOMPLETE : VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t* n, VkPresentModeKHR* m) {
    const VkPresentModeKHR all[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
    if (!m) { *n = 2; return VK_SUCCESS; }
    uint32_t c = std::min(*n, 2u);
    for (uint32_t i = 0; i < c; ++i) m[i] = all[i];
    *n = c;
    return c < 2 ? VK_INCOMPLETE : VK_SUCCESS;
}

void DumpDebugMessages(ID3D12Device* device) {
    ID3D12InfoQueue* q = nullptr;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&q)))) return;
    UINT64 n = q->GetNumStoredMessages();
    for (UINT64 i = n > 40 ? n - 40 : 0; i < n; ++i) {
        SIZE_T size = 0;
        q->GetMessage(i, nullptr, &size);
        std::vector<char> buf(size);
        auto* m = reinterpret_cast<D3D12_MESSAGE*>(buf.data());
        if (SUCCEEDED(q->GetMessage(i, m, &size)) && m->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) Log("d3d12: %s", m->pDescription);
    }
    q->Release();
}

XVK VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* info, const VkAllocationCallbacks*, VkSwapchainKHR* out) {
    auto* dev = reinterpret_cast<XDevice*>(device);
    auto* old = Obj<XSwapchain>(uint64_t(info->oldSwapchain));
    if (old && old->chain) { WaitIdle(dev->queue); for (auto* i : old->images) Release(i->t.res); Release(old->chain); }
    const FormatInfo* fi = Format(info->imageFormat);
    if (!fi) return VK_ERROR_FORMAT_NOT_SUPPORTED;
    DXGI_SWAP_CHAIN_DESC1 d{};
    d.Width = info->imageExtent.width;
    d.Height = info->imageExtent.height;
    d.Format = fi->dx == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ? DXGI_FORMAT_B8G8R8A8_UNORM : fi->dx == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ? DXGI_FORMAT_R8G8B8A8_UNORM : fi->dx;
    d.SampleDesc.Count = 1;
    d.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    d.BufferCount = std::clamp(info->minImageCount, 2u, 3u);
    d.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    IDXGISwapChain1* chain = nullptr;
    HRESULT hr;
    if (IUnknown* core = CoreWindowUnknown()) {
        d.Scaling = DXGI_SCALING_STRETCH;
        hr = dev->physical->instance->factory->CreateSwapChainForCoreWindow(dev->queue.queue, core, &d, nullptr, &chain);
    } else {
        hr = dev->physical->instance->factory->CreateSwapChainForHwnd(dev->queue.queue, Obj<XSurface>(uint64_t(info->surface))->hwnd, &d, nullptr, nullptr, &chain);
    }
    if (FAILED(hr)) { Log("swapchain failed %08x, device removed reason %08x", unsigned(hr), unsigned(dev->device->GetDeviceRemovedReason())); DumpDebugMessages(dev->device); return VK_ERROR_INITIALIZATION_FAILED; }
    auto* sc = new XSwapchain{dev};
    chain->QueryInterface(IID_PPV_ARGS(&sc->chain));
    chain->Release();
    sc->format = info->imageFormat;
    for (UINT i = 0; i < d.BufferCount; ++i) {
        auto* img = new XImage;
        img->owned = false;
        img->format = info->imageFormat;
        img->extent = {d.Width, d.Height, 1};
        sc->chain->GetBuffer(i, IID_PPV_ARGS(&img->t.res));
        img->desc = img->t.res->GetDesc();
        img->desc.Format = d.Format;
        img->t.state = D3D12_RESOURCE_STATE_PRESENT;
        sc->images.push_back(img);
    }
    *out = VkSwapchainKHR(Handle(sc));
    Log("swapchain %ux%u x%u", d.Width, d.Height, d.BufferCount);
    return VK_SUCCESS;
}

XVK void VKAPI_CALL vkDestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain, const VkAllocationCallbacks*) {
    auto* sc = Obj<XSwapchain>(uint64_t(swapchain));
    if (!sc) return;
    WaitIdle(reinterpret_cast<XDevice*>(device)->queue);
    for (auto* i : sc->images) { Release(i->t.res); delete i; }
    Release(sc->chain);
    delete sc;
}

XVK VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR swapchain, uint32_t* n, VkImage* images) {
    auto* sc = Obj<XSwapchain>(uint64_t(swapchain));
    const uint32_t total = uint32_t(sc->images.size());
    if (!images) { *n = total; return VK_SUCCESS; }
    uint32_t c = std::min(*n, total);
    for (uint32_t i = 0; i < c; ++i) images[i] = VkImage(Handle(sc->images[i]));
    *n = c;
    return c < total ? VK_INCOMPLETE : VK_SUCCESS;
}

XVK VkResult VKAPI_CALL vkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t, VkSemaphore semaphore, VkFence fence, uint32_t* index) {
    PerfTimer perfTimer(g_perf.acquireUs);
    auto* sc = Obj<XSwapchain>(uint64_t(swapchain));
    auto& q = reinterpret_cast<XDevice*>(device)->queue;
    *index = sc->chain->GetCurrentBackBufferIndex();
    if (semaphore) { auto* s = Obj<XSemaphore>(uint64_t(semaphore)); ++s->value; q.queue->Signal(s->fence, s->value); s->queued = std::max(s->queued, s->value); }
    if (fence) vkQueueSubmit(reinterpret_cast<VkQueue>(&q), 0, nullptr, fence);
    return VK_SUCCESS;
}

void CaptureBackBuffer(XSwapchain* sc, ID3D12CommandQueue* queue, uint64_t index) {
    ID3D12Device* dev = sc->device->device;
    ID3D12Resource* back = nullptr;
    if (FAILED(sc->chain->GetBuffer(sc->chain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(&back)))) return;
    const D3D12_RESOURCE_DESC desc = back->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0;
    UINT64 rowSize = 0, total = 0;
    dev->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &rowSize, &total);
    D3D12_HEAP_PROPERTIES heap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bd.Width = total;
    bd.Height = 1;
    bd.DepthOrArraySize = 1;
    bd.MipLevels = 1;
    bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* rb = nullptr;
    ID3D12CommandAllocator* alloc = nullptr;
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Fence* fence = nullptr;
    if (SUCCEEDED(dev->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&rb))) &&
        SUCCEEDED(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc))) &&
        SUCCEEDED(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, nullptr, IID_PPV_ARGS(&list))) &&
        SUCCEEDED(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = back;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &b);
        D3D12_TEXTURE_COPY_LOCATION dst{rb, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
        dst.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION src{back, D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
        src.SubresourceIndex = 0;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        std::swap(b.Transition.StateBefore, b.Transition.StateAfter);
        list->ResourceBarrier(1, &b);
        list->Close();
        ID3D12CommandList* lists[] = {list};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(fence, 1);
        HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        fence->SetEventOnCompletion(1, done);
        WaitForSingleObject(done, 5000);
        CloseHandle(done);
        void* data = nullptr;
        if (SUCCEEDED(rb->Map(0, nullptr, &data))) {
            const uint32_t w = uint32_t(desc.Width), h = desc.Height;
            const bool rgba = desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            char name[64];
            std::snprintf(name, sizeof name, "xvk_frame_%llu.bmp", (unsigned long long)index);
            if (FILE* f = std::fopen(name, "wb")) {
                const uint32_t imageSize = w * h * 4;
                uint8_t header[54] = {'B', 'M'};
                auto put = [&](int at, uint32_t v) { std::memcpy(header + at, &v, 4); };
                put(2, 54 + imageSize);
                put(10, 54);
                put(14, 40);
                put(18, w);
                put(22, uint32_t(-int32_t(h)));
                header[26] = 1;
                header[28] = 32;
                put(34, imageSize);
                std::fwrite(header, 1, 54, f);
                std::vector<uint8_t> row(w * 4);
                for (uint32_t y = 0; y < h; ++y) {
                    std::memcpy(row.data(), static_cast<uint8_t*>(data) + fp.Offset + uint64_t(y) * fp.Footprint.RowPitch, row.size());
                    if (rgba) for (uint32_t x = 0; x < w; ++x) std::swap(row[x * 4], row[x * 4 + 2]);
                    std::fwrite(row.data(), 1, row.size(), f);
                }
                std::fclose(f);
                Log("captured %s %ux%u format %u", name, w, h, unsigned(desc.Format));
            }
            rb->Unmap(0, nullptr);
        }
    }
    if (fence) fence->Release();
    if (list) list->Release();
    if (alloc) alloc->Release();
    if (rb) rb->Release();
    back->Release();
}

XVK VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    PerfTimer perfTimer(g_perf.presentUs);
    auto* q = reinterpret_cast<XQueue*>(queue);
    for (uint32_t i = 0; i < info->waitSemaphoreCount; ++i) { auto* s = Obj<XSemaphore>(uint64_t(info->pWaitSemaphores[i])); q->queue->Wait(s->fence, s->value); }
    VkResult result = VK_SUCCESS;
    for (uint32_t i = 0; i < info->swapchainCount; ++i) {
        static std::atomic<uint64_t> presents{0};
        const uint64_t n = ++presents;
        if (n == 60 || n == 300) CaptureBackBuffer(Obj<XSwapchain>(uint64_t(info->pSwapchains[i])), q->queue, n);
        HRESULT hr = Obj<XSwapchain>(uint64_t(info->pSwapchains[i]))->chain->Present(1, 0);
        if (n % 120 == 0) PerfReport(120);
        if (FAILED(hr) || n == 1 || n % 60 == 0) Log("present %llu hr %08x tick %llu", (unsigned long long)n, unsigned(hr), (unsigned long long)GetTickCount64());
        VkResult r = SUCCEEDED(hr) ? VK_SUCCESS : VK_ERROR_SURFACE_LOST_KHR;
        if (info->pResults) info->pResults[i] = r;
        if (r != VK_SUCCESS) result = r;
    }
    return result;
}

#include "xcross.inc"
#include "xvk2.inc"

namespace {

std::map<std::string, PFN_vkVoidFunction>& Table() {
    static std::map<std::string, PFN_vkVoidFunction> t = {
#define F(name) {#name, reinterpret_cast<PFN_vkVoidFunction>(&name)}
        F(vkEnumerateInstanceVersion), F(vkEnumerateInstanceLayerProperties), F(vkEnumerateInstanceExtensionProperties), F(vkCreateInstance), F(vkDestroyInstance),
        F(vkEnumeratePhysicalDevices), F(vkGetPhysicalDeviceProperties), F(vkGetPhysicalDeviceProperties2), F(vkGetPhysicalDeviceFeatures), F(vkGetPhysicalDeviceFeatures2),
        F(vkGetPhysicalDeviceQueueFamilyProperties), F(vkGetPhysicalDeviceMemoryProperties), F(vkGetPhysicalDeviceMemoryProperties2), F(vkGetPhysicalDeviceFormatProperties),
        F(vkGetPhysicalDeviceImageFormatProperties), F(vkEnumerateDeviceExtensionProperties), F(vkEnumerateDeviceLayerProperties), F(vkCreateDevice), F(vkDestroyDevice),
        F(vkDeviceWaitIdle), F(vkGetDeviceQueue), F(vkQueueWaitIdle), F(vkAllocateMemory), F(vkGetMemoryHostPointerPropertiesEXT), F(vkFreeMemory), F(vkMapMemory), F(vkUnmapMemory), F(vkFlushMappedMemoryRanges),
        F(vkInvalidateMappedMemoryRanges), F(vkCreateBuffer), F(vkDestroyBuffer), F(vkGetBufferMemoryRequirements), F(vkBindBufferMemory), F(vkCreateImage), F(vkDestroyImage),
        F(vkGetImageMemoryRequirements), F(vkBindImageMemory), F(vkCreateImageView), F(vkDestroyImageView), F(vkCreateCommandPool), F(vkDestroyCommandPool),
        F(vkAllocateCommandBuffers), F(vkFreeCommandBuffers), F(vkBeginCommandBuffer), F(vkResetCommandBuffer), F(vkEndCommandBuffer), F(vkCmdPipelineBarrier),
        F(vkCmdCopyBuffer), F(vkCmdUpdateBuffer), F(vkCmdFillBuffer), F(vkCmdCopyBufferToImage), F(vkCmdCopyImageToBuffer), F(vkCmdCopyImage), F(vkCmdBlitImage),
        F(vkCmdClearColorImage), F(vkCmdClearDepthStencilImage), F(vkCmdResolveImage), F(vkCreateSemaphore), F(vkDestroySemaphore), F(vkGetSemaphoreCounterValueKHR),
        F(vkWaitSemaphoresKHR), F(vkCreateFence), F(vkDestroyFence), F(vkGetFenceStatus), F(vkResetFences), F(vkWaitForFences), F(vkQueueSubmit), F(vkCreateWin32SurfaceKHR),
        F(vkDestroySurfaceKHR), F(vkGetPhysicalDeviceWin32PresentationSupportKHR), F(vkGetPhysicalDeviceSurfaceSupportKHR), F(vkGetPhysicalDeviceSurfaceCapabilitiesKHR),
        F(vkGetPhysicalDeviceSurfaceFormatsKHR), F(vkGetPhysicalDeviceSurfacePresentModesKHR), F(vkCreateSwapchainKHR), F(vkDestroySwapchainKHR), F(vkGetSwapchainImagesKHR),
        F(vkAcquireNextImageKHR), F(vkQueuePresentKHR), F(vkGetBufferDeviceAddressKHR), {"vkGetBufferDeviceAddress", reinterpret_cast<PFN_vkVoidFunction>(&vkGetBufferDeviceAddressKHR)},
        {"vkGetSemaphoreCounterValue", reinterpret_cast<PFN_vkVoidFunction>(&vkGetSemaphoreCounterValueKHR)}, {"vkWaitSemaphores", reinterpret_cast<PFN_vkVoidFunction>(&vkWaitSemaphoresKHR)},
#undef F
    };
    static const bool layer2 = (Layer2(t), true);
    (void)layer2;
    return t;
}

}

XVK PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char* name);

XVK PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance, const char* name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&vkGetDeviceProcAddr);
    if (std::strcmp(name, "vkGetInstanceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&vkGetInstanceProcAddr);
    auto& t = Table();
    auto it = t.find(name);
    if (it != t.end()) return it->second;
    Log("missing entry point: %s", name);
    return nullptr;
}

XVK PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice, const char* name) {
    if (std::strcmp(name, "vkGetDeviceProcAddr") == 0) return reinterpret_cast<PFN_vkVoidFunction>(&vkGetDeviceProcAddr);
    return vkGetInstanceProcAddr(nullptr, name);
}
