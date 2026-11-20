#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <vector>

#define L(name) auto name = reinterpret_cast<PFN_##name>(gipa(inst, #name)); if (!name) { std::printf("missing %s\n", #name); return 2; }

static uint32_t Pixel(uint32_t x, uint32_t y) { return (x * 255 / 635) | ((y * 255 / 359) << 8) | (128u << 16) | (255u << 24); }

int main(int argc, char** argv) {
    HMODULE dll = LoadLibraryA(argc > 1 ? argv[1] : "xvk.dll");
    if (!dll) { std::printf("cannot load xvk.dll\n"); return 2; }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(dll, "vkGetInstanceProcAddr"));
    VkInstance inst = nullptr;
    L(vkCreateInstance)
    const char* iext[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "xvktest", 1, "xvk", 1, VK_API_VERSION_1_3};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app, 0, nullptr, 2, iext};
    if (vkCreateInstance(&ici, nullptr, &inst)) { std::printf("instance failed\n"); return 2; }
    L(vkEnumeratePhysicalDevices) L(vkGetPhysicalDeviceProperties) L(vkCreateDevice) L(vkGetDeviceQueue) L(vkCreateWin32SurfaceKHR) L(vkCreateSwapchainKHR)
    L(vkGetSwapchainImagesKHR) L(vkAllocateMemory) L(vkMapMemory) L(vkCreateBuffer) L(vkGetBufferMemoryRequirements) L(vkBindBufferMemory) L(vkCreateImage)
    L(vkGetImageMemoryRequirements) L(vkBindImageMemory) L(vkCreateCommandPool) L(vkAllocateCommandBuffers) L(vkBeginCommandBuffer) L(vkEndCommandBuffer)
    L(vkCmdCopyBufferToImage) L(vkCmdCopyImageToBuffer) L(vkCmdBlitImage) L(vkCmdClearColorImage) L(vkCmdPipelineBarrier) L(vkQueueSubmit) L(vkCreateFence)
    L(vkWaitForFences) L(vkResetFences) L(vkCreateSemaphore) L(vkAcquireNextImageKHR) L(vkQueuePresentKHR) L(vkDeviceWaitIdle) L(vkCmdFillBuffer) L(vkCmdCopyBuffer)
    uint32_t n = 8;
    VkPhysicalDevice pds[8];
    vkEnumeratePhysicalDevices(inst, &n, pds);
    int failures = 0;
    for (uint32_t gi = 0; gi < n; ++gi) {
        VkPhysicalDevice pd = pds[gi];
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pd, &props);
        float prio = 1;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, 0, 1, &prio};
        const char* dext[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &qci, 0, nullptr, 1, dext, nullptr};
        VkDevice dev;
        if (vkCreateDevice(pd, &dci, nullptr, &dev)) { std::printf("%s: device failed\n", props.deviceName); ++failures; continue; }
        VkQueue q;
        vkGetDeviceQueue(dev, 0, 0, &q);
        auto buffer = [&](VkDeviceSize size, uint32_t type, void** map) {
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT};
            VkBuffer b;
            vkCreateBuffer(dev, &bi, nullptr, &b);
            VkMemoryRequirements r;
            vkGetBufferMemoryRequirements(dev, b, &r);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, r.size, type};
            VkDeviceMemory m;
            vkAllocateMemory(dev, &ai, nullptr, &m);
            vkBindBufferMemory(dev, b, m, 0);
            if (map) vkMapMemory(dev, m, 0, size, 0, map);
            return b;
        };
        auto image = [&](uint32_t w, uint32_t h) {
            VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {w, h, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
            VkImage img;
            vkCreateImage(dev, &ii, nullptr, &img);
            VkMemoryRequirements r;
            vkGetImageMemoryRequirements(dev, img, &r);
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, r.size, 0};
            VkDeviceMemory m;
            vkAllocateMemory(dev, &ai, nullptr, &m);
            vkBindImageMemory(dev, img, m, 0);
            return img;
        };
        const uint32_t W = 636, H = 360;
        void *srcMap = nullptr, *outMap = nullptr, *clrMap = nullptr, *fillMap = nullptr;
        VkBuffer src = buffer(W * H * 4, 1, &srcMap);
        VkBuffer out = buffer(W * 2 * H * 2 * 4, 1, &outMap);
        VkBuffer clr = buffer(64 * 64 * 4, 1, &clrMap);
        VkBuffer dloc = buffer(4096, 0, nullptr);
        VkBuffer fill = buffer(4096, 1, &fillMap);
        auto* px = static_cast<uint32_t*>(srcMap);
        for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) px[y * W + x] = Pixel(x, y);
        VkImage a = image(W, H), b = image(W * 2, H * 2), c = image(64, 64);
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        VkCommandPool pool;
        vkCreateCommandPool(dev, &pci, nullptr, &pool);
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(dev, &cai, &cb);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cb, &bi);
        VkBufferImageCopy up{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, H, 1}};
        vkCmdCopyBufferToImage(cb, src, a, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &up);
        VkImageBlit bl{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {int(W), int(H), 1}}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {int(W * 2), int(H * 2), 1}}};
        vkCmdBlitImage(cb, a, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, b, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl, VK_FILTER_NEAREST);
        VkBufferImageCopy down{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W * 2, H * 2, 1}};
        vkCmdCopyImageToBuffer(cb, b, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, out, 1, &down);
        VkClearColorValue cv{};
        cv.float32[0] = 1; cv.float32[1] = 0.5f; cv.float32[2] = 0; cv.float32[3] = 1;
        VkImageSubresourceRange all{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, c, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &all);
        VkBufferImageCopy cdown{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {64, 64, 1}};
        vkCmdCopyImageToBuffer(cb, c, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, clr, 1, &cdown);
        vkCmdFillBuffer(cb, dloc, 0, 4096, 0xA5A5F00Du);
        VkBufferCopy bc{0, 0, 4096};
        vkCmdCopyBuffer(cb, dloc, fill, 1, &bc);
        vkEndCommandBuffer(cb);
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence;
        vkCreateFence(dev, &fci, nullptr, &fence);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cb};
        vkQueueSubmit(q, 1, &si, fence);
        VkResult wr = vkWaitForFences(dev, 1, &fence, VK_TRUE, 5000000000ull);
        int bad = 0;
        auto* o = static_cast<uint32_t*>(outMap);
        for (uint32_t y = 0; y < H * 2; y += 37) for (uint32_t x = 0; x < W * 2; x += 41) if (o[y * W * 2 + x] != Pixel(x / 2, y / 2)) ++bad;
        auto* cc = static_cast<uint32_t*>(clrMap);
        const bool clearOk = (cc[0] & 0xffffff) == 0x0080ff || (cc[0] & 0xffffff) == 0x007fff;
        const bool fillOk = static_cast<uint32_t*>(fillMap)[1023] == 0xA5A5F00Du;
        std::printf("%s: fence %d, blit samples bad %d, clear %08x %s, fill %s\n", props.deviceName, int(wr), bad, cc[0], clearOk ? "ok" : "BAD", fillOk ? "ok" : "BAD");
        if (wr || bad || !clearOk || !fillOk) ++failures;
        if (gi == 0) {
            WNDCLASSA wc{};
            wc.lpfnWndProc = DefWindowProcA;
            wc.hInstance = GetModuleHandleA(nullptr);
            wc.lpszClassName = "xvktest";
            RegisterClassA(&wc);
            HWND hwnd = CreateWindowA("xvktest", "xvk test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 1280, 720, nullptr, nullptr, wc.hInstance, nullptr);
            VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, nullptr, 0, wc.hInstance, hwnd};
            VkSurfaceKHR surf;
            vkCreateWin32SurfaceKHR(inst, &sci, nullptr, &surf);
            RECT rc;
            GetClientRect(hwnd, &rc);
            VkSwapchainCreateInfoKHR sw{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, nullptr, 0, surf, 2, VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, {UINT(rc.right), UINT(rc.bottom)}, 1,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT};
            sw.presentMode = VK_PRESENT_MODE_FIFO_KHR;
            VkSwapchainKHR chain;
            if (vkCreateSwapchainKHR(dev, &sw, nullptr, &chain)) { std::printf("swapchain failed\n"); ++failures; }
            else {
                uint32_t count = 4;
                VkImage imgs[4];
                vkGetSwapchainImagesKHR(dev, chain, &count, imgs);
                VkSemaphoreCreateInfo semi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
                VkSemaphore acquired, rendered;
                vkCreateSemaphore(dev, &semi, nullptr, &acquired);
                vkCreateSemaphore(dev, &semi, nullptr, &rendered);
                int presented = 0;
                for (int frame = 0; frame < 120; ++frame) {
                    MSG msg;
                    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
                    uint32_t idx;
                    vkAcquireNextImageKHR(dev, chain, UINT64_MAX, acquired, VK_NULL_HANDLE, &idx);
                    vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);
                    vkResetFences(dev, 1, &fence);
                    vkBeginCommandBuffer(cb, &bi);
                    VkImageBlit full{{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {int(W), int(H), 1}}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {{0, 0, 0}, {int(rc.right), int(rc.bottom), 1}}};
                    vkCmdBlitImage(cb, a, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, imgs[idx], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &full, VK_FILTER_LINEAR);
                    VkImageMemoryBarrier pb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, nullptr, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 0, 0, imgs[idx], all};
                    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &pb);
                    vkEndCommandBuffer(cb);
                    VkPipelineStageFlags stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                    VkSubmitInfo ps{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 1, &acquired, &stage, 1, &cb, 1, &rendered};
                    vkQueueSubmit(q, 1, &ps, fence);
                    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, nullptr, 1, &rendered, 1, &chain, &idx};
                    if (vkQueuePresentKHR(q, &pi) == VK_SUCCESS) ++presented;
                }
                vkDeviceWaitIdle(dev);
                std::printf("presented %d/120 frames\n", presented);
                if (presented != 120) ++failures;
            }
            DestroyWindow(hwnd);
        }
    }
    std::printf(failures ? "XVK FAIL (%d)\n" : "XVK PASS\n", failures);
    return failures ? 1 : 0;
}
