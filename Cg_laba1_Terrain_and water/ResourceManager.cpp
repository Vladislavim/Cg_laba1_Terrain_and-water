#include "ResourceManager.h"
#include "lodepng.h"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <DirectXTex.h>

using namespace DirectX;

ResourceManager::ResourceManager(Device* d,
    unsigned int numRTVs, unsigned int numDSVs,
    unsigned int numCBVSRVUAVs, unsigned int numSamplers)
    : m_pDev(d),
    m_numRTVs(numRTVs), m_numDSVs(numDSVs),
    m_numCBVSRVUAVs(numCBVSRVUAVs), m_numSamplers(numSamplers),
    m_pheapRTV(nullptr), m_pheapDSV(nullptr),
    m_pheapCBVSRVUAV(nullptr), m_pheapSampler(nullptr),
    m_pCmdAllocator(nullptr), m_pCmdList(nullptr),
    m_pFence(nullptr), m_pUpload(nullptr),
    m_hdlFenceEvent(nullptr),
    m_valFence(0), m_iUpload(0),
    m_indexFirstFreeSlotRTV(-1), m_indexFirstFreeSlotDSV(-1),
    m_indexFirstFreeSlotCBVSRVUAV(-1), m_indexFirstFreeSlotSampler(-1),
    m_sizeRTVHeapDesc(0), m_sizeDSVHeapDesc(0),
    m_sizeCBVSRVUAVHeapDesc(0), m_sizeSamplerHeapDesc(0)
{
    m_pDev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCmdAllocator);
    m_pDev->CreateGraphicsCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, m_pCmdAllocator, m_pCmdList);
    m_pCmdList->Close();

    m_pDev->CreateFence(m_valFence, D3D12_FENCE_FLAG_NONE, m_pFence);
    m_hdlFenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_hdlFenceEvent) throw GFX_Exception("ResourceManager::ResourceManager: CreateEvent failed.");

    D3D12_DESCRIPTOR_HEAP_DESC descHeap{};
    descHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    if (m_numRTVs) {
        descHeap.NumDescriptors = m_numRTVs;
        descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        m_pDev->CreateDescriptorHeap(&descHeap, m_pheapRTV);
        m_pheapRTV->SetName(L"RTV Heap");
        m_indexFirstFreeSlotRTV = 0;
    }
    if (m_numDSVs) {
        descHeap.NumDescriptors = m_numDSVs;
        descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        m_pDev->CreateDescriptorHeap(&descHeap, m_pheapDSV);
        m_pheapDSV->SetName(L"DSV Heap");
        m_indexFirstFreeSlotDSV = 0;
    }

    descHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    if (m_numCBVSRVUAVs) {
        descHeap.NumDescriptors = m_numCBVSRVUAVs;
        descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        m_pDev->CreateDescriptorHeap(&descHeap, m_pheapCBVSRVUAV);
        m_pheapCBVSRVUAV->SetName(L"CBV/SRV/UAV Heap");
        m_indexFirstFreeSlotCBVSRVUAV = 0;
    }
    if (m_numSamplers) {
        descHeap.NumDescriptors = m_numSamplers;
        descHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        m_pDev->CreateDescriptorHeap(&descHeap, m_pheapSampler);
        m_pheapSampler->SetName(L"Sampler Heap");
        m_indexFirstFreeSlotSampler = 0;
    }

    m_sizeRTVHeapDesc = m_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_sizeDSVHeapDesc = m_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    m_sizeCBVSRVUAVHeapDesc = m_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_sizeSamplerHeapDesc = m_pDev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

    // Общий upload-буфер (ring)
    D3D12_RESOURCE_DESC   upDesc = CD3DX12_RESOURCE_DESC::Buffer(DEFAULT_UPLOAD_BUFFER_SIZE);
    D3D12_HEAP_PROPERTIES upProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    m_pDev->CreateCommittedResource(
        m_pUpload,
        &upDesc,
        &upProps,
        D3D12_HEAP_FLAG_NONE,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr
    );
    m_iUpload = 0;
}

