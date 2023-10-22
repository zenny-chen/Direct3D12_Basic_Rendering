// Direct3D12_BasicRendering.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstdarg>
#include <cerrno>

#define _USE_MATH_DEFINES
#include <math.h>
#include <cmath>
#include <limits>
#include <algorithm>
#include <utility>

#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <d3dcompiler.h>

static constexpr UINT MAX_HARDWARE_ADAPTER_COUNT = 16;
static constexpr UINT TOTAL_FRAME_COUNT = 5;
static constexpr int WINDOW_WIDTH = 640;
static constexpr int WINDOW_HEIGHT = 640;

static IDXGIFactory4* s_factory = nullptr;
static ID3D12Device* s_device = nullptr;
static ID3D12CommandQueue* s_commandQueue = nullptr;
static ID3D12CommandAllocator* s_commandAllocator = nullptr;
static ID3D12CommandAllocator* s_commandBundleAllocator = nullptr;
static IDXGISwapChain3* s_swapChain = nullptr;
static ID3D12DescriptorHeap* s_rtvDescriptorHeap = nullptr;
static UINT s_rtvDescriptorSize = 0;
static ID3D12RootSignature* s_rootSignature = nullptr;
static ID3D12PipelineState* s_basicPipelineState = nullptr;
static ID3D12GraphicsCommandList* s_basicCommandList = nullptr;
static ID3D12GraphicsCommandList* s_basicCommandBundle = nullptr;
static ID3D12Resource* s_renderTargets[TOTAL_FRAME_COUNT]{ };
static ID3D12Resource* s_vertexBuffer = nullptr;

// Synchronization objects.
static UINT s_currFrameIndex = 0;
static HANDLE s_hFenceEvent = nullptr;
static ID3D12Fence* s_fence = nullptr;
static UINT64 s_fenceValue = 0;

static D3D_FEATURE_LEVEL s_maxFeatureLevel = D3D_FEATURE_LEVEL_1_0_CORE;
static D3D_SHADER_MODEL s_highestShaderModel = D3D_SHADER_MODEL_5_1;
static UINT s_waveSize = 0;
static UINT s_maxSIMDSize = 0;

static POINT s_wndMinsize{ };       // minimum window size
static const char s_appName[] = "Direct3D 12 Basic Rendering";
static float s_rotateAngle = 0.0f;

static auto TransWStrToString(char dstBuf[], const WCHAR srcBuf[]) -> void
{
    if (dstBuf == nullptr || srcBuf == nullptr) return;

    const int len = WideCharToMultiByte(CP_UTF8, 0, srcBuf, -1, NULL, 0, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, srcBuf, -1, dstBuf, len, NULL, NULL);
    dstBuf[len] = '\0';
}

static auto CreateCompiledShaderObjectFromPath(const char csoPath[]) -> D3D12_SHADER_BYTECODE
{
    D3D12_SHADER_BYTECODE result{ };
    FILE* fp = nullptr;
    auto const err = fopen_s(&fp, csoPath, "rb");
    if (err != 0 || fp == nullptr)
    {
        fprintf(stderr, "Read compiled shader object file: `%s` failed: %d\n", csoPath, err);
        if (fp != nullptr) {
            fclose(fp);
        }
        return result;
    }

    fseek(fp, 0, SEEK_END);
    const size_t fileSize = (size_t)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const size_t codeElemCount = (fileSize + sizeof(uint32_t)) / sizeof(uint32_t);
    uint32_t* csoBlob = (uint32_t*)calloc(codeElemCount, sizeof(uint32_t));
    if (csoBlob == nullptr)
    {
        fprintf(stderr, "Lack of system memory to allocate memory for `%s` CSO object!\n", csoPath);
        return result;
    }
    if (fread(csoBlob, 1, fileSize, fp) < 1) {
        printf("WARNING: Read compiled shader object file `%s` error!\n", csoPath);
    }
    fclose(fp);

    result.pShaderBytecode = csoBlob;
    result.BytecodeLength = fileSize;
    return result;
}

static auto QueryDeviceSupportedMaxFeatureLevel() -> bool
{
    const D3D_FEATURE_LEVEL requestedLevels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_2
    };
    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{
        .NumFeatureLevels = sizeof(requestedLevels) / sizeof(requestedLevels[0]),
        .pFeatureLevelsRequested = requestedLevels
    };

    auto const hRes = s_device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CheckFeatureSupport for `D3D12_FEATURE_FEATURE_LEVELS` failed: %ld\n", hRes);
        return false;
    }

    s_maxFeatureLevel = featureLevels.MaxSupportedFeatureLevel;

    char strBuf[32]{ };
    switch (s_maxFeatureLevel)
    {
    case D3D_FEATURE_LEVEL_1_0_CORE:
        strcpy_s(strBuf, "1.0 core");
        break;

    case D3D_FEATURE_LEVEL_9_1:
        strcpy_s(strBuf, "9.1");
        break;

    case D3D_FEATURE_LEVEL_9_2:
        strcpy_s(strBuf, "9.2");
        break;

    case D3D_FEATURE_LEVEL_9_3:
        strcpy_s(strBuf, "9.3");
        break;

    case D3D_FEATURE_LEVEL_10_0:
        strcpy_s(strBuf, "10.0");
        break;

    case D3D_FEATURE_LEVEL_10_1:
        strcpy_s(strBuf, "10.1");
        break;

    case D3D_FEATURE_LEVEL_11_0:
        strcpy_s(strBuf, "11.0");
        break;

    case D3D_FEATURE_LEVEL_11_1:
        strcpy_s(strBuf, "11.1");
        break;

    case D3D_FEATURE_LEVEL_12_0:
        strcpy_s(strBuf, "12.0");
        break;

    case D3D_FEATURE_LEVEL_12_1:
        strcpy_s(strBuf, "12.1");
        break;

    case D3D_FEATURE_LEVEL_12_2:
        strcpy_s(strBuf, "12.2");
        break;

    default:
        break;
    }

    printf("Current device supports max feature level: %s\n", strBuf);
    return true;
}

