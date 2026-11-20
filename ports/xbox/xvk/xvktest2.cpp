#define VK_NO_PROTOTYPES
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include "t2/shaders.inc"

#define L(name) auto name = reinterpret_cast<PFN_##name>(gipa(inst, #name)); if (!name) { std::printf("missing %s\n", #name); return 2; }

int main(int argc, char** argv) {
    HMODULE dll = LoadLibraryA(argc > 1 ? argv[1] : "xvk.dll");
    if (!dll) { std::printf("cannot load xvk.dll\n"); return 2; }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(dll, "vkGetInstanceProcAddr"));
    VkInstance inst = nullptr;
    L(vkCreateInstance)
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO, nullptr, "xvktest2", 1, "xvk", 1, VK_API_VERSION_1_1};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, nullptr, 0, &app};
    if (vkCreateInstance(&ici, nullptr, &inst)) { std::printf("instance failed\n"); return 2; }
    L(vkEnumeratePhysicalDevices) L(vkGetPhysicalDeviceProperties) L(vkCreateDevice) L(vkGetDeviceQueue) L(vkAllocateMemory) L(vkMapMemory) L(vkCreateBuffer)
    L(vkGetBufferMemoryRequirements) L(vkBindBufferMemory) L(vkCreateImage) L(vkGetImageMemoryRequirements) L(vkBindImageMemory) L(vkCreateImageView)
    L(vkCreateCommandPool) L(vkAllocateCommandBuffers) L(vkBeginCommandBuffer) L(vkEndCommandBuffer) L(vkCmdCopyImageToBuffer) L(vkCmdCopyBuffer) L(vkCmdPipelineBarrier)
    L(vkQueueSubmit) L(vkQueueWaitIdle) L(vkCreateShaderModule) L(vkCreateDescriptorSetLayout) L(vkCreatePipelineLayout) L(vkCreateComputePipelines)
    L(vkCreateGraphicsPipelines) L(vkCreateDescriptorPool) L(vkAllocateDescriptorSets) L(vkUpdateDescriptorSets) L(vkCmdBindPipeline) L(vkCmdBindDescriptorSets)
    L(vkCmdPushConstants) L(vkCmdDispatch) L(vkCreateRenderPass) L(vkCreateFramebuffer) L(vkCmdBeginRenderPass) L(vkCmdEndRenderPass) L(vkCmdDraw)
    L(vkCmdSetViewport) L(vkCmdSetScissor)
    uint32_t n = 8;
    VkPhysicalDevice pds[8];
    vkEnumeratePhysicalDevices(inst, &n, pds);
    int failures = 0;
    for (uint32_t gi = 0; gi < n; ++gi) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(pds[gi], &props);
        float prio = 1;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, nullptr, 0, 0, 1, &prio};
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, nullptr, 0, 1, &qci};
        VkDevice dev;
        if (vkCreateDevice(pds[gi], &dci, nullptr, &dev)) { ++failures; continue; }
        VkQueue q;
        vkGetDeviceQueue(dev, 0, 0, &q);
        auto buffer = [&](VkDeviceSize size, uint32_t type, void** map) {
            VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr, 0, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT};
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
        auto module = [&](const unsigned char* code, size_t size) {
            VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, size, reinterpret_cast<const uint32_t*>(code)};
            VkShaderModule m;
            vkCreateShaderModule(dev, &si, nullptr, &m);
            return m;
        };
        const uint32_t W = 64, H = 64, N = 256;
        void* hostOut = nullptr;
        void* hostUniform = nullptr;
        VkBuffer storage = buffer(N * 4, 0, nullptr);
        VkBuffer readback = buffer(W * H * 4 + N * 4, 1, &hostOut);
        VkBuffer uniform = buffer(256, 1, &hostUniform);
        const float color[4] = {0.f, 1.f, 0.f, 1.f};
        std::memcpy(hostUniform, color, 16);
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr, 0, VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {W, H, 1}, 1, 1, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT};
        VkImage img;
        vkCreateImage(dev, &ii, nullptr, &img);
        VkMemoryRequirements ir;
        vkGetImageMemoryRequirements(dev, img, &ir);
        VkMemoryAllocateInfo iai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, nullptr, ir.size, 0};
        VkDeviceMemory imem;
        vkAllocateMemory(dev, &iai, nullptr, &imem);
        vkBindImageMemory(dev, img, imem, 0);
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr, 0, img, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, {}, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
        VkImageView view;
        vkCreateImageView(dev, &vi, nullptr, &view);
        VkDescriptorSetLayoutBinding bindings[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_ALL}, {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_ALL}};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, nullptr, 0, 2, bindings};
        VkDescriptorSetLayout sl;
        vkCreateDescriptorSetLayout(dev, &li, nullptr, &sl);
        VkPushConstantRange pr{VK_SHADER_STAGE_ALL, 0, 16};
        VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, nullptr, 0, 1, &sl, 1, &pr};
        VkPipelineLayout pl;
        vkCreatePipelineLayout(dev, &pli, nullptr, &pl);
        VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
        VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, nullptr, 0, 1, 1, &ps};
        VkDescriptorPool pool;
        vkCreateDescriptorPool(dev, &dpi, nullptr, &pool);
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr, pool, 1, &sl};
        VkDescriptorSet set;
        vkAllocateDescriptorSets(dev, &dai, &set);
        VkDescriptorBufferInfo sbi{storage, 0, VK_WHOLE_SIZE}, ubi{uniform, 0, 16};
        VkWriteDescriptorSet w[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &sbi},
                                     {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ubi}};
        vkUpdateDescriptorSets(dev, 2, w, 0, nullptr);
        VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, nullptr, 0, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, module(kComp, sizeof kComp), "main"}, pl};
        VkPipeline cp;
        vkCreateComputePipelines(dev, nullptr, 1, &cpi, nullptr, &cp);
        VkAttachmentDescription ad{0, VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                   VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
        VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sd{0, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, nullptr, 1, &ar};
        VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO, nullptr, 0, 1, &ad, 1, &sd};
        VkRenderPass rp;
        vkCreateRenderPass(dev, &rpi, nullptr, &rp);
        VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO, nullptr, 0, rp, 1, &view, W, H, 1};
        VkFramebuffer fb;
        vkCreateFramebuffer(dev, &fbi, nullptr, &fb);
        VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, module(kVert, sizeof kVert), "main"},
                                                     {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, module(kFrag, sizeof kFrag), "main"}};
        VkPipelineVertexInputStateCreateInfo vis{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ias{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, nullptr, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkPipelineViewportStateCreateInfo vps{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, nullptr, 1, nullptr};
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, nullptr, 0, VK_SAMPLE_COUNT_1_BIT};
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, nullptr, 0, VK_FALSE, VK_LOGIC_OP_COPY, 1, &ba};
        VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dys{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, nullptr, 0, 2, dyn};
        VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, nullptr, 0, 2, stages, &vis, &ias, nullptr, &vps, &rs, &ms, nullptr, &cbs, &dys, pl, rp, 0};
        VkPipeline gp;
        vkCreateGraphicsPipelines(dev, nullptr, 1, &gpi, nullptr, &gp);
        VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        VkCommandPool cpool;
        vkCreateCommandPool(dev, &cpci, nullptr, &cpool);
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, cpool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(dev, &cai, &cb);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cb, &bi);
        const uint32_t k = 3;
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cp);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_ALL, 0, 4, &k);
        vkCmdDispatch(cb, N / 64, 1, 1);
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        VkBufferCopy bc{0, W * H * 4, N * 4};
        vkCmdCopyBuffer(cb, storage, readback, 1, &bc);
        VkClearValue clear{};
        clear.color.float32[0] = 1;
        clear.color.float32[3] = 1;
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, nullptr, rp, fb, {{0, 0}, {W, H}}, 1, &clear};
        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, float(W), float(H), 0, 1};
        VkRect2D sc{{0, 0}, {W, H}};
        vkCmdSetViewport(cb, 0, 1, &vp);
        vkCmdSetScissor(cb, 0, 1, &sc);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &set, 0, nullptr);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);
        VkBufferImageCopy ic{0, 0, 0, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, {0, 0, 0}, {W, H, 1}};
        vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &ic);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO, nullptr, 0, nullptr, nullptr, 1, &cb};
        vkQueueSubmit(q, 1, &si, nullptr);
        vkQueueWaitIdle(q);
        const uint32_t* px = static_cast<const uint32_t*>(hostOut);
        const uint32_t* cv = px + W * H;
        bool computeOk = true;
        for (uint32_t i = 0; i < N; ++i) if (cv[i] != i * k) { computeOk = false; std::printf("  compute v[%u]=%u want %u\n", i, cv[i], i * k); break; }
        const uint32_t tl = px[2 * W + 2], br = px[(H - 3) * W + (W - 3)];
        const bool drawOk = tl == 0xFF00FF00u && br == 0xFF0000FFu;
        std::printf("%s: compute %s, draw %s (top-left %08x bottom-right %08x)\n", props.deviceName, computeOk ? "ok" : "FAIL", drawOk ? "ok" : "FAIL", tl, br);
        if (!computeOk || !drawOk) ++failures;
    }
    std::printf(failures ? "XVK2 FAIL\n" : "XVK2 PASS\n");
    return failures ? 1 : 0;
}