ResourceManager::~ResourceManager() {
    if (m_pUpload) {
        WaitForGPU();
        m_pUpload->Release();
        m_pUpload = nullptr;
    }

    m_pDev = nullptr;

    if (m_hdlFenceEvent) { CloseHandle(m_hdlFenceEvent); m_hdlFenceEvent = nullptr; }
    if (m_pFence) { m_pFence->Release(); m_pFence = nullptr; }
    if (m_pCmdAllocator) { m_pCmdAllocator->Release(); m_pCmdAllocator = nullptr; }
    if (m_pCmdList) { m_pCmdList->Release(); m_pCmdList = nullptr; }

    while (!m_listFileData.empty()) {
        unsigned char* p = m_listFileData.back();
        if (p) delete[] p;
        m_listFileData.pop_back();
    }
    while (!m_listResources.empty()) {
        ID3D12Resource* r = m_listResources.back();
        if (r) r->Release();
        m_listResources.pop_back();
    }

    if (m_pheapSampler) { m_pheapSampler->Release();    m_pheapSampler = nullptr; }
    if (m_pheapCBVSRVUAV) { m_pheapCBVSRVUAV->Release();  m_pheapCBVSRVUAV = nullptr; }
    if (m_pheapDSV) { m_pheapDSV->Release();        m_pheapDSV = nullptr; }
    if (m_pheapRTV) { m_pheapRTV->Release();        m_pheapRTV = nullptr; }
}

void ResourceManager::AddRTV(ID3D12Resource* tex, D3D12_RENDER_TARGET_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE& handleCPU) {
    if (m_indexFirstFreeSlotRTV < 0 || m_indexFirstFreeSlotRTV >= (int)m_numRTVs) {
        throw GFX_Exception("RTV heap is full.");
    }
    handleCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_pheapRTV->GetCPUDescriptorHandleForHeapStart(),
        m_indexFirstFreeSlotRTV, m_sizeRTVHeapDesc
    );
    m_pDev->CreateRTV(tex, desc, handleCPU);
    ++m_indexFirstFreeSlotRTV;
}

void ResourceManager::AddDSV(ID3D12Resource* tex, D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE& handleCPU) {
    if (m_indexFirstFreeSlotDSV < 0 || m_indexFirstFreeSlotDSV >= (int)m_numDSVs) {
        throw GFX_Exception("DSV heap is full.");
    }
    handleCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_pheapDSV->GetCPUDescriptorHandleForHeapStart(),
        m_indexFirstFreeSlotDSV, m_sizeDSVHeapDesc
    );
    m_pDev->CreateDSV(tex, desc, handleCPU);
    ++m_indexFirstFreeSlotDSV;
}

void ResourceManager::AddCBV(D3D12_CONSTANT_BUFFER_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE& handleCPU, D3D12_GPU_DESCRIPTOR_HANDLE& handleGPU) {
    if (m_indexFirstFreeSlotCBVSRVUAV < 0 || m_indexFirstFreeSlotCBVSRVUAV >= (int)m_numCBVSRVUAVs) {
        throw GFX_Exception("CBV/SRV/UAV heap is full.");
    }
    handleCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_pheapCBVSRVUAV->GetCPUDescriptorHandleForHeapStart(),
        m_indexFirstFreeSlotCBVSRVUAV, m_sizeCBVSRVUAVHeapDesc
    );
    handleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_pheapCBVSRVUAV->GetGPUDescriptorHandleForHeapStart(),
        m_indexFirstFreeSlotCBVSRVUAV, m_sizeCBVSRVUAVHeapDesc
    );
    m_pDev->CreateCBV(desc, handleCPU);
    ++m_indexFirstFreeSlotCBVSRVUAV;
}