static auto QueryDeviceShaderModel() -> bool
{
    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{ .HighestShaderModel = D3D_SHADER_MODEL_6_7 };
    auto const hRes = s_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CheckFeatureSupport for `D3D12_FEATURE_SHADER_MODEL` failed: %ld\n", hRes);
        return false;
    }

    s_highestShaderModel = shaderModel.HighestShaderModel;

    const int minor = s_highestShaderModel & 0x0f;
    const int major = s_highestShaderModel >> 4;
    printf("Current device support highest shader model: %d.%d\n", major, minor);

    return true;
}

static auto QueryRootSignatureVersion() -> bool
{
    D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSignature{ .HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1 };
    auto const hRes = s_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rootSignature, sizeof(rootSignature));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CheckFeatureSupport for `D3D12_FEATURE_DATA_ROOT_SIGNATURE` failed: %ld\n", hRes);
        return false;
    }

    const char* signatureVersion = "1.0";

    switch (rootSignature.HighestVersion)
    {
    case D3D_ROOT_SIGNATURE_VERSION_1_0:
    default:
        break;

    case D3D_ROOT_SIGNATURE_VERSION_1_1:
        signatureVersion = "1.1";
        break;
    }

    printf("Current device supports highest root signature version: %s\n", signatureVersion);
    return true;
}

static auto QueryDeviceArchitecture(const UINT& selectedAdapterIndex) -> bool
{
    D3D12_FEATURE_DATA_ARCHITECTURE1 architecture{ .NodeIndex = 0 };
    auto hRes = s_device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CheckFeatureSupport for `D3D12_FEATURE_DATA_ARCHITECTURE1` failed: %ld\n", hRes);
        return false;
    }

    printf("Current device has tile based renderer? %s\n", architecture.TileBasedRenderer ? "YES" : "NO");
    printf("Current device supports Unified Memory Access? %s\n", architecture.UMA ? "YES" : "NO");
    printf("Current device supports Cache-Coherent Unified Memory Access? %s\n", architecture.CacheCoherentUMA ? "YES" : "NO");
    printf("Current device supports Isolated Memory Management Unit? %s\n", architecture.IsolatedMMU ? "YES" : "NO");

    D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT gpuVAS{ };
    hRes = s_device->CheckFeatureSupport(D3D12_FEATURE_GPU_VIRTUAL_ADDRESS_SUPPORT, &gpuVAS, sizeof(gpuVAS));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CheckFeatureSupport for `D3D12_FEATURE_DATA_GPU_VIRTUAL_ADDRESS_SUPPORT` failed: %ld\n", hRes);
        return false;
    }

    printf("Current device maximum GPU virtual address bits per resource: %u\n", gpuVAS.MaxGPUVirtualAddressBitsPerResource);
    printf("Current device maximum GPU virtual address bits per process: %u\n", gpuVAS.MaxGPUVirtualAddressBitsPerProcess);

    return true;
}

static auto QueryDeviceBasicFeatures() -> bool
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{ };
    auto const hRes = s_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CheckFeatureSupport for `D3D12_FEATURE_D3D12_OPTIONS` failed: %ld\n", hRes);
        return false;
    }

    printf("Current device supports double-precision float shader ops: %s\n", options.DoublePrecisionFloatShaderOps ? "YES" : "NO");
    printf("Current device supports output merger logic op: %s\n", options.OutputMergerLogicOp ? "YES" : "NO");

    const char* descStr = "";
    switch (options.MinPrecisionSupport)
    {
    case D3D12_SHADER_MIN_PRECISION_SUPPORT_NONE:
    default:
        descStr = "none";
        break;

    case D3D12_SHADER_MIN_PRECISION_SUPPORT_10_BIT:
        descStr = "10-bit";
        break;

    case D3D12_SHADER_MIN_PRECISION_SUPPORT_16_BIT:
        descStr = "16-bit";
        break;
    }
    printf("Current device supports minimum precision: %s\n", descStr);

    printf("Current device supports tiled resource tier: %d\n", options.TiledResourcesTier);
    printf("Current device supports resource binding tier: %d\n", options.ResourceBindingTier);
    printf("Current device supports pixel shader stencil ref: %s\n", options.PSSpecifiedStencilRefSupported ? "YES" : "NO");
    printf("Current device supports the loading of additional formats for typed unordered-access views (UAVs): %s\n", options.TypedUAVLoadAdditionalFormats ? "YES" : "NO");
    printf("Current device supports Rasterizer Order Views: %s\n", options.ROVsSupported ? "YES" : "NO");
    printf("Current device supports conservative rasterization tier: %d\n", options.ConservativeRasterizationTier);
    printf("Current device supports 64KB standard swizzle pattern: %s\n", options.StandardSwizzle64KBSupported ? "YES" : "NO");
    printf("Current device supports resource heap tier: %d\n", options.ResourceHeapTier);

    return true;
}

