// Terrain.cpp
#include "lodepng.h"
#include "Terrain.h"
#include "Common.h"
#include <algorithm>
#include <functional>
#include <vector>
#include <float.h>
#include <cstring>
#include <cctype>
#include <cstdarg>
#include <cstdio>

//   helpers  

// общий шаг тессел€ции (храним в статике)
static int& G_TerrainTess()
{
    static int g_tess = 16;
    return g_tess;
}

// проверка расширени€ файла без аллокаций
static bool HasExtNoCase(const char* fileName, const char* ext)
{
    size_t n = std::strlen(fileName), m = std::strlen(ext);
    if (n < m) return false;
#ifdef _WIN32
    return _stricmp(fileName + (n - m), ext) == 0;
#else
    auto tolow = [](char c) { return (char)std::tolower((unsigned char)c); };
    for (size_t i = 0; i < m; ++i)
        if (tolow(fileName[n - m + i]) != tolow(ext[i])) return false;
    return true;
#endif
}

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

//   Terrain  

Terrain::Terrain(ResourceManager* rm, TerrainMaterial* mat,
    const char* fnHeightmap, const char* fnDisplacementMap)
    : m_pMat(mat), m_pResMgr(rm)
{
    // инициализаци€
    m_dataHeightMap = nullptr;
    m_dataDisplacementMap = nullptr;
    m_dataVertices = nullptr;
    m_dataIndices = nullptr;
    m_pConstants = nullptr;

    // загрузка карт
    LoadHeightMap(fnHeightmap);
    LoadDisplacementMap(fnDisplacementMap);

    // построение
    CreateMesh3D();
    BuildQT();
}

Terrain::~Terrain()
{
    // чистим только CPU-часть
    m_dataHeightMap = nullptr;
    m_dataDisplacementMap = nullptr;
    DeleteVertexAndIndexArrays();
    m_pResMgr = nullptr;
    delete m_pMat;
}

void Terrain::Draw(ID3D12GraphicsCommandList* cmdList, bool draw3D)
{
    if (draw3D) {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
        cmdList->IASetVertexBuffers(0, 1, &m_viewVertexBuffer);
        cmdList->IASetIndexBuffer(&m_viewIndexBuffer);
        cmdList->DrawIndexedInstanced(m_numIndices, 1, 0, 0, 0);
    }
    else {
        // проста€ заглушка
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }
}

void Terrain::DeleteVertexAndIndexArrays()
{
    if (m_dataVertices) { delete[] m_dataVertices;   m_dataVertices = nullptr; }
    if (m_dataIndices) { delete[] m_dataIndices;    m_dataIndices = nullptr; }
    if (m_pConstants) { delete   m_pConstants;     m_pConstants = nullptr; }
}