void ResourceManager::AddSRV(ID3D12Resource* tex, D3D12_SHADER_RESOURCE_VIEW_DESC* desc,
    D3D12_CPU_DESCRIPTOR_HANDLE& handleCPU, D3D12_GPU_DESCRIPTOR_HANDLE& handleGPU) {
    if (m_indexFirstFreeSlotCBVSRVUAV < 0 || m_indexFirstFreeSlotCBVSRVUAV >= (int)m_numCBVSRVUAVs) {
        throw GFX_Exception("CBV/SRV/UAV heap is full.");
    }
    handleCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_pheapCBVSRVUAV->GetCPUDescriptorHandleForHeapStart(),
        m_indexFirstFreeSlotCBVSRVUAV, m_sizeCBVSRVUAVHeapDesc
    );
    handleGPU = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        m_pheapCBVSRVUAV->GetGPUDescriptorHandleForHeapStart(),
        m_indexFirstFreeSlotCBVSRVUAV, m_sizeCBVSRVUAVHeapDesc
    );
    m_pDev->CreateSRV(tex, desc, handleCPU);
    ++m_indexFirstFreeSlotCBVSRVUAV;
}

void ResourceManager::AddSampler(D3D12_SAMPLER_DESC* desc, D3D12_CPU_DESCRIPTOR_HANDLE& handleCPU) {
    if (m_indexFirstFreeSlotSampler < 0 || m_indexFirstFreeSlotSampler >= (int)m_numSamplers) {
        throw GFX_Exception("Sampler heap is full.");
    }
    handleCPU = CD3DX12_CPU_DESCRIPTOR_HANDLE(
        m_pheapSampler->GetCPUDescriptorHandleForHeapStart(),
        m_indexFirstFreeSlotSampler, m_sizeSamplerHeapDesc
    );
    m_pDev->CreateSampler(desc, handleCPU);
    ++m_indexFirstFreeSlotSampler;
}

unsigned int ResourceManager::AddExistingResource(ID3D12Resource* tex) {
    m_listResources.push_back(tex);
    return static_cast<unsigned int>(m_listResources.size() - 1);
}

unsigned int ResourceManager::NewBuffer(ID3D12Resource*& buffer, D3D12_RESOURCE_DESC* descBuffer,
    D3D12_HEAP_PROPERTIES* props, D3D12_HEAP_FLAGS flags,
    D3D12_RESOURCE_STATES state, D3D12_CLEAR_VALUE* clear) {
    m_pDev->CreateCommittedResource(buffer, descBuffer, props, flags, state, clear);
    m_listResources.push_back(buffer);
    return static_cast<unsigned int>(m_listResources.size() - 1);
}

unsigned int ResourceManager::NewBufferAt(unsigned int i, ID3D12Resource*& buffer, D3D12_RESOURCE_DESC* descBuffer,
    D3D12_HEAP_PROPERTIES* props, D3D12_HEAP_FLAGS flags,
    D3D12_RESOURCE_STATES state, D3D12_CLEAR_VALUE* clear) {
    if (i >= m_listResources.size()) throw GFX_Exception("ResourceManager::NewBufferAt: index out of bounds.");
    m_pDev->CreateCommittedResource(buffer, descBuffer, props, flags, state, clear);
    m_listResources[i] = buffer;
    return i;
}