static auto QueryDeviceWaveOps() -> bool
{
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 waveOptions{ };
    auto const hRes = s_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &waveOptions, sizeof(waveOptions));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CheckFeatureSupport for `D3D12_FEATURE_D3D12_OPTIONS1` failed: %ld\n", hRes);
        return false;
    }

    if (!waveOptions.WaveOps) {
        puts("Current device does not support HLSL 6.0 wave operations.");
    }
    else
    {
        s_waveSize = waveOptions.WaveLaneCountMin;
        printf("Current device baseline number of lanes in the SIMD wave: %u\n", s_waveSize);
    }

    s_maxSIMDSize = waveOptions.TotalLaneCount;
    printf("Current device total number of SIMD lanes: %u\n", s_maxSIMDSize);

    printf("Current device supports Int64 shader ops: %s\n", waveOptions.Int64ShaderOps ? "YES" : "NO");

    return true;
}

static auto CreateD3D12Device() -> bool
{
    HRESULT hRes = S_OK;

#if defined(DEBUG) || defined(_DEBUG)
    // In debug mode, we're going to enable the debug layer
    ID3D12Debug* debugController = nullptr;
    hRes = D3D12GetDebugInterface(IID_PPV_ARGS(&debugController));
    if (FAILED(hRes) || debugController == nullptr) {
        printf("WARNING: D3D12GetDebugInterface failed: %ld. So debug layer will not be enabled!\n", hRes);
    }
    else {
        debugController->EnableDebugLayer();
    }
#endif // DEBUG

    hRes = CreateDXGIFactory1(IID_PPV_ARGS(&s_factory));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CreateDXGIFactory1 failed: %ld\n", hRes);
        return false;
    }

    // Enumerate the adapters (video cards)
    IDXGIAdapter1* hardwareAdapters[MAX_HARDWARE_ADAPTER_COUNT]{ };
    UINT foundAdapterCount;
    for (foundAdapterCount = 0; foundAdapterCount < MAX_HARDWARE_ADAPTER_COUNT; ++foundAdapterCount)
    {
        hRes = s_factory->EnumAdapters1(foundAdapterCount, &hardwareAdapters[foundAdapterCount]);
        if (FAILED(hRes))
        {
            if (hRes != DXGI_ERROR_NOT_FOUND) {
                printf("WARNING: Some error occurred during enumerating adapters: %ld\n", hRes);
            }
            break;
        }
    }
    if (foundAdapterCount == 0)
    {
        fprintf(stderr, "There are no Direct3D capable adapters found on the current platform...\n");
        return false;
    }

    printf("Found %u Direct3D capable device%s in all.\n", foundAdapterCount, foundAdapterCount > 1 ? "s" : "");

    DXGI_ADAPTER_DESC1 adapterDesc{ };
    char strBuf[512]{ };
    for (UINT i = 0; i < foundAdapterCount; ++i)
    {
        hardwareAdapters[i]->GetDesc1(&adapterDesc);
        TransWStrToString(strBuf, adapterDesc.Description);
        printf("Adapter[%u]: %s\n", i, strBuf);
    }
    printf("Please Choose which adapter to use: ");

    gets_s(strBuf);

    char* endChar = nullptr;
    long selectedAdapterIndex = std::strtol(strBuf, &endChar, 10);
    if (selectedAdapterIndex < 0 || selectedAdapterIndex >= long(foundAdapterCount))
    {
        puts("WARNING: The index you input exceeds the range of available adatper count. So adatper[0] will be used!");
        selectedAdapterIndex = 0;
    }

    hardwareAdapters[selectedAdapterIndex]->GetDesc1(&adapterDesc);
    TransWStrToString(strBuf, adapterDesc.Description);

    printf("\nYou have chosen adapter[%ld]\n", selectedAdapterIndex);
    printf("Adapter description: %s\n", strBuf);
    printf("Dedicated Video Memory: %.1f GB\n", double(adapterDesc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0));
    printf("Dedicated System Memory: %.1f GB\n", double(adapterDesc.DedicatedSystemMemory) / (1024.0 * 1024.0 * 1024.0));
    printf("Shared System Memory: %.1f GB\n", double(adapterDesc.SharedSystemMemory) / (1024.0 * 1024.0 * 1024.0));

    puts("\n================================================\n");

    hRes = D3D12CreateDevice(hardwareAdapters[selectedAdapterIndex], D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&s_device));
    if (FAILED(hRes))
    {
        fprintf(stderr, "D3D12CreateDevice failed: %ld\n", hRes);
        return false;
    }

    if (!QueryDeviceSupportedMaxFeatureLevel()) return false;
    if (!QueryDeviceShaderModel()) return false;
    if (!QueryRootSignatureVersion()) return false;
    if (!QueryDeviceArchitecture(UINT(selectedAdapterIndex))) return false;
    if (!QueryDeviceBasicFeatures()) return false;
    if (!QueryDeviceWaveOps()) return false;

    return true;
}