// построение сетки + буферы
void Terrain::CreateMesh3D()
{
    float mountainsScale = 1.0f;
    m_scaleHeightMap = (float)m_wHeightMap / 16.0f * mountainsScale;

    const int tess = G_TerrainTess();
    const int patchCountX = m_wHeightMap / tess;
    const int patchCountY = m_hHeightMap / tess;

    const int gridVerts = patchCountX * patchCountY;
    m_numVertices = gridVerts + patchCountX * 4; // + юбка

    // вершины
    {
        const int totalVerts = (int)m_numVertices;
        m_dataVertices = new Vertex[totalVerts];

        for (int py = 0; py < patchCountY; ++py) {
            for (int px = 0; px < patchCountX; ++px) {
                float u = (float)px / (float)m_wHeightMap;
                float v = (float)py / (float)m_hHeightMap;

                float cx = (float)m_wHeightMap * 0.5f;
                float cy = (float)m_hHeightMap * 0.5f;
                float dx = ((float)px * tess - cx) / (float)m_wHeightMap;
                float dy = ((float)py * tess - cy) / (float)m_hHeightMap;
                float r = sqrtf(dx * dx + dy * dy);

                int sx = px * tess;
                int sy = py * tess;
                int idxHM = (sy * (int)m_wHeightMap + sx) * 4;

                float base = (float)m_dataHeightMap[idxHM] / 255.0f * 0.5f;
                float wave1 = 0.15f * sinf(12.0f * u + 7.0f * v);
                float wave2 = 0.10f * cosf(18.0f * u - 11.0f * v);
                float rings = 0.12f * sinf(30.0f * r);
                float terraces = 0.0f; // пока не юзаю
                float height = clamp01(base + wave1 + wave2 + rings + terraces * 0.3f) * m_scaleHeightMap * 1.5f;

                const int vIdx = py * patchCountX + px;
                m_dataVertices[vIdx].position = XMFLOAT3((float)px * tess, (float)py * tess, height);
                m_dataVertices[vIdx].skirt = 5; // обычна€ вершина
            }
        }

        XMFLOAT2 zBounds = CalcZBounds(m_dataVertices[0], m_dataVertices[gridVerts - 1]);
        m_hBase = zBounds.x - 10;

        // юбка (4 стороны)
        int writeV = gridVerts;

        // низ
        for (int px = 0; px < patchCountX; ++px) {
            m_dataVertices[writeV].position = XMFLOAT3((float)(px * tess), 0.0f, m_hBase);
            m_dataVertices[writeV++].skirt = 1;
        }
        // верх
        for (int px = 0; px < patchCountX; ++px) {
            m_dataVertices[writeV].position = XMFLOAT3((float)(px * tess), (float)(m_hHeightMap - tess), m_hBase);
            m_dataVertices[writeV++].skirt = 2;
        }
        // лево
        for (int py = 0; py < patchCountY; ++py) {
            m_dataVertices[writeV].position = XMFLOAT3(0.0f, (float)(py * tess), m_hBase);
            m_dataVertices[writeV++].skirt = 3;
        }
        // право
        for (int py = 0; py < patchCountY; ++py) {
            m_dataVertices[writeV].position = XMFLOAT3((float)(m_wHeightMap - tess), (float)(py * tess), m_hBase);
            m_dataVertices[writeV++].skirt = 4;
        }
    }

    // индексы (патчи 4-вершинные)
    {
        const int bodyPatches = (patchCountX - 1) * (patchCountY - 1);
        const int skirtH = 2 * (patchCountX - 1);
        const int skirtV = 2 * (patchCountY - 1);
        const int idxCount = bodyPatches * 4 + (skirtH + skirtV) * 4;

        m_dataIndices = new UINT[idxCount];

        int w = 0;

        // тело
        for (int py = 0; py < patchCountY - 1; ++py) {
            for (int px = 0; px < patchCountX - 1; ++px) {
                UINT v0 = px + py * patchCountX;
                UINT v1 = px + 1 + py * patchCountX;
                UINT v2 = px + (py + 1) * patchCountX;
                UINT v3 = px + 1 + (py + 1) * patchCountX;

                m_dataIndices[w++] = v0;
                m_dataIndices[w++] = v1;
                m_dataIndices[w++] = v2;
                m_dataIndices[w++] = v3;

                XMFLOAT2 bz = CalcZBounds(m_dataVertices[v0], m_dataVertices[v3]);
                m_dataVertices[v0].aabbmin = XMFLOAT3(m_dataVertices[v0].position.x - 0.5f, m_dataVertices[v0].position.y - 0.5f, bz.x - 0.5f);
                m_dataVertices[v0].aabbmax = XMFLOAT3(m_dataVertices[v3].position.x + 0.5f, m_dataVertices[v3].position.y + 0.5f, bz.y + 0.5f);
            }
        }

        // юбка
        int readSkirtV = patchCountX * patchCountY;

        // низ
        for (int px = 0; px < patchCountX - 1; ++px) {
            m_dataIndices[w++] = readSkirtV;
            m_dataIndices[w++] = readSkirtV + 1;
            m_dataIndices[w++] = px;
            m_dataIndices[w++] = px + 1;

            XMFLOAT2 bz = CalcZBounds(m_dataVertices[px], m_dataVertices[px + 1]);
            m_dataVertices[readSkirtV].aabbmin = XMFLOAT3((float)(px * tess), 0.0f, m_hBase);
            m_dataVertices[readSkirtV++].aabbmax = XMFLOAT3((float)((px + 1) * tess), 0.0f, bz.y);
        }

        // верх
        ++readSkirtV;
        for (int px = 0; px < patchCountX - 1; ++px) {
            m_dataIndices[w++] = readSkirtV + 1;
            m_dataIndices[w++] = readSkirtV;
            int offsetTop = patchCountX * (patchCountY - 1);
            m_dataIndices[w++] = px + offsetTop + 1;
            m_dataIndices[w++] = px + offsetTop;

            XMFLOAT2 bz = CalcZBounds(m_dataVertices[px + offsetTop], m_dataVertices[px + offsetTop + 1]);
            m_dataVertices[++readSkirtV].aabbmin = XMFLOAT3((float)(px * tess), (float)(m_hHeightMap - tess), m_hBase);
            m_dataVertices[readSkirtV].aabbmax = XMFLOAT3((float)((px + 1) * tess), (float)(m_hHeightMap - tess), bz.y);
        }

        // лево
        ++readSkirtV;
        for (int py = 0; py < patchCountY - 1; ++py) {
            m_dataIndices[w++] = readSkirtV + 1;
            m_dataIndices[w++] = readSkirtV;
            m_dataIndices[w++] = (py + 1) * patchCountX;
            m_dataIndices[w++] = py * patchCountX;

            XMFLOAT2 bz = CalcZBounds(m_dataVertices[py * patchCountX], m_dataVertices[(py + 1) * patchCountX]);
            m_dataVertices[++readSkirtV].aabbmin = XMFLOAT3(0.0f, (float)(py * tess), m_hBase);
            m_dataVertices[readSkirtV].aabbmax = XMFLOAT3(0.0f, (float)((py + 1) * tess), bz.y);
        }

        // право
        ++readSkirtV;
        for (int py = 0; py < patchCountY - 1; ++py) {
            m_dataIndices[w++] = readSkirtV;
            m_dataIndices[w++] = readSkirtV + 1;
            m_dataIndices[w++] = py * patchCountX + patchCountX - 1;
            m_dataIndices[w++] = (py + 1) * patchCountX + patchCountX - 1;

            XMFLOAT2 bz = CalcZBounds(m_dataVertices[py * patchCountX + patchCountX - 1],
                m_dataVertices[(py + 1) * patchCountX + patchCountX - 1]);
            m_dataVertices[readSkirtV].aabbmin = XMFLOAT3((float)(m_wHeightMap - tess), (float)(py * tess), m_hBase);
            m_dataVertices[readSkirtV++].aabbmax = XMFLOAT3((float)(m_wHeightMap - tess), (float)((py + 1) * tess), bz.y);
        }

        m_numIndices = idxCount;
    }

    // GPU-буферы
    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateConstantBuffer();

    // сфера дл€ простых тестов видимости
    float hw = (float)m_wHeightMap * 0.5f;
    float hh = (float)m_hHeightMap * 0.5f;

    XMFLOAT2 zb = CalcZBounds(m_dataVertices[0], m_dataVertices[(patchCountX * patchCountY) - 1]);
    m_BoundingSphere.SetCenter(hw, hh, (zb.y + zb.x) * 0.5f);
    m_BoundingSphere.SetRadius(sqrtf(hw * hw + hh * hh));
}