void ResourceManager::UploadToBuffer(unsigned int i, unsigned int numSubResources,
    D3D12_SUBRESOURCE_DATA* data, D3D12_RESOURCE_STATES finalState) {
    if (i >= m_listResources.size()) throw GFX_Exception("ResourceManager::UploadToBuffer: index out of bounds.");

    // Reset: allocator -> list
    if (FAILED(m_pCmdAllocator->Reset())) {
        throw GFX_Exception("UploadToBuffer: CommandAllocator Reset failed.");
    }
    if (FAILED(m_pCmdList->Reset(m_pCmdAllocator, nullptr))) {
        throw GFX_Exception("UploadToBuffer: CommandList Reset failed.");
    }

    // Transition to COPY_DEST
    D3D12_RESOURCE_BARRIER toCopy =
        CD3DX12_RESOURCE_BARRIER::Transition(m_listResources[i], finalState, D3D12_RESOURCE_STATE_COPY_DEST);
    m_pCmdList->ResourceBarrier(1, &toCopy);

    UINT64 size = GetRequiredIntermediateSize(m_listResources[i], 0, numSubResources);
    size = (UINT64)std::pow(2.0, std::ceil(std::log((double)size) / std::log(2.0))); // округление до степени 2

    if (size > DEFAULT_UPLOAD_BUFFER_SIZE) {
        // Временный upload-ресурс
        ID3D12Resource* tmpUpload = nullptr;
        D3D12_RESOURCE_DESC   tmpDesc = CD3DX12_RESOURCE_DESC::Buffer(size);
        D3D12_HEAP_PROPERTIES tmpProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

        m_pDev->CreateCommittedResource(
            tmpUpload, &tmpDesc, &tmpProps,
            D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr);

        UpdateSubresources(m_pCmdList, m_listResources[i], tmpUpload, 0, 0, numSubResources, data);

        D3D12_RESOURCE_BARRIER toFinal =
            CD3DX12_RESOURCE_BARRIER::Transition(m_listResources[i], D3D12_RESOURCE_STATE_COPY_DEST, finalState);
        m_pCmdList->ResourceBarrier(1, &toFinal);

        if (FAILED(m_pCmdList->Close())) { tmpUpload->Release(); throw GFX_Exception("UploadToBuffer: CommandList Close failed."); }

        ID3D12CommandList* lists[] = { m_pCmdList };
        m_pDev->ExecuteCommandLists(lists, _countof(lists));

        ++m_valFence;
        m_pDev->SetFence(m_pFence, m_valFence);

        WaitForGPU();
        tmpUpload->Release();
    }
    else {
        // Проверка места в ring upload-буфере
        if (size > DEFAULT_UPLOAD_BUFFER_SIZE - m_iUpload) {
            if (m_pFence->GetCompletedValue() < m_valFence) {
                WaitForGPU();
            }
            m_iUpload = 0;
        }

        UpdateSubresources(m_pCmdList, m_listResources[i], m_pUpload, m_iUpload, 0, numSubResources, data);
        m_iUpload += (UINT)size;

        D3D12_RESOURCE_BARRIER toFinal =
            CD3DX12_RESOURCE_BARRIER::Transition(m_listResources[i], D3D12_RESOURCE_STATE_COPY_DEST, finalState);
        m_pCmdList->ResourceBarrier(1, &toFinal);

        if (FAILED(m_pCmdList->Close())) {
            throw GFX_Exception("UploadToBuffer: CommandList Close failed.");
        }

        ID3D12CommandList* lists[] = { m_pCmdList };
        m_pDev->ExecuteCommandLists(lists, _countof(lists));

        ++m_valFence;
        m_pDev->SetFence(m_pFence, m_valFence);
    }
}

void ResourceManager::WaitForGPU() {
    if (FAILED(m_pFence->SetEventOnCompletion(m_valFence, m_hdlFenceEvent))) {
        throw GFX_Exception("ResourceManager::WaitForGPU: SetEventOnCompletion failed.");
    }
    WaitForSingleObject(m_hdlFenceEvent, INFINITE);
}

ID3D12Resource* ResourceManager::GetResource(unsigned int index) {
    if (index >= m_listResources.size()) throw GFX_Exception("ResourceManager::GetResource: index out of bounds.");
    return m_listResources[index];
}

unsigned int ResourceManager::LoadFile(const char* fn, unsigned int& h, unsigned int& w) {
    unsigned char* data = nullptr;
    unsigned error = lodepng_decode32_file(&data, &w, &h, fn);
    if (error) throw GFX_Exception(("ResourceManager::LoadFile: error loading " + std::string(fn)).c_str());
    m_listFileData.push_back(data);
    return static_cast<unsigned int>(m_listFileData.size() - 1);
}

unsigned char* ResourceManager::GetFileData(unsigned int i) {
    if (i >= m_listFileData.size()) throw GFX_Exception("ResourceManager::GetFileData: index out of bounds.");
    return m_listFileData[i];
}

void ResourceManager::UnloadFileData(unsigned int i) {
    if (i >= m_listFileData.size()) throw GFX_Exception("ResourceManager::UnloadFileData: index out of bounds.");
    delete[] m_listFileData[i];
    m_listFileData[i] = nullptr;
}