static auto CreateCommandQueue() -> bool
{
    const D3D12_COMMAND_QUEUE_DESC queueDesc{
        .Type = D3D12_COMMAND_LIST_TYPE_DIRECT,
        .Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
        .Flags = D3D12_COMMAND_QUEUE_FLAG_NONE,
        .NodeMask = 0
    };

    HRESULT hRes = s_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&s_commandQueue));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CreateCommandQueue failed: %ld\n", hRes);
        return false;
    }

    hRes = s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&s_commandAllocator));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CreateCommandAllocator for command list failed: %ld\n", hRes);
        return false;
    }

    hRes = s_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_BUNDLE, IID_PPV_ARGS(&s_commandBundleAllocator));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CreateCommandAllocator for command bundle failed: %ld\n", hRes);
        return false;
    }

    return true;
}

static auto CreateSwapChain(HWND hWnd) -> bool
{
    DXGI_SWAP_CHAIN_DESC swapChainDesc{
        .BufferDesc = {.Width = WINDOW_WIDTH, .Height = WINDOW_HEIGHT,
                        .RefreshRate = {.Numerator = 60, .Denominator = 1 },    // refresh rate of 60 FPS
                        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                        .ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, .Scaling = DXGI_MODE_SCALING_UNSPECIFIED },
        .SampleDesc = {.Count = 1, .Quality = 0 },
        .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
        .BufferCount = TOTAL_FRAME_COUNT,
        .OutputWindow = hWnd,
        .Windowed = TRUE,
        .SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
        .Flags = 0
    };

    IDXGISwapChain* swapChain = nullptr;
    HRESULT hRes = s_factory->CreateSwapChain(s_commandQueue, &swapChainDesc, &swapChain);
    if (FAILED(hRes) || swapChain == nullptr)
    {
        fprintf(stderr, "CreateSwapChain failed: %ld\n", hRes);
        return false;
    }

    s_swapChain = (IDXGISwapChain3*)swapChain;

    s_currFrameIndex = s_swapChain->GetCurrentBackBufferIndex();

    return true;
}

static auto CreateRenderTargetViews() -> bool
{
    // Describe and create a render target view (RTV) descriptor heap.
    const D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{
        .Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
        .NumDescriptors = TOTAL_FRAME_COUNT,
        .Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE,
        .NodeMask = 0
    };
    HRESULT hRes = s_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&s_rtvDescriptorHeap));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CreateDescriptorHeap for render target view failed: %ld\n", hRes);
        return false;
    }

    s_rtvDescriptorSize = s_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // Create frame resources
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = s_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_DESCRIPTOR_HEAP_DESC currDesc = s_rtvDescriptorHeap->GetDesc();

    // Create a RTV for each frame.
    for (UINT i = 0; i < TOTAL_FRAME_COUNT; ++i)
    {
        hRes = s_swapChain->GetBuffer(i, IID_PPV_ARGS(&s_renderTargets[i]));
        if (FAILED(hRes))
        {
            fprintf(stderr, "GetBuffer for render target [%u] failed!\n", i);
            return false;
        }

        s_device->CreateRenderTargetView(s_renderTargets[i], NULL, rtvHandle);
        rtvHandle.ptr += s_rtvDescriptorSize;
    }

    return true;
}

static auto CreateRootSignature() -> bool
{
    const D3D12_ROOT_PARAMETER rootParameter{
        // Use constants directly in the root signature
        .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
        .Constants {
            .ShaderRegister = 0,
            .RegisterSpace = 0,
            .Num32BitValues = 1
        },
        // This constant buffer will just be accessed in a vertex shader
        .ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX
    };

    // Create an empty root signature.
    const D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {
        .NumParameters = 1,
        .pParameters = &rootParameter,
        .NumStaticSamplers = 0,
        .pStaticSamplers = nullptr,
        .Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
    };

    ID3DBlob* signature = nullptr;
    ID3DBlob* error = nullptr;
    HRESULT hRes = D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);
    do
    {
        if (FAILED(hRes))
        {
            fprintf(stderr, "D3D12SerializeRootSignature failed: %ld\n", hRes);
            break;
        }

        hRes = s_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&s_rootSignature));
        if (FAILED(hRes))
        {
            fprintf(stderr, "CreateRootSignature failed: %ld\n", hRes);
            break;
        }
    } 
    while (false);

    if (signature != nullptr) {
        signature->Release();
    }
    if (error != nullptr) {
        error->Release();
    }
    
    if (FAILED(hRes)) return false;

    return true;
}

static auto CreateFenceAndEvent() -> bool
{
    HRESULT hRes = s_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&s_fence));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CreateFence failed: %ld\n", hRes);
        return false;
    }

    // Create an event handle to use for frame synchronization.
    s_hFenceEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (s_hFenceEvent == nullptr)
    {
        hRes = HRESULT_FROM_WIN32(GetLastError());
        if (FAILED(hRes)) return false;
    }

    return true;
}

