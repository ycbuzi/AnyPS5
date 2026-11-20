std::string ComputeTest(ID3D12Device* device, const unsigned char* code, size_t size) {
    char buffer[160];
    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rootDesc{1, &parameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    com_ptr<ID3DBlob> blob, error;
    HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, blob.put(), error.put());
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"rootsig\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    com_ptr<ID3D12RootSignature> root;
    hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(root.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"create_rootsig\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = root.get();
    psoDesc.CS = {code, size};
    com_ptr<ID3D12PipelineState> pso;
    hr = device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(pso.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"pso\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    D3D12_HEAP_PROPERTIES defaultHeap{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_HEAP_PROPERTIES readbackHeap{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    com_ptr<ID3D12Resource> target, readback;
    hr = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(target.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"buffer\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    hr = device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.put()));
    if (FAILED(hr)) { std::snprintf(buffer, sizeof buffer, "{\"stage\":\"readback\",\"hr\":\"0x%08lx\"}", hr); return buffer; }
    D3D12_COMMAND_QUEUE_DESC queueDesc{D3D12_COMMAND_LIST_TYPE_COMPUTE};
    com_ptr<ID3D12CommandQueue> queue;
    com_ptr<ID3D12CommandAllocator> allocator;
    com_ptr<ID3D12GraphicsCommandList> list;
    com_ptr<ID3D12Fence> fence;
    device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(queue.put()));
    device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE, IID_PPV_ARGS(allocator.put()));
    device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, allocator.get(), pso.get(), IID_PPV_ARGS(list.put()));
    device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.put()));
    if (!queue || !list || !fence) return "{\"stage\":\"queue\"}";
    list->SetComputeRootSignature(root.get());
    list->SetComputeRootUnorderedAccessView(0, target->GetGPUVirtualAddress());
    list->Dispatch(1, 1, 1);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = target.get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    list->ResourceBarrier(1, &barrier);
    list->CopyResource(readback.get(), target.get());
    list->Close();
    ID3D12CommandList* lists[] = {list.get()};
    queue->ExecuteCommandLists(1, lists);
    queue->Signal(fence.get(), 1);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    fence->SetEventOnCompletion(1, event);
    const DWORD wait = WaitForSingleObject(event, 5000);
    CloseHandle(event);
    if (wait != WAIT_OBJECT_0) return "{\"stage\":\"wait\"}";
    std::uint32_t* values = nullptr;
    D3D12_RANGE range{0, 256};
    if (FAILED(readback->Map(0, &range, reinterpret_cast<void**>(&values)))) return "{\"stage\":\"map\"}";
    std::snprintf(buffer, sizeof buffer, "{\"stage\":\"ok\",\"v0\":%u,\"v1\":%u,\"v63\":%u}", values[0], values[1], values[63]);
    readback->Unmap(0, nullptr);
    return buffer;
}