//   GPU buffers  

void Terrain::CreateVertexBuffer()
{
    ID3D12Resource* vbRes = nullptr;

    D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(m_numVertices * sizeof(Vertex));
    CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

    auto vbIndex = m_pResMgr->NewBuffer(vbRes, &vbDesc, &defHeap,
        D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr);
    vbRes->SetName(L"Terrain Vertex Buffer");

    auto vbSize = GetRequiredIntermediateSize(vbRes, 0, 1);

    D3D12_SUBRESOURCE_DATA vbData = {};
    vbData.pData = m_dataVertices;
    vbData.RowPitch = vbSize;
    vbData.SlicePitch = vbSize;

    m_pResMgr->UploadToBuffer(vbIndex, 1, &vbData, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    m_viewVertexBuffer = {};
    m_viewVertexBuffer.BufferLocation = vbRes->GetGPUVirtualAddress();
    m_viewVertexBuffer.StrideInBytes = sizeof(Vertex);
    m_viewVertexBuffer.SizeInBytes = (UINT)vbSize;
}

void Terrain::CreateIndexBuffer()
{
    ID3D12Resource* ibRes = nullptr;

    D3D12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(m_numIndices * sizeof(UINT));
    CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

    auto ibIndex = m_pResMgr->NewBuffer(ibRes, &ibDesc, &defHeap,
        D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_INDEX_BUFFER, nullptr);
    ibRes->SetName(L"Terrain Index Buffer");

    auto ibSize = GetRequiredIntermediateSize(ibRes, 0, 1);

    D3D12_SUBRESOURCE_DATA ibData = {};
    ibData.pData = m_dataIndices;
    ibData.RowPitch = ibSize;
    ibData.SlicePitch = ibSize;

    m_pResMgr->UploadToBuffer(ibIndex, 1, &ibData, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    m_viewIndexBuffer = {};
    m_viewIndexBuffer.BufferLocation = ibRes->GetGPUVirtualAddress();
    m_viewIndexBuffer.Format = DXGI_FORMAT_R32_UINT;
    m_viewIndexBuffer.SizeInBytes = (UINT)ibSize;
}

void Terrain::CreateConstantBuffer()
{
    ID3D12Resource* cbRes = nullptr;

    D3D12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(TerrainShaderConstants));
    CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

    auto cbIndex = m_pResMgr->NewBuffer(cbRes, &cbDesc, &defHeap,
        D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr);
    cbRes->SetName(L"Terrain Shader Constants Buffer");

    auto cbSize = GetRequiredIntermediateSize(cbRes, 0, 1);

    m_pConstants = new TerrainShaderConstants(m_scaleHeightMap, (float)m_wHeightMap, (float)m_hHeightMap, m_hBase);

    D3D12_SUBRESOURCE_DATA cbData = {};
    cbData.pData = m_pConstants;
    cbData.RowPitch = cbSize;
    cbData.SlicePitch = cbSize;

    m_pResMgr->UploadToBuffer(cbIndex, 1, &cbData, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = cbRes->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = (sizeof(TerrainShaderConstants) + 255) & ~255;

    m_pResMgr->AddCBV(&cbvDesc, m_hdlConstantsCBV_CPU, m_hdlConstantsCBV_GPU);
}

//   данные высот/смещени€ ====

XMFLOAT2 Terrain::CalcZBounds(Vertex bl, Vertex tr)
{
    float zmax = -100000.0f;
    float zmin = 100000.0f;

    int x0 = (bl.position.x <= 0.0f) ? 0 : (int)bl.position.x - 1;
    int y0 = (bl.position.y <= 0.0f) ? 0 : (int)bl.position.y - 1;
    int x1 = (tr.position.x >= (float)(m_wHeightMap - 1)) ? (m_wHeightMap - 1) : (int)tr.position.x + 1;
    int y1 = (tr.position.y >= (float)(m_hHeightMap - 1)) ? (m_hHeightMap - 1) : (int)tr.position.y + 1;

    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float z = ((float)m_dataHeightMap[(x + y * m_wHeightMap) * 4] / 255.0f) * m_scaleHeightMap;
            if (z > zmax) zmax = z;
            if (z < zmin) zmin = z;
        }
    }
    return XMFLOAT2(zmin, zmax);
}