// 等待上一帧处理完成
static auto WaitForPreviousFrame(void) -> bool
{
    // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
    // This is code implemented as such for simplicity. The D3D12HelloFrameBuffering
    // sample illustrates how to use fences for efficient resource usage and to
    // maximize GPU utilization.

    // Signal and increment the fence value.
    auto const fence = ++s_fenceValue;
    HRESULT hRes = s_commandQueue->Signal(s_fence, fence);
    if (FAILED(hRes)) return false;

    // Wait until the previous frame is finished.
    if (s_fence->GetCompletedValue() != fence)
    {
        hRes = s_fence->SetEventOnCompletion(fence, s_hFenceEvent);
        if (FAILED(hRes)) return false;

        WaitForSingleObject(s_hFenceEvent, INFINITE);
    }

    s_currFrameIndex = s_swapChain->GetCurrentBackBufferIndex();

    return true;
}

static auto CreateBasicPipelineStateObject() -> bool
{
    D3D12_SHADER_BYTECODE vertexShaderObj = CreateCompiledShaderObjectFromPath("shaders/basic.vert.cso");
    D3D12_SHADER_BYTECODE pixelShaderObj = CreateCompiledShaderObjectFromPath("shaders/basic.frag.cso");

    bool done = false;
    do
    {
        if (vertexShaderObj.pShaderBytecode == nullptr || vertexShaderObj.BytecodeLength == 0) break;
        if (pixelShaderObj.pShaderBytecode == nullptr || pixelShaderObj.BytecodeLength == 0) break;

        // Define the vertex input layout.
        const D3D12_INPUT_ELEMENT_DESC inputElementDescs[]{
            { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };

        // Describe and create the graphics pipeline state object (PSO).
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{
            .pRootSignature = s_rootSignature,
            .VS = vertexShaderObj,
            .PS = pixelShaderObj,
            .BlendState {
                .AlphaToCoverageEnable = FALSE,
                .IndependentBlendEnable = FALSE,
                .RenderTarget {
                // RenderTarget[0]
                {
                    .BlendEnable = FALSE,
                    .LogicOpEnable = FALSE,
                    .SrcBlend = D3D12_BLEND_SRC_ALPHA,
                    .DestBlend = D3D12_BLEND_INV_SRC_ALPHA,
                    .BlendOp = D3D12_BLEND_OP_ADD,
                    .SrcBlendAlpha = D3D12_BLEND_ONE,
                    .DestBlendAlpha = D3D12_BLEND_ZERO,
                    .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                    .LogicOp = D3D12_LOGIC_OP_NOOP,
                    .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL
                }
            }
        },
        .SampleMask = UINT32_MAX,
            // Use the default rasterizer state
            .RasterizerState {
                .FillMode = D3D12_FILL_MODE_SOLID,
                .CullMode = D3D12_CULL_MODE_BACK,
                .FrontCounterClockwise = FALSE,
                .DepthBias = 0,
                .DepthBiasClamp = 0.0f,
                .SlopeScaledDepthBias = 0.0f,
                .DepthClipEnable = TRUE,
                .MultisampleEnable = FALSE,
                .AntialiasedLineEnable = FALSE,
                .ForcedSampleCount = 0,
                .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF
            },
            .DepthStencilState {
                .DepthEnable = FALSE,
                .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
                .DepthFunc = D3D12_COMPARISON_FUNC_NEVER,
                .StencilEnable = FALSE,
                .StencilReadMask = 0,
                .StencilWriteMask = 0,
                .FrontFace {},
                .BackFace { }
            },
            .InputLayout {
                .pInputElementDescs = inputElementDescs,
                .NumElements = (UINT)std::size(inputElementDescs)
            },
            .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
            .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
            .NumRenderTargets = 1,
            .RTVFormats {
                // RTVFormats[0]
                { DXGI_FORMAT_R8G8B8A8_UNORM }
            },
            .DSVFormat = DXGI_FORMAT_UNKNOWN,
            .SampleDesc {
                .Count = 1,
                .Quality = 0
            },
            .NodeMask = 0,
            .CachedPSO { },
            .Flags = D3D12_PIPELINE_STATE_FLAG_NONE
        };

        HRESULT hRes = s_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&s_basicPipelineState));
        if (FAILED(hRes))
        {
            fprintf(stderr, "CreateGraphicsPipelineState for basic PSO failed: %ld\n", hRes);
            break;
        }

        hRes = s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, s_commandAllocator, s_basicPipelineState, IID_PPV_ARGS(&s_basicCommandList));
        if (FAILED(hRes))
        {
            fprintf(stderr, "CreateCommandList for basic PSO failed: %ld\n", hRes);
            break;
        }

        hRes = s_basicCommandList->Close();
        if (FAILED(hRes))
        {
            fprintf(stderr, "Close basic command list failed: %ld\n", hRes);
            break;
        }

        hRes = s_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_BUNDLE, s_commandBundleAllocator, s_basicPipelineState, IID_PPV_ARGS(&s_basicCommandBundle));
        if (FAILED(hRes))
        {
            fprintf(stderr, "CreateCommandList for command bundle failed: %ld\n", hRes);
            break;
        }

        done = true;
    } while (false);

    if (vertexShaderObj.pShaderBytecode != nullptr) {
        free((void*)vertexShaderObj.pShaderBytecode);
    }
    if (pixelShaderObj.pShaderBytecode != nullptr) {
        free((void*)pixelShaderObj.pShaderBytecode);
    }

    return done;
}