// -------- DDS helpers --------

unsigned int ResourceManager::CreateTextureDDS(const wchar_t* path,
    D3D12_CPU_DESCRIPTOR_HANDLE& cpuSrv, D3D12_GPU_DESCRIPTOR_HANDLE& gpuSrv)
{
    TexMetadata meta{};
    ScratchImage img;
    HRESULT hr = LoadFromDDSFile(path, DDS_FLAGS_NONE, &meta, img);
    if (FAILED(hr)) throw GFX_Exception("CreateTextureDDS: LoadFromDDSFile failed.");

    if (meta.mipLevels <= 1) {
        ScratchImage mip;
        GenerateMipMaps(img.GetImages(), img.GetImageCount(), meta, TEX_FILTER_DEFAULT, 0, mip);
        img = std::move(mip);
        meta = img.GetMetadata();
    }

    ID3D12Resource* tex = nullptr;
    D3D12_RESOURCE_DESC desc{};

    if (meta.dimension == TEX_DIMENSION_TEXTURE3D) {
        desc = CD3DX12_RESOURCE_DESC::Tex3D(meta.format, meta.width, (UINT)meta.height, (UINT16)meta.depth, (UINT16)meta.mipLevels);
    }
    else {
        desc = CD3DX12_RESOURCE_DESC::Tex2D(meta.format, meta.width, (UINT)meta.height, (UINT16)meta.arraySize, (UINT16)meta.mipLevels);
    }

    CD3DX12_HEAP_PROPERTIES heapDefault(D3D12_HEAP_TYPE_DEFAULT);
    auto initState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    unsigned int idx = NewBuffer(
        tex,
        &desc,
        &heapDefault,          // <-- адрес L-value
        D3D12_HEAP_FLAG_NONE,
        initState,
        nullptr);


    const Image* images = img.GetImages();
    size_t count = img.GetImageCount();
    std::vector<D3D12_SUBRESOURCE_DATA> sub(count);
    for (size_t i = 0; i < count; ++i) {
        sub[i].pData = images[i].pixels;
        sub[i].RowPitch = images[i].rowPitch;
        sub[i].SlicePitch = images[i].slicePitch;
    }

    UploadToBuffer(idx, (unsigned)count, sub.data(),
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = meta.format;

    if (meta.dimension == TEX_DIMENSION_TEXTURE3D) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        srv.Texture3D.MipLevels = (UINT)meta.mipLevels;
    }
    else if (meta.miscFlags & TEX_MISC_TEXTURECUBE) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srv.TextureCube.MipLevels = (UINT)meta.mipLevels;
    }
    else if (meta.arraySize > 1) {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        srv.Texture2DArray.ArraySize = (UINT)meta.arraySize;
        srv.Texture2DArray.MipLevels = (UINT)meta.mipLevels;
    }
    else {
        srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv.Texture2D.MipLevels = (UINT)meta.mipLevels;
    }

    AddSRV(tex, &srv, cpuSrv, gpuSrv);
    return idx;
}

unsigned int ResourceManager::CreateTextureDDSA(const char* pathA,
    D3D12_CPU_DESCRIPTOR_HANDLE& cpuSrv, D3D12_GPU_DESCRIPTOR_HANDLE& gpuSrv)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, pathA, -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, pathA, -1, w.data(), n);
    return CreateTextureDDS(w.c_str(), cpuSrv, gpuSrv);
}