void Terrain::LoadHeightMap(const char* fnHeightMap)
{
    if (HasExtNoCase(fnHeightMap, ".dds")) {
        unsigned int h = 0, w = 0;
        unsigned int idxCpu = m_pResMgr->LoadDDS_CPU_RGBA8A(fnHeightMap, h, w);
        m_dataHeightMap = m_pResMgr->GetFileData(idxCpu);
        m_wHeightMap = w;
        m_hHeightMap = h;

        m_pResMgr->CreateTextureDDSA(fnHeightMap, m_hdlHeightMapSRV_CPU, m_hdlHeightMapSRV_GPU);
    }
    else {
        unsigned int index = m_pResMgr->LoadFile(fnHeightMap, m_hHeightMap, m_wHeightMap);
        m_dataHeightMap = m_pResMgr->GetFileData(index);

        D3D12_RESOURCE_DESC descTex = {};
        descTex.MipLevels = 1;
        descTex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descTex.Width = m_wHeightMap;
        descTex.Height = m_hHeightMap;
        descTex.Flags = D3D12_RESOURCE_FLAG_NONE;
        descTex.DepthOrArraySize = 1;
        descTex.SampleDesc.Count = 1;
        descTex.SampleDesc.Quality = 0;
        descTex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ID3D12Resource* hm = nullptr;
        CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

        unsigned int iBuffer = m_pResMgr->NewBuffer(
            hm, &descTex, &defHeap, D3D12_HEAP_FLAG_NONE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
        hm->SetName(L"Height Map");

        D3D12_SUBRESOURCE_DATA dataTex = {};
        dataTex.pData = m_dataHeightMap;
        dataTex.RowPitch = m_wHeightMap * 4;
        dataTex.SlicePitch = m_hHeightMap * m_wHeightMap * 4;

        m_pResMgr->UploadToBuffer(iBuffer, 1, &dataTex,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
        descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        descSRV.Format = descTex.Format;
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        descSRV.Texture2D.MipLevels = 1;

        m_pResMgr->AddSRV(hm, &descSRV, m_hdlHeightMapSRV_CPU, m_hdlHeightMapSRV_GPU);
    }
}

void Terrain::LoadDisplacementMap(const char* fnMap)
{
    if (HasExtNoCase(fnMap, ".dds")) {
        unsigned int h = 0, w = 0;
        unsigned int idxCpu = m_pResMgr->LoadDDS_CPU_RGBA8A(fnMap, h, w);
        m_dataDisplacementMap = m_pResMgr->GetFileData(idxCpu);
        m_wDisplacementMap = w;
        m_hDisplacementMap = h;

        D3D12_RESOURCE_DESC descTex = {};
        descTex.MipLevels = 1;
        descTex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descTex.Width = m_wDisplacementMap;
        descTex.Height = m_hDisplacementMap;
        descTex.Flags = D3D12_RESOURCE_FLAG_NONE;
        descTex.DepthOrArraySize = 1;
        descTex.SampleDesc.Count = 1;
        descTex.SampleDesc.Quality = 0;
        descTex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ID3D12Resource* dm = nullptr;
        CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

        unsigned int iBuffer = m_pResMgr->NewBuffer(
            dm, &descTex, &defHeap, D3D12_HEAP_FLAG_NONE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            nullptr);
        dm->SetName(L"Displacement Map (DDS?RGBA8)");

        D3D12_SUBRESOURCE_DATA dataTex = {};
        dataTex.pData = m_dataDisplacementMap;
        dataTex.RowPitch = m_wDisplacementMap * 4;
        dataTex.SlicePitch = m_hDisplacementMap * m_wDisplacementMap * 4;

        m_pResMgr->UploadToBuffer(iBuffer, 1, &dataTex,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
        descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        descSRV.Format = descTex.Format;
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        descSRV.Texture2D.MipLevels = 1;

        m_pResMgr->AddSRV(dm, &descSRV, m_hdlDisplacementMapSRV_CPU, m_hdlDisplacementMapSRV_GPU);
    }
    else {
        unsigned int index = m_pResMgr->LoadFile(fnMap, m_hDisplacementMap, m_wDisplacementMap);
        m_dataDisplacementMap = m_pResMgr->GetFileData(index);

        D3D12_RESOURCE_DESC descTex = {};
        descTex.MipLevels = 1;
        descTex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        descTex.Width = m_wDisplacementMap;
        descTex.Height = m_hDisplacementMap;
        descTex.Flags = D3D12_RESOURCE_FLAG_NONE;
        descTex.DepthOrArraySize = 1;
        descTex.SampleDesc.Count = 1;
        descTex.SampleDesc.Quality = 0;
        descTex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

        ID3D12Resource* dm = nullptr;
        CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

        unsigned int iBuffer = m_pResMgr->NewBuffer(
            dm, &descTex, &defHeap, D3D12_HEAP_FLAG_NONE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr);
        dm->SetName(L"Displacement Map");

        D3D12_SUBRESOURCE_DATA dataTex = {};
        dataTex.pData = m_dataDisplacementMap;
        dataTex.RowPitch = m_wDisplacementMap * 4;
        dataTex.SlicePitch = m_hDisplacementMap * m_wDisplacementMap * 4;

        m_pResMgr->UploadToBuffer(iBuffer, 1, &dataTex,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

        D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
        descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        descSRV.Format = descTex.Format;
        descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        descSRV.Texture2D.MipLevels = 1;

        m_pResMgr->AddSRV(dm, &descSRV, m_hdlDisplacementMapSRV_CPU, m_hdlDisplacementMapSRV_GPU);
    }
}

//   биндинги  

void Terrain::AttachTerrainResources(ID3D12GraphicsCommandList* cmdList,
    unsigned int slotHM, unsigned int slotDM, unsigned int slotCBV)
{
    cmdList->SetGraphicsRootDescriptorTable(slotHM, m_hdlHeightMapSRV_GPU);
    cmdList->SetGraphicsRootDescriptorTable(slotDM, m_hdlDisplacementMapSRV_GPU);
    cmdList->SetGraphicsRootDescriptorTable(slotCBV, m_hdlConstantsCBV_GPU);
}

void Terrain::AttachMaterialResources(ID3D12GraphicsCommandList* cmdList, unsigned int srvTableIndex)
{
    m_pMat->Attach(cmdList, srvTableIndex);
}

//   выборка по картам  

float Terrain::GetHeightMapValueAtPoint(float x, float y)
{
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    float x1f = floorf(x), x2f = ceilf(x);
    float y1f = floorf(y), y2f = ceilf(y);

    int x1 = clampi((int)x1f, 0, (int)m_wHeightMap - 1);
    int x2 = clampi((int)x2f, 0, (int)m_wHeightMap - 1);
    int y1 = clampi((int)y1f, 0, (int)m_hHeightMap - 1);
    int y2 = clampi((int)y2f, 0, (int)m_hHeightMap - 1);

    float dx = x - (float)x1f;
    float dy = y - (float)y1f;

    float a = (float)m_dataHeightMap[(y1 * m_wHeightMap + x1) * 4 + 0] / 255.0f;
    float b = (float)m_dataHeightMap[(y1 * m_wHeightMap + x2) * 4 + 0] / 255.0f;
    float c = (float)m_dataHeightMap[(y2 * m_wHeightMap + x1) * 4 + 0] / 255.0f;
    float d = (float)m_dataHeightMap[(y2 * m_wHeightMap + x2) * 4 + 0] / 255.0f;

    return bilerp(a, b, c, d, dx, dy);
}

float Terrain::GetDisplacementMapValueAtPoint(float x, float y)
{
    // тайлим карту, чтоб видеть мелочь
    float u = (x / (float)m_wDisplacementMap) / 32.0f;
    float v = (y / (float)m_hDisplacementMap) / 32.0f;

    u = u - floorf(u);
    v = v - floorf(v);

    float X = u * (float)(m_wDisplacementMap - 1);
    float Y = v * (float)(m_hDisplacementMap - 1);

    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    float x1f = floorf(X), x2f = ceilf(X);
    float y1f = floorf(Y), y2f = ceilf(Y);

    int x1 = clampi((int)x1f, 0, (int)m_wDisplacementMap - 1);
    int x2 = clampi((int)x2f, 0, (int)m_wDisplacementMap - 1);
    int y1 = clampi((int)y1f, 0, (int)m_hDisplacementMap - 1);
    int y2 = clampi((int)y2f, 0, (int)m_hDisplacementMap - 1);

    float dx = X - (float)x1f;
    float dy = Y - (float)y1f;

    // берЄм альфу (канал A)
    float a = (float)m_dataDisplacementMap[(y1 * m_wDisplacementMap + x1) * 4 + 3] / 255.0f;
    float b = (float)m_dataDisplacementMap[(y1 * m_wDisplacementMap + x2) * 4 + 3] / 255.0f;
    float c = (float)m_dataDisplacementMap[(y2 * m_wDisplacementMap + x1) * 4 + 3] / 255.0f;
    float d = (float)m_dataDisplacementMap[(y2 * m_wDisplacementMap + x2) * 4 + 3] / 255.0f;

    return bilerp(a, b, c, d, dx, dy);
}

XMFLOAT3 Terrain::CalculateNormalAtPoint(float x, float y)
{
    // небольшой собель по окрестности
    XMFLOAT2 b(x, y - 0.3f / m_hHeightMap);
    XMFLOAT2 c(x + 0.3f / m_wHeightMap, y - 0.3f / m_hHeightMap);
    XMFLOAT2 d(x + 0.3f / m_wHeightMap, y);
    XMFLOAT2 e(x + 0.3f / m_wHeightMap, y + 0.3f / m_hHeightMap);
    XMFLOAT2 f(x, y + 0.3f / m_hHeightMap);
    XMFLOAT2 g(x - 0.3f / m_wHeightMap, y + 0.3f / m_hHeightMap);
    XMFLOAT2 h(x - 0.3f / m_wHeightMap, y);
    XMFLOAT2 i(x - 0.3f / m_wHeightMap, y - 0.3f / m_hHeightMap);

    float zb = GetHeightMapValueAtPoint(b.x, b.y) * m_scaleHeightMap;
    float zc = GetHeightMapValueAtPoint(c.x, c.y) * m_scaleHeightMap;
    float zd = GetHeightMapValueAtPoint(d.x, d.y) * m_scaleHeightMap;
    float ze = GetHeightMapValueAtPoint(e.x, e.y) * m_scaleHeightMap;
    float zf = GetHeightMapValueAtPoint(f.x, f.y) * m_scaleHeightMap;
    float zg = GetHeightMapValueAtPoint(g.x, g.y) * m_scaleHeightMap;
    float zh = GetHeightMapValueAtPoint(h.x, h.y) * m_scaleHeightMap;
    float zi = GetHeightMapValueAtPoint(i.x, i.y) * m_scaleHeightMap;

    float nx = sin(zg + 2 * zh + zi - zc - 2 * zd - ze);
    float ny = 2 * zb + zc + zi - ze - 2 * zf - zg;
    float nz = 8.0f;

    XMFLOAT3 n(nx, ny, nz);
    XMVECTOR nn = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, nn);
    return n;
}

float Terrain::GetHeightAtPoint(float x, float y)
{
    float z = GetHeightMapValueAtPoint(x, y) * m_scaleHeightMap;
    float d = 2.0f * GetDisplacementMapValueAtPoint(x, y) - 1.0f;

    XMFLOAT3 n = CalculateNormalAtPoint(x, y);
    XMFLOAT3 p(x, y, z);

    XMVECTOR nv = XMLoadFloat3(&n);
    XMVECTOR pv = XMLoadFloat3(&p);
    XMVECTOR pf = pv + nv * 0.5f * d;

    XMFLOAT3 outP;
    XMStoreFloat3(&outP, pf);
    return outP.z;
}

//   квадродерево (LOD)  

struct __QTNode {
    int x0, y0, x1, y1;
    XMFLOAT3 aabbMin, aabbMax;
    int level;
    __QTNode* c[4];
    __QTNode() : x0(0), y0(0), x1(0), y1(0), level(0) { c[0] = c[1] = c[2] = c[3] = nullptr; }
};

static __QTNode* s_qtRoot = nullptr;
static int  s_patchW = 0, s_patchH = 0;
static int  s_tessStep = 8;

static void DestroyQT(__QTNode* n) { if (!n) return; for (int i = 0; i < 4; i++) DestroyQT(n->c[i]); delete n; }

void Terrain::BuildQT()
{
    DestroyQT(s_qtRoot); s_qtRoot = nullptr;

    s_tessStep = G_TerrainTess();
    s_patchW = max(1, (int)(m_wHeightMap / s_tessStep));
    s_patchH = max(1, (int)(m_hHeightMap / s_tessStep));

    auto calcZ = [&](int x0, int y0, int x1, int y1)->XMFLOAT2 {
        int px0 = x0 * s_tessStep, py0 = y0 * s_tessStep;
        int px1 = min(m_wHeightMap - 1, x1 * s_tessStep - 1);
        int py1 = min(m_hHeightMap - 1, y1 * s_tessStep - 1);

        float zmin = +FLT_MAX, zmax = -FLT_MAX;
        for (int y = py0; y <= py1; ++y)
            for (int x = px0; x <= px1; ++x) {
                float z = ((float)m_dataHeightMap[(x + y * m_wHeightMap) * 4] / 255.0f) * m_scaleHeightMap;
                if (z < zmin) zmin = z;
                if (z > zmax) zmax = z;
            }
        return XMFLOAT2(zmin + m_hBase, zmax + m_hBase);
        };

    std::function<__QTNode* (int, int, int, int, int)> build =
        [&](int x0, int y0, int x1, int y1, int lvl)->__QTNode*
        {
            __QTNode* n = new __QTNode(); n->x0 = x0; n->y0 = y0; n->x1 = x1; n->y1 = y1; n->level = lvl;

            XMFLOAT2 bz = calcZ(x0, y0, x1, y1);
            n->aabbMin = XMFLOAT3((float)(x0 * s_tessStep), (float)(y0 * s_tessStep), bz.x);
            n->aabbMax = XMFLOAT3((float)(x1 * s_tessStep), (float)(y1 * s_tessStep), bz.y);

            if ((x1 - x0) <= 1 && (y1 - y0) <= 1) return n;

            int mx = (x0 + x1) / 2, my = (y0 + y1) / 2;
            if (mx == x0 && x1 - x0 > 1) mx = x0 + 1;
            if (my == y0 && y1 - y0 > 1) my = y0 + 1;

            n->c[0] = (x0 < mx && y0 < my) ? build(x0, y0, mx, my, lvl + 1) : nullptr;
            n->c[1] = (mx < x1 && y0 < my) ? build(mx, y0, x1, my, lvl + 1) : nullptr;
            n->c[2] = (x0 < mx && my < y1) ? build(x0, my, mx, y1, lvl + 1) : nullptr;
            n->c[3] = (mx < x1 && my < y1) ? build(mx, my, x1, y1, lvl + 1) : nullptr;

            for (int k = 0; k < 4; k++) if (n->c[k]) {
                n->aabbMin.x = min(n->aabbMin.x, n->c[k]->aabbMin.x);
                n->aabbMin.y = min(n->aabbMin.y, n->c[k]->aabbMin.y);
                n->aabbMin.z = min(n->aabbMin.z, n->c[k]->aabbMin.z);
                n->aabbMax.x = max(n->aabbMax.x, n->c[k]->aabbMax.x);
                n->aabbMax.y = max(n->aabbMax.y, n->c[k]->aabbMax.y);
                n->aabbMax.z = max(n->aabbMax.z, n->c[k]->aabbMax.z);
            }
            return n;
        };

    s_qtRoot = build(0, 0, s_patchW, s_patchH, 0);
}

static inline float dist2(const XMFLOAT3& a, const XMFLOAT3& b)
{
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

void Terrain::SelectQT(const DirectX::XMFLOAT3& eye, std::vector<DirectX::XMFLOAT4>& outBoxes) const
{
    outBoxes.clear();
    if (!s_qtRoot) return;

    auto shouldSplit = [&](const __QTNode* n) -> bool {
        float cx = 0.5f * (n->aabbMin.x + n->aabbMax.x);
        float cy = 0.5f * (n->aabbMin.y + n->aabbMax.y);
        float ex = 0.5f * (n->aabbMax.x - n->aabbMin.x);
        float ey = 0.5f * (n->aabbMax.y - n->aabbMin.y);
        float R = max(ex, ey);

        float dx = eye.x - cx, dy = eye.y - cy;
        float dist = sqrtf(dx * dx + dy * dy) + 1e-3f;

        const float k = 2.0f;
        const float minSize = 4.0f * s_tessStep;
        return (R > minSize) && (dist < k * R);
        };

    std::function<void(__QTNode*)> walk = [&](__QTNode* n)
        {
            const bool hasChildren = (n->c[0] || n->c[1] || n->c[2] || n->c[3]);
            if (hasChildren && shouldSplit(n)) {
                for (int i = 0; i < 4; ++i) if (n->c[i]) walk(n->c[i]);
                return;
            }

            outBoxes.emplace_back(
                DirectX::XMFLOAT4(n->aabbMin.x, n->aabbMin.y, n->aabbMax.x, n->aabbMax.y)
            );
        };

    walk(s_qtRoot);
}

#define LOD_DEBUG 1
#define LOD_DEBUG_EVERY_N_FRAMES 60

void Terrain::DrawLOD(ID3D12GraphicsCommandList* cmdList, const DirectX::XMFLOAT3& eye)
{
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
    cmdList->IASetVertexBuffers(0, 1, &m_viewVertexBuffer);
    cmdList->IASetIndexBuffer(&m_viewIndexBuffer);

#if LOD_DEBUG
    static int s_frame = 0;
    const bool doLog = ((++s_frame % LOD_DEBUG_EVERY_N_FRAMES) == 0);

    auto LOGF = [](const char* fmt, ...) {
        char buf[512];
        va_list args; va_start(args, fmt);
        _vsnprintf_s(buf, _TRUNCATE, fmt, args);
        va_end(args);
        OutputDebugStringA(buf);
        std::printf("%s", buf);
        };
#endif

    std::vector<DirectX::XMFLOAT4> boxes;
    SelectQT(eye, boxes);

#if LOD_DEBUG
    if (doLog)
    {
        const int cellsX = max(0, s_patchW - 1);
        const int cellsY = max(0, s_patchH - 1);
        const int totalPatches = cellsX * cellsY;

        auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
        auto imax = [](int a, int b) { return (a > b) ? a : b; };
        auto imin = [](int a, int b) { return (a < b) ? a : b; };

        long long estPatches = 0;
        int minW = 0x7fffffff, minH = 0x7fffffff;
        int maxW = 0, maxH = 0;

        LOGF(
            "\n[LOD] frame=%d | eye=(%.1f, %.1f, %.1f) | tessStep=%d | grid=%dx%d patches=%d | boxes=%zu\n",
            s_frame, eye.x, eye.y, eye.z, s_tessStep, cellsX, cellsY, totalPatches, boxes.size()
        );

        const int PREVIEW = 5;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            const auto& b = boxes[i];
            int vx0 = (int)floorf(b.x / (float)s_tessStep);
            int vy0 = (int)floorf(b.y / (float)s_tessStep);
            int vx1 = (int)ceilf(b.z / (float)s_tessStep);
            int vy1 = (int)ceilf(b.w / (float)s_tessStep);

            int x0 = clampi(vx0, 0, cellsX);
            int y0 = clampi(vy0, 0, cellsY);
            int x1 = clampi(vx1, 0, cellsX);
            int y1 = clampi(vy1, 0, cellsY);

            const int bw = imax(0, x1 - x0);
            const int bh = imax(0, y1 - y0);
            const int area = bw * bh;

            estPatches += area;
            minW = imin(minW, bw);  minH = imin(minH, bh);
            maxW = imax(maxW, bw);  maxH = imax(maxH, bh);

            if ((int)i < PREVIEW)
                LOGF("  box[%02zu] world=[%.0f %.0f .. %.0f %.0f] -> patches=[%d..%d)x[%d..%d) size=%dx%d area=%d\n",
                    i, b.x, b.y, b.z, b.w, x0, x1, y0, y1, bw, bh, area);
        }

        const double coverage = (totalPatches > 0)
            ? (100.0 * (double)estPatches / (double)totalPatches) : 0.0;

        LOGF("  summary: estPatches=%lld (%.1f%% of %d) | boxSize[min=%dx%d max=%dx%d]\n",
            (long long)estPatches, coverage, totalPatches,
            (minW == 0x7fffffff ? 0 : minW), (minH == 0x7fffffff ? 0 : minH), maxW, maxH);
    }
#endif

    cmdList->DrawIndexedInstanced(m_numIndices, 1, 0, 0, 0);
}