static auto CreateVertexBuffer() -> bool
{
    const struct Vertex
    {
        float position[4];
        float color[4];
    } squareVertices[]{
        // Direct3D是以左手作为前面背面顶点排列的依据
        {.position { -0.75f, 0.75f, 0.0f, 1.0f }, .color { 0.9f, 0.1f, 0.1f, 1.0f } },     // top left
        {.position { 0.75f, 0.75f, 0.0f, 1.0f }, .color { 0.9f, 0.9f, 0.1f, 1.0f } },      // top right
        {.position { -0.75f, -0.75f, 0.0f, 1.0f }, .color { 0.1f, 0.9f, 0.1f, 1.0f } },    // bottom left
        {.position { 0.75f, -0.75f, 0.0f, 1.0f }, .color { 0.1f, 0.1f, 0.9f, 1.0f } }      // bottom right
    };

    // Note: using upload heaps to transfer static data like vert buffers is not 
    // recommended. Every time the GPU needs it, the upload heap will be marshalled 
    // over. Please read up on Default Heap usage. An upload heap is used here for 
    // code simplicity and because there are very few verts to actually transfer.
    const D3D12_HEAP_PROPERTIES vbHeapProperties{
        .Type = D3D12_HEAP_TYPE_UPLOAD,
        .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
        .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
        .CreationNodeMask = 1,
        .VisibleNodeMask = 1
    };
    const D3D12_RESOURCE_DESC vbResourceDesc{
        .Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
        .Alignment = 0,
        .Width = sizeof(squareVertices),
        .Height = 1U,
        .DepthOrArraySize = 1,
        .MipLevels = 1,
        .Format = DXGI_FORMAT_UNKNOWN,
        .SampleDesc {.Count = 1U, .Quality = 0 },
        .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
        .Flags = D3D12_RESOURCE_FLAG_NONE
    };

    HRESULT hRes = s_device->CreateCommittedResource(&vbHeapProperties, D3D12_HEAP_FLAG_NONE, &vbResourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&s_vertexBuffer));
    if (FAILED(hRes))
    {
        fprintf(stderr, "CreateCommittedResource for vertex buffer failed: %ld\n", hRes);
        return false;
    }

    // Copy the triangle data to the vertex buffer.
    void* pVertexDataBegin = nullptr;
    D3D12_RANGE readRange = { 0, 0 };   // We do not intend to read from this resource on the CPU.	
    hRes = s_vertexBuffer->Map(0, &readRange, &pVertexDataBegin);
    if (FAILED(hRes))
    {
        fprintf(stderr, "Map vertex buffer failed: %ld\n", hRes);
        return false;
    }

    memcpy(pVertexDataBegin, squareVertices, sizeof(squareVertices));
    s_vertexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView{
        .BufferLocation = s_vertexBuffer->GetGPUVirtualAddress(),
        .SizeInBytes = (uint32_t)sizeof(squareVertices),
        .StrideInBytes = sizeof(squareVertices[0])
    };

    // Record commands to the command list bundle.
    s_basicCommandBundle->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    s_basicCommandBundle->IASetVertexBuffers(0, 1, &vertexBufferView);
    s_basicCommandBundle->DrawInstanced((UINT)std::size(squareVertices), 1, 0, 0);

    // End of the record
    hRes = s_basicCommandBundle->Close();
    if (FAILED(hRes))
    {
        fprintf(stderr, "Close basic command bundle failed: %ld\n", hRes);
        return false;
    }

    // Wait for the command list to execute;
    // we are reusing the same command list in our main loop but for now,
    // we just want to wait for setup to complete before continuing.
    WaitForPreviousFrame();

    return true;
}