unsigned int ResourceManager::LoadDDS_CPU_RGBA8(const wchar_t* path,
    unsigned int& outH, unsigned int& outW)
{
    TexMetadata meta{};
    ScratchImage img;

    // 1) грузим DDS
    HRESULT hr = LoadFromDDSFile(path, DDS_FLAGS_NONE | DDS_FLAGS_ALLOW_LARGE_FILES, &meta, img);
    if (FAILED(hr)) throw GFX_Exception("LoadDDS_CPU_RGBA8: LoadFromDDSFile failed.");

    const ScratchImage* pSrc = &img;
    ScratchImage tmp;

    // 2) planar -> single plane
    if (IsPlanar(meta.format)) {
        hr = ConvertToSinglePlane(img.GetImages(), img.GetImageCount(), meta, tmp);
        if (FAILED(hr)) throw GFX_Exception("LoadDDS_CPU_RGBA8: ConvertToSinglePlane failed.");
        pSrc = &tmp;
        meta = tmp.GetMetadata();
    }

    // 3) распаковка BC*
    ScratchImage dec;
    if (IsCompressed(meta.format)) {
        hr = Decompress(pSrc->GetImages(), pSrc->GetImageCount(), meta, DXGI_FORMAT_R8G8B8A8_UNORM, dec);
        if (FAILED(hr)) throw GFX_Exception("LoadDDS_CPU_RGBA8: Decompress failed.");
        pSrc = &dec;
        meta = dec.GetMetadata();
    }

    // 4) конверт в RGBA8
    ScratchImage conv;
    if (meta.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        hr = Convert(pSrc->GetImages(), pSrc->GetImageCount(), meta,
            DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, conv);
        if (FAILED(hr)) throw GFX_Exception("LoadDDS_CPU_RGBA8: Convert failed.");
        pSrc = &conv;
        meta = conv.GetMetadata();
    }

    const Image* im = pSrc->GetImage(0, 0, 0);
    outW = (unsigned)im->width;
    outH = (unsigned)im->height;

    size_t sz = im->slicePitch;
    unsigned char* data = new unsigned char[sz];
    std::memcpy(data, im->pixels, sz);

    // 5) если альфа вся 255 — копируем R в A
    bool alphaOpaque = true;
    for (size_t y = 0; y < im->height && alphaOpaque; ++y) {
        const unsigned char* row = data + y * im->rowPitch;
        for (size_t x = 0; x < im->width; ++x) {
            if (row[x * 4 + 3] != 255) { alphaOpaque = false; break; }
        }
    }
    if (alphaOpaque) {
        for (size_t y = 0; y < im->height; ++y) {
            unsigned char* row = data + y * im->rowPitch;
            for (size_t x = 0; x < im->width; ++x) {
                row[x * 4 + 3] = row[x * 4 + 0]; // A = R
            }
        }
    }

    m_listFileData.push_back(data);
    return (unsigned)m_listFileData.size() - 1;
}

unsigned int ResourceManager::LoadDDS_CPU_RGBA8A(const char* pathA,
    unsigned int& outH, unsigned int& outW)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, pathA, -1, nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, pathA, -1, w.data(), n);
    return LoadDDS_CPU_RGBA8(w.c_str(), outH, outW);
}

unsigned int ResourceManager::LoadDDS_RGBA8(const char* fn, unsigned int& h, unsigned int& w)
{
    // utf8 -> wide
    int n = MultiByteToWideChar(CP_UTF8, 0, fn, -1, nullptr, 0);
    std::wstring ws(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, fn, -1, ws.data(), n);

    // загрузка DDS
    DirectX::TexMetadata meta{};
    DirectX::ScratchImage src;
    HRESULT hr = DirectX::LoadFromDDSFile(ws.c_str(), DirectX::DDS_FLAGS_FORCE_RGB, &meta, src);
    if (FAILED(hr)) throw GFX_Exception("LoadDDS_RGBA8: LoadFromDDSFile failed");

    // конверт в RGBA8
    DirectX::ScratchImage conv;
    hr = DirectX::Convert(src.GetImages(), src.GetImageCount(), meta,
        DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, 0.0f, conv);
    if (FAILED(hr)) throw GFX_Exception("LoadDDS_RGBA8: Convert failed");

    const DirectX::Image* im = conv.GetImage(0, 0, 0);
    w = (unsigned)im->width;
    h = (unsigned)im->height;

    size_t sz = im->slicePitch; // RGBA8
    unsigned char* data = new unsigned char[sz];
    std::memcpy(data, im->pixels, sz);

    m_listFileData.push_back(data);
    return (unsigned)m_listFileData.size() - 1;
}