static auto PopulateCommandList() -> bool
{
    HRESULT hRes = s_commandAllocator->Reset();
    if (FAILED(hRes))
    {
        fprintf(stderr, "Reset command allocator failed: %ld\n", hRes);
        return false;
    }

    hRes = s_basicCommandList->Reset(s_commandAllocator, s_basicPipelineState);
    if (FAILED(hRes))
    {
        fprintf(stderr, "Reset basic command list failed: %ld\n", hRes);
        return false;
    }

    // Record commands to the command list
    // Set necessary state.
    const D3D12_VIEWPORT viewPort{
        .TopLeftX = 0.0f,
        .TopLeftY = 0.0f,
        .Width = FLOAT(WINDOW_WIDTH),
        .Height = FLOAT(WINDOW_HEIGHT),
        .MinDepth = 0.0f,
        .MaxDepth = 3.0f
    };
    s_basicCommandList->RSSetViewports(1, &viewPort);

    const D3D12_RECT scissorRect{
        .left = 0,
        .top = 0,
        .right = WINDOW_WIDTH,
        .bottom = WINDOW_HEIGHT
    };
    s_basicCommandList->RSSetScissorRects(1, &scissorRect);

    // Indicate that the back buffer will be used as a render target.
    const D3D12_RESOURCE_BARRIER renderBarrier = {
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition {
            .pResource = s_renderTargets[s_currFrameIndex],
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = D3D12_RESOURCE_STATE_PRESENT,
            .StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET
        }
    };
    s_basicCommandList->ResourceBarrier(1, &renderBarrier);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = s_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += size_t(s_currFrameIndex * s_rtvDescriptorSize);
    s_basicCommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    const float clearColor[] = { 0.5f, 0.6f, 0.5f, 1.0f };
    s_basicCommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // This can also be set into a command bundle.
    // Set it to this command list for update per frame
    s_basicCommandList->SetGraphicsRootSignature(s_rootSignature);

    const union { float f; UINT i; } rotAngleConstant{ .f = s_rotateAngle };
    s_basicCommandList->SetGraphicsRoot32BitConstant(0, rotAngleConstant.i, 0);

    // Execute the bundle to the command list
    s_basicCommandList->ExecuteBundle(s_basicCommandBundle);

    if (++s_rotateAngle >= 360.0f) {
        s_rotateAngle = 0.0f;
    }

    // Indicate that the back buffer will now be used to present.
    const D3D12_RESOURCE_BARRIER presentBarrier{
        .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
        .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
        .Transition {
            .pResource = s_renderTargets[s_currFrameIndex],
            .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
            .StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET,
            .StateAfter = D3D12_RESOURCE_STATE_PRESENT
        }
    };
    s_basicCommandList->ResourceBarrier(1, &presentBarrier);

    // End of the record
    hRes = s_basicCommandList->Close();
    if (FAILED(hRes))
    {
        fprintf(stderr, "Close basic command list failed: %ld\n", hRes);
        return false;
    }

    return true;
}

static auto Render() -> bool
{
    if (!PopulateCommandList()) return false;

    // Execute the command list.
    ID3D12CommandList* const ppCommandLists[] = { (ID3D12CommandList*)s_basicCommandList };
    s_commandQueue->ExecuteCommandLists((UINT)std::size(ppCommandLists), ppCommandLists);

    // Present the frame.
    HRESULT hRes = s_swapChain->Present(1, 0);
    if (FAILED(hRes))
    {
        fprintf(stderr, "Present failed: %ld\n", hRes);
        return false;
    }

    if (!WaitForPreviousFrame()) return false;

    return true;
}

static auto DestroyAllAssets() -> void
{
    if (s_hFenceEvent != nullptr)
    {
        CloseHandle(s_hFenceEvent);
        s_hFenceEvent = nullptr;
    }

    if (s_fence != nullptr)
    {
        s_fence->Release();
        s_fence = nullptr;
    }
    if (s_vertexBuffer != nullptr)
    {
        s_vertexBuffer->Release();
        s_vertexBuffer = nullptr;
    }
    if (s_basicCommandBundle != nullptr)
    {
        s_basicCommandBundle->Release();
        s_basicCommandBundle = nullptr;
    }
    if (s_basicCommandList != nullptr)
    {
        s_basicCommandList->Release();
        s_basicCommandList = nullptr;
    }
    if (s_basicPipelineState != nullptr)
    {
        s_basicPipelineState->Release();
        s_basicPipelineState = nullptr;
    }
    if (s_rootSignature != nullptr)
    {
        s_rootSignature->Release();
        s_rootSignature = nullptr;
    }
    for (UINT i = 0; i < TOTAL_FRAME_COUNT; ++i)
    {
        if (s_renderTargets[i] != nullptr)
        {
            s_renderTargets[i]->Release();
            s_renderTargets[i] = nullptr;
        }
    }
    if (s_rtvDescriptorHeap != nullptr)
    {
        s_rtvDescriptorHeap->Release();
        s_rtvDescriptorHeap = nullptr;
    }
    if (s_swapChain != nullptr)
    {
        s_swapChain->Release();
        s_swapChain = nullptr;
    }
    if (s_commandQueue != nullptr)
    {
        s_commandQueue->Release();
        s_commandQueue = nullptr;
    }
    if (s_commandBundleAllocator != nullptr)
    {
        s_commandBundleAllocator->Release();
        s_commandBundleAllocator = nullptr;
    }
    if (s_commandAllocator != nullptr)
    {
        s_commandAllocator->Release();
        s_commandAllocator = nullptr;
    }
    if (s_device != nullptr)
    {
        s_device->Release();
        s_device = nullptr;
    }
    if (s_factory != nullptr)
    {
        s_factory->Release();
        s_factory = nullptr;
    }
}

static auto CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) -> LRESULT
{
    switch (uMsg)
    {
    case WM_CREATE:
    {
        RECT windowRect;
        GetWindowRect(hWnd, &windowRect);
        SetWindowLongA(hWnd, GWL_STYLE, GetWindowLongA(hWnd, GWL_STYLE) & ~WS_MINIMIZEBOX);
        SetWindowLongA(hWnd, GWL_STYLE, GetWindowLongA(hWnd, GWL_STYLE) & ~WS_MAXIMIZEBOX);
        SetWindowLongA(hWnd, GWL_STYLE, GetWindowLongA(hWnd, GWL_STYLE) & ~WS_SIZEBOX);
        break;
    }

    case WM_CLOSE:
        DestroyAllAssets();
        PostQuitMessage(0);
        break;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        // TODO: Add any drawing code that uses hdc here...
        (void)hdc;

        // 我们这里再绘制一帧
        if (s_basicPipelineState != nullptr) {
            Render();
        }

        EndPaint(hWnd, &ps);

        break;
    }

    case WM_GETMINMAXINFO:  // set window's minimum size
        ((MINMAXINFO*)lParam)->ptMinTrackSize = s_wndMinsize;
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_SIZE:
        // Resize the application to the new window size, except when
        // it was minimized.
        break;

    case WM_KEYDOWN:
        switch (wParam)
        {
        case VK_ESCAPE:
            PostQuitMessage(0);
            break;
        case VK_LEFT:
            break;
        case VK_RIGHT:
            break;
        case VK_SPACE:
            break;
        }
        return 0;

    default:
        break;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static HWND CreateAndInitializeWindow(HINSTANCE hInstance, LPCSTR appName, int windowWidth, int windowHeight)
{
    WNDCLASSEXA win_class;
    // Initialize the window class structure:
    win_class.cbSize = sizeof(WNDCLASSEXA);
    win_class.style = CS_HREDRAW | CS_VREDRAW;
    win_class.lpfnWndProc = WndProc;
    win_class.cbClsExtra = 0;
    win_class.cbWndExtra = 0;
    win_class.hInstance = hInstance;
    win_class.hIcon = LoadIconA(NULL, IDI_APPLICATION);
    win_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    win_class.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
    win_class.lpszMenuName = NULL;
    win_class.lpszClassName = appName;
    win_class.hIconSm = LoadIconA(NULL, IDI_WINLOGO);
    // Register window class:
    if (!RegisterClassExA(&win_class))
    {
        // It didn't work, so try to give a useful error:
        printf("Unexpected error trying to start the application!\n");
        fflush(stdout);
        exit(1);
    }
    // Create window with the registered class:
    RECT wr = { 0, 0, windowWidth, windowHeight };
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    const LONG windowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_SYSMENU;

    HWND hWnd = CreateWindowExA(
        0,                            // extra style
        appName,                    // class name
        appName,                   // app name
        windowStyle,                   // window style
        CW_USEDEFAULT, CW_USEDEFAULT,     // x, y coords
        windowWidth,                    // width
        windowHeight,                  // height
        NULL,                        // handle to parent
        NULL,                            // handle to menu
        hInstance,                            // hInstance
        NULL);

    if (hWnd == NULL) {
        // It didn't work, so try to give a useful error:
        puts("Cannot create a window in which to draw!");
    }

    // Window client area size must be at least 1 pixel high, to prevent crash.
    s_wndMinsize.x = GetSystemMetrics(SM_CXMINTRACK);
    s_wndMinsize.y = GetSystemMetrics(SM_CYMINTRACK) + 1;

    return hWnd;
}

auto main(int argc, const char* argv[]) -> int
{
    if (!CreateD3D12Device()) return 1;

    bool done = false;

    // Windows Instance
    HINSTANCE wndInstance = GetModuleHandleA(NULL);

    // window handle
    HWND wndHandle = CreateAndInitializeWindow(wndInstance, s_appName, WINDOW_WIDTH, WINDOW_HEIGHT);

    do
    {
        if (!CreateCommandQueue()) break;
        if (!CreateSwapChain(wndHandle)) break;
        if (!CreateRenderTargetViews()) break;
        if (!CreateRootSignature()) break;
        if (!CreateFenceAndEvent()) break;
        if (!CreateBasicPipelineStateObject()) break;
        if (!CreateVertexBuffer()) break;
        if (!Render()) break;

        done = true;
    } while (false);

    if (!done)
    {
        DestroyAllAssets();
        return 1;
    }

    // main message loop
    MSG msg{ };
    done = false;
    while (!done)
    {
        PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE);
        // check for a quit message
        if (msg.message == WM_QUIT) {
            done = true;  // if found, quit app
        }
        else
        {
            // Translate and dispatch to event queue
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        RedrawWindow(wndHandle, NULL, NULL, RDW_INTERNALPAINT);
    }

    if (wndHandle != NULL)
    {
        DestroyWindow(wndHandle);
        wndHandle = NULL;
    }

    DestroyAllAssets();
}

// 运行程序: Ctrl + F5 或调试 >“开始执行(不调试)”菜单
// 调试程序: F5 或调试 >“开始调试”菜单

// 入门使用技巧: 
//   1. 使用解决方案资源管理器窗口添加/管理文件
//   2. 使用团队资源管理器窗口连接到源代码管理
//   3. 使用输出窗口查看生成输出和其他消息
//   4. 使用错误列表窗口查看错误
//   5. 转到“项目”>“添加新项”以创建新的代码文件，或转到“项目”>“添加现有项”以将现有代码文件添加到项目
//   6. 将来，若要再次打开此项目，请转到“文件”>“打开”>“项目”并选择 .sln 文件

