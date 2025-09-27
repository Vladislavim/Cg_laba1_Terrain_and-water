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

// Единая настройка шага сетки 
static int& TerrainTessStep()
{
    static int s_step = 64;
    return s_step;
}

static bool HasExtA(const char* fn, const char* ext)
{
    size_t n = std::strlen(fn), m = std::strlen(ext);
    if (n < m) return false;
#ifdef _WIN32
    return _stricmp(fn + (n - m), ext) == 0;
#else
    auto tolow = [](char c) { return (char)std::tolower((unsigned char)c); };
    for (size_t i = 0; i < m; ++i)
        if (tolow(fn[n - m + i]) != tolow(ext[i])) return false;
    return true;
#endif
}


// тут создаем террейн: грузим карты, делаем сетку и дерево (LOD через квадродерево)
Terrain::Terrain(ResourceManager* rm, TerrainMaterial* mat, const char* fnHeightmap, const char* fnDisplacementMap) :
    m_pMat(mat), m_pResMgr(rm) {
    m_dataHeightMap = nullptr;
    m_dataDisplacementMap = nullptr;
    m_dataVertices = nullptr;
    m_dataIndices = nullptr;
    m_pConstants = nullptr;

    LoadHeightMap(fnHeightmap);
    LoadDisplacementMap(fnDisplacementMap);

    CreateMesh3D();
    BuildQT();
}

Terrain::~Terrain() {
    m_dataHeightMap = nullptr;
    m_dataDisplacementMap = nullptr;
    DeleteVertexAndIndexArrays();
    m_pResMgr = nullptr;
    delete m_pMat;
}

void Terrain::Draw(ID3D12GraphicsCommandList* cmdList, bool Draw3D) {
    if (Draw3D) {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
        cmdList->IASetVertexBuffers(0, 1, &m_viewVertexBuffer);
        cmdList->IASetIndexBuffer(&m_viewIndexBuffer);
        cmdList->DrawIndexedInstanced(m_numIndices, 1, 0, 0, 0);
    }
    else {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->DrawInstanced(3, 1, 0, 0);
    }
}

void Terrain::DeleteVertexAndIndexArrays() {
    if (m_dataVertices) { delete[] m_dataVertices; m_dataVertices = nullptr; }
    if (m_dataIndices) { delete[] m_dataIndices;  m_dataIndices = nullptr; }
    if (m_pConstants) { delete   m_pConstants;   m_pConstants = nullptr; }
}

static inline float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void Terrain::CreateMesh3D() {
    float kMountains = 1.0f;
    m_scaleHeightMap = (float)m_wHeightMap / 16.0f * kMountains;
    int tessFactor = TerrainTessStep();
    int scalePatchX = m_wHeightMap / tessFactor;
    int scalePatchY = m_hHeightMap / tessFactor;
    int numVertsInTerrain = scalePatchX * scalePatchY;
    m_numVertices = numVertsInTerrain + scalePatchX * 4;

    int arrSize = (int)(m_numVertices);
    m_dataVertices = new Vertex[arrSize];

    for (int y = 0; y < scalePatchY; ++y) {
        for (int x = 0; x < scalePatchX; ++x) {
            float u = (float)x / (float)m_wHeightMap;
            float v = (float)y / (float)m_hHeightMap;
            float cx = (float)m_wHeightMap * 0.5f;
            float cy = (float)m_hHeightMap * 0.5f;
            float dx = ((float)x * tessFactor - cx) / (float)m_wHeightMap;
            float dy = ((float)y * tessFactor - cy) / (float)m_hHeightMap;
            float r = sqrtf(dx * dx + dy * dy);

            int sx = x * tessFactor;
            int sy = y * tessFactor;
            int idx = (sy * (int)m_wHeightMap + sx) * 4;
            float base = (float)m_dataHeightMap[idx] / 255.0f * 0.5f;
            float wave1 = 0.15f * sinf(12.0f * u + 7.0f * v);
            float wave2 = 0.10f * cosf(18.0f * u - 11.0f * v);
            float rings = 0.12f * sinf(30.0f * r);
            float terraces = 0.0f;
            float h = clamp01(base + wave1 + wave2 + rings + terraces * 0.3f) * m_scaleHeightMap * 1.5f;

            m_dataVertices[y * scalePatchX + x].position = XMFLOAT3((float)x * tessFactor, (float)y * tessFactor, h);
            m_dataVertices[y * scalePatchX + x].skirt = 5;
        }
    }

    XMFLOAT2 zBounds = CalcZBounds(m_dataVertices[0], m_dataVertices[numVertsInTerrain - 1]);
    m_hBase = zBounds.x - 10;

    int iVertex = numVertsInTerrain;
    for (int x = 0; x < scalePatchX; ++x) {
        m_dataVertices[iVertex].position = XMFLOAT3((float)(x * tessFactor), 0.0f, m_hBase);
        m_dataVertices[iVertex++].skirt = 1;
    }
    for (int x = 0; x < scalePatchX; ++x) {
        m_dataVertices[iVertex].position = XMFLOAT3((float)(x * tessFactor), (float)(m_hHeightMap - tessFactor), m_hBase);
        m_dataVertices[iVertex++].skirt = 2;
    }
    for (int y = 0; y < scalePatchY; ++y) {
        m_dataVertices[iVertex].position = XMFLOAT3(0.0f, (float)(y * tessFactor), m_hBase);
        m_dataVertices[iVertex++].skirt = 3;
    }
    for (int y = 0; y < scalePatchY; ++y) {
        m_dataVertices[iVertex].position = XMFLOAT3((float)(m_wHeightMap - tessFactor), (float)(y * tessFactor), m_hBase);
        m_dataVertices[iVertex++].skirt = 4;
    }

    arrSize = (scalePatchX - 1) * (scalePatchY - 1) * 4
        + 2 * 4 * (scalePatchX - 1)
        + 2 * 4 * (scalePatchY - 1);
    m_dataIndices = new UINT[arrSize];

    int i = 0;
    for (int y = 0; y < scalePatchY - 1; ++y) {
        for (int x = 0; x < scalePatchX - 1; ++x) {
            UINT vert0 = x + y * scalePatchX;
            UINT vert1 = x + 1 + y * scalePatchX;
            UINT vert2 = x + (y + 1) * scalePatchX;
            UINT vert3 = x + 1 + (y + 1) * scalePatchX;
            m_dataIndices[i++] = vert0;
            m_dataIndices[i++] = vert1;
            m_dataIndices[i++] = vert2;
            m_dataIndices[i++] = vert3;

            XMFLOAT2 bz = CalcZBounds(m_dataVertices[vert0], m_dataVertices[vert3]);
            m_dataVertices[vert0].aabbmin = XMFLOAT3(m_dataVertices[vert0].position.x - 0.5f, m_dataVertices[vert0].position.y - 0.5f, bz.x - 0.5f);
            m_dataVertices[vert0].aabbmax = XMFLOAT3(m_dataVertices[vert3].position.x + 0.5f, m_dataVertices[vert3].position.y + 0.5f, bz.y + 0.5f);
        }
    }

    iVertex = numVertsInTerrain;
    for (int x = 0; x < scalePatchX - 1; ++x) {
        m_dataIndices[i++] = iVertex;
        m_dataIndices[i++] = iVertex + 1;
        m_dataIndices[i++] = x;
        m_dataIndices[i++] = x + 1;
        XMFLOAT2 bz = CalcZBounds(m_dataVertices[x], m_dataVertices[x + 1]);
        m_dataVertices[iVertex].aabbmin = XMFLOAT3((float)(x * tessFactor), 0.0f, m_hBase);
        m_dataVertices[iVertex++].aabbmax = XMFLOAT3((float)((x + 1) * tessFactor), 0.0f, bz.y);
    }

    ++iVertex;
    for (int x = 0; x < scalePatchX - 1; ++x) {
        m_dataIndices[i++] = iVertex + 1;
        m_dataIndices[i++] = iVertex;
        int offset = scalePatchX * (scalePatchY - 1);
        m_dataIndices[i++] = x + offset + 1;
        m_dataIndices[i++] = x + offset;
        XMFLOAT2 bz = CalcZBounds(m_dataVertices[x + offset], m_dataVertices[x + offset + 1]);
        m_dataVertices[++iVertex].aabbmin = XMFLOAT3((float)(x * tessFactor), (float)(m_hHeightMap - tessFactor), m_hBase);
        m_dataVertices[iVertex].aabbmax = XMFLOAT3((float)((x + 1) * tessFactor), (float)(m_hHeightMap - tessFactor), bz.y);
    }

    ++iVertex;
    for (int y = 0; y < scalePatchY - 1; ++y) {
        m_dataIndices[i++] = iVertex + 1;
        m_dataIndices[i++] = iVertex;
        m_dataIndices[i++] = (y + 1) * scalePatchX;
        m_dataIndices[i++] = y * scalePatchX;
        XMFLOAT2 bz = CalcZBounds(m_dataVertices[y * scalePatchX], m_dataVertices[(y + 1) * scalePatchX]);
        m_dataVertices[++iVertex].aabbmin = XMFLOAT3(0.0f, (float)(y * tessFactor), m_hBase);
        m_dataVertices[iVertex].aabbmax = XMFLOAT3(0.0f, (float)((y + 1) * tessFactor), bz.y);
    }

    ++iVertex;
    for (int y = 0; y < scalePatchY - 1; ++y) {
        m_dataIndices[i++] = iVertex;
        m_dataIndices[i++] = iVertex + 1;
        m_dataIndices[i++] = y * scalePatchX + scalePatchX - 1;
        m_dataIndices[i++] = (y + 1) * scalePatchX + scalePatchX - 1;
        XMFLOAT2 bz = CalcZBounds(m_dataVertices[y * scalePatchX + scalePatchX - 1], m_dataVertices[(y + 1) * scalePatchX + scalePatchX - 1]);
        m_dataVertices[iVertex].aabbmin = XMFLOAT3((float)(m_wHeightMap - tessFactor), (float)(y * tessFactor), m_hBase);
        m_dataVertices[iVertex++].aabbmax = XMFLOAT3((float)(m_wHeightMap - tessFactor), (float)((y + 1) * tessFactor), bz.y);
    }

    m_numIndices = arrSize;

    CreateVertexBuffer();
    CreateIndexBuffer();
    CreateConstantBuffer();

    float w = (float)m_wHeightMap / 2.0f;
    float h = (float)m_hHeightMap / 2.0f;
    m_BoundingSphere.SetCenter(w, h, (zBounds.y + zBounds.x) / 2.0f);
    m_BoundingSphere.SetRadius(sqrtf(w * w + h * h));
}

void Terrain::CreateVertexBuffer() {
    ID3D12Resource* buffer = nullptr;

    D3D12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(m_numVertices * sizeof(Vertex));
    CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

    auto iBuffer = m_pResMgr->NewBuffer(buffer, &vbDesc, &defHeap,
        D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr);
    buffer->SetName(L"Terrain Vertex Buffer");

    auto sizeofVertexBuffer = GetRequiredIntermediateSize(buffer, 0, 1);

    D3D12_SUBRESOURCE_DATA dataVB = {};
    dataVB.pData = m_dataVertices;
    dataVB.RowPitch = sizeofVertexBuffer;
    dataVB.SlicePitch = sizeofVertexBuffer;

    m_pResMgr->UploadToBuffer(iBuffer, 1, &dataVB, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    m_viewVertexBuffer = {};
    m_viewVertexBuffer.BufferLocation = buffer->GetGPUVirtualAddress();
    m_viewVertexBuffer.StrideInBytes = sizeof(Vertex);
    m_viewVertexBuffer.SizeInBytes = (UINT)sizeofVertexBuffer;
}

void Terrain::CreateIndexBuffer() {
    ID3D12Resource* buffer = nullptr;

    D3D12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(m_numIndices * sizeof(UINT));
    CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

    auto iBuffer = m_pResMgr->NewBuffer(buffer, &ibDesc, &defHeap,
        D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_INDEX_BUFFER, nullptr);
    buffer->SetName(L"Terrain Index Buffer");

    auto sizeofIndexBuffer = GetRequiredIntermediateSize(buffer, 0, 1);

    D3D12_SUBRESOURCE_DATA dataIB = {};
    dataIB.pData = m_dataIndices;
    dataIB.RowPitch = sizeofIndexBuffer;
    dataIB.SlicePitch = sizeofIndexBuffer;

    m_pResMgr->UploadToBuffer(iBuffer, 1, &dataIB, D3D12_RESOURCE_STATE_INDEX_BUFFER);

    m_viewIndexBuffer = {};
    m_viewIndexBuffer.BufferLocation = buffer->GetGPUVirtualAddress();
    m_viewIndexBuffer.Format = DXGI_FORMAT_R32_UINT;
    m_viewIndexBuffer.SizeInBytes = (UINT)sizeofIndexBuffer;
}

void Terrain::CreateConstantBuffer() {
    ID3D12Resource* buffer = nullptr;

    D3D12_RESOURCE_DESC cbDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(TerrainShaderConstants));
    CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

    auto iBuffer = m_pResMgr->NewBuffer(buffer, &cbDesc, &defHeap,
        D3D12_HEAP_FLAG_NONE, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, nullptr);
    buffer->SetName(L"Terrain Shader Constants Buffer");

    auto sizeofBuffer = GetRequiredIntermediateSize(buffer, 0, 1);

    m_pConstants = new TerrainShaderConstants(m_scaleHeightMap, (float)m_wHeightMap, (float)m_hHeightMap, m_hBase);

    D3D12_SUBRESOURCE_DATA dataCB = {};
    dataCB.pData = m_pConstants;
    dataCB.RowPitch = sizeofBuffer;
    dataCB.SlicePitch = sizeofBuffer;

    m_pResMgr->UploadToBuffer(iBuffer, 1, &dataCB, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

    D3D12_CONSTANT_BUFFER_VIEW_DESC descCBV = {};
    descCBV.BufferLocation = buffer->GetGPUVirtualAddress();
    descCBV.SizeInBytes = (sizeof(TerrainShaderConstants) + 255) & ~255;

    m_pResMgr->AddCBV(&descCBV, m_hdlConstantsCBV_CPU, m_hdlConstantsCBV_GPU);
}

XMFLOAT2 Terrain::CalcZBounds(Vertex bottomLeft, Vertex topRight) {
    float zmax = -100000.0f;
    float zmin = 100000.0f;

    int bottomLeftX = (bottomLeft.position.x <= 0.0f) ? 0 : (int)bottomLeft.position.x - 1;
    int bottomLeftY = (bottomLeft.position.y <= 0.0f) ? 0 : (int)bottomLeft.position.y - 1;
    int topRightX = (topRight.position.x >= (float)(m_wHeightMap - 1)) ? (m_wHeightMap - 1) : (int)topRight.position.x + 1;
    int topRightY = (topRight.position.y >= (float)(m_hHeightMap - 1)) ? (m_hHeightMap - 1) : (int)topRight.position.y + 1;

    for (int y = bottomLeftY; y <= topRightY; ++y) {
        for (int x = bottomLeftX; x <= topRightX; ++x) {
            float z = ((float)m_dataHeightMap[(x + y * m_wHeightMap) * 4] / 255.0f) * m_scaleHeightMap;
            if (z > zmax) zmax = z;
            if (z < zmin) zmin = z;
        }
    }
    return XMFLOAT2(zmin, zmax);
}

void Terrain::LoadHeightMap(const char* fnHeightMap)
{
    if (HasExtA(fnHeightMap, ".dds")) {
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
    if (HasExtA(fnMap, ".dds")) {
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


// привязка ресурсов террейна
void Terrain::AttachTerrainResources(ID3D12GraphicsCommandList* cmdList, unsigned int srvDescTableIndexHeightMap,
    unsigned int srvDescTableIndexDisplacementMap, unsigned int cbvDescTableIndex) {
    cmdList->SetGraphicsRootDescriptorTable(srvDescTableIndexHeightMap, m_hdlHeightMapSRV_GPU);
    cmdList->SetGraphicsRootDescriptorTable(srvDescTableIndexDisplacementMap, m_hdlDisplacementMapSRV_GPU);
    cmdList->SetGraphicsRootDescriptorTable(cbvDescTableIndex, m_hdlConstantsCBV_GPU);
}

void Terrain::AttachMaterialResources(ID3D12GraphicsCommandList* cmdList, unsigned int srvDescTableIndex) {
    m_pMat->Attach(cmdList, srvDescTableIndex);
}

float Terrain::GetHeightMapValueAtPoint(float x, float y) {
    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    float x1f = floorf(x);
    float x2f = ceilf(x);
    float y1f = floorf(y);
    float y2f = ceilf(y);

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

float Terrain::GetDisplacementMapValueAtPoint(float x, float y) {
    float _x = (x / (float)m_wDisplacementMap) / 32.0f;
    float _y = (y / (float)m_hDisplacementMap) / 32.0f;

    _x = _x - floorf(_x);
    _y = _y - floorf(_y);

    float X = _x * (float)(m_wDisplacementMap - 1);
    float Y = _y * (float)(m_hDisplacementMap - 1);

    auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };

    float x1f = floorf(X);
    float x2f = ceilf(X);
    float y1f = floorf(Y);
    float y2f = ceilf(Y);

    int x1 = clampi((int)x1f, 0, (int)m_wDisplacementMap - 1);
    int x2 = clampi((int)x2f, 0, (int)m_wDisplacementMap - 1);
    int y1 = clampi((int)y1f, 0, (int)m_hDisplacementMap - 1);
    int y2 = clampi((int)y2f, 0, (int)m_hDisplacementMap - 1);

    float dx = X - (float)x1f;
    float dy = Y - (float)y1f;

    float a = (float)m_dataDisplacementMap[(y1 * m_wDisplacementMap + x1) * 4 + 3] / 255.0f;
    float b = (float)m_dataDisplacementMap[(y1 * m_wDisplacementMap + x2) * 4 + 3] / 255.0f;
    float c = (float)m_dataDisplacementMap[(y2 * m_wDisplacementMap + x1) * 4 + 3] / 255.0f;
    float d = (float)m_dataDisplacementMap[(y2 * m_wDisplacementMap + x2) * 4 + 3] / 255.0f;

    return bilerp(a, b, c, d, dx, dy);
}

XMFLOAT3 Terrain::CalculateNormalAtPoint(float x, float y) {
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

    float u = sin(zg + 2 * zh + zi - zc - 2 * zd - ze);
    float v = 2 * zb + zc + zi - ze - 2 * zf - zg;
    float w = 8.0f;

    XMFLOAT3 norm(u, v, w);
    XMVECTOR normalized = XMVector3Normalize(XMLoadFloat3(&norm));
    XMStoreFloat3(&norm, normalized);
    return norm;
}

float Terrain::GetHeightAtPoint(float x, float y) {
    float z = GetHeightMapValueAtPoint(x, y) * m_scaleHeightMap;
    float d = 2.0f * GetDisplacementMapValueAtPoint(x, y) - 1.0f;
    XMFLOAT3 norm = CalculateNormalAtPoint(x, y);
    XMFLOAT3 pos(x, y, z);
    XMVECTOR normal = XMLoadFloat3(&norm);
    XMVECTOR position = XMLoadFloat3(&pos);
    XMVECTOR posFinal = position + normal * 0.5f * d;
    XMFLOAT3 fp;
    XMStoreFloat3(&fp, posFinal);
    return fp.z;
}

struct __QTNode {
    int x0, y0, x1, y1;
    XMFLOAT3 aabbMin, aabbMax;
    int level;
    __QTNode* c[4];
    __QTNode() : x0(0), y0(0), x1(0), y1(0), level(0) { c[0] = c[1] = c[2] = c[3] = nullptr; }
};

static __QTNode* __gQT = nullptr;
static int  __gPatchW = 0, __gPatchH = 0;
static int  __gTessStep = 8;

static void __KillQT(__QTNode* n) { if (!n) return; for (int i = 0; i < 4; i++) __KillQT(n->c[i]); delete n; }

void Terrain::BuildQT() {
    __KillQT(__gQT); __gQT = nullptr;

    __gTessStep = TerrainTessStep();
    __gPatchW = max(1, (int)(m_wHeightMap / __gTessStep));
    __gPatchH = max(1, (int)(m_hHeightMap / __gTessStep));

    auto calcZ = [&](int x0, int y0, int x1, int y1)->XMFLOAT2 {
        int px0 = x0 * __gTessStep, py0 = y0 * __gTessStep;
        int px1 = min(m_wHeightMap - 1, x1 * __gTessStep - 1);
        int py1 = min(m_hHeightMap - 1, y1 * __gTessStep - 1);

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
        [&](int x0, int y0, int x1, int y1, int lvl)->__QTNode* {
        __QTNode* n = new __QTNode(); n->x0 = x0; n->y0 = y0; n->x1 = x1; n->y1 = y1; n->level = lvl;

        XMFLOAT2 bz = calcZ(x0, y0, x1, y1);
        n->aabbMin = XMFLOAT3((float)(x0 * __gTessStep), (float)(y0 * __gTessStep), bz.x);
        n->aabbMax = XMFLOAT3((float)(x1 * __gTessStep), (float)(y1 * __gTessStep), bz.y);

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

    __gQT = build(0, 0, __gPatchW, __gPatchH, 0);
}

static inline float __dist2(const XMFLOAT3& a, const XMFLOAT3& b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

void Terrain::SelectQT(const DirectX::XMFLOAT3& eye, std::vector<DirectX::XMFLOAT4>& outBoxes) const
{
    outBoxes.clear();
    if (!__gQT) return;

    auto isLeaf = [](__QTNode* n) -> bool {
        return !(n->c[0] || n->c[1] || n->c[2] || n->c[3]);
        };

    auto shouldSplit = [&](const __QTNode* n) -> bool {
        float cx = 0.5f * (n->aabbMin.x + n->aabbMax.x);
        float cy = 0.5f * (n->aabbMin.y + n->aabbMax.y);
        float ex = 0.5f * (n->aabbMax.x - n->aabbMin.x);
        float ey = 0.5f * (n->aabbMax.y - n->aabbMin.y);
        float R = max(ex, ey);

        float dx = eye.x - cx, dy = eye.y - cy;
        float dist = sqrt(dx * dx + dy * dy) + 1e-3f;

        const float k = 2.0f;
        const float minSize = 4.0f * __gTessStep;
        return (R > minSize) && (dist < k * R);
        };

    std::function<void(__QTNode*)> walk = [&](__QTNode* n)
        {
            if (!(n->c[0] || n->c[1] || n->c[2] || n->c[3]) == false && shouldSplit(n)) {
                for (int i = 0; i < 4; ++i) if (n->c[i]) walk(n->c[i]);
                return;
            }

            outBoxes.emplace_back(
                DirectX::XMFLOAT4(n->aabbMin.x, n->aabbMin.y, n->aabbMax.x, n->aabbMax.y)
            );
        };

    walk(__gQT);
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
        const int cellsX = max(0, __gPatchW - 1);
        const int cellsY = max(0, __gPatchH - 1);
        const int totalPatches = cellsX * cellsY;

        auto clampi = [](int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); };
        auto imax = [](int a, int b) { return (a > b) ? a : b; };
        auto imin = [](int a, int b) { return (a < b) ? a : b; };

        long long estPatches = 0;
        int minW = 0x7fffffff, minH = 0x7fffffff;
        int maxW = 0, maxH = 0;

        LOGF(
            "\n[LOD] frame=%d | eye=(%.1f, %.1f, %.1f) | tessStep=%d | grid=%dx%d patches=%d | boxes=%zu\n",
            s_frame, eye.x, eye.y, eye.z, __gTessStep, cellsX, cellsY, totalPatches, boxes.size()
        );

        const int PREVIEW = 5;
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            const auto& b = boxes[i];
            int vx0 = (int)floorf(b.x / (float)__gTessStep);
            int vy0 = (int)floorf(b.y / (float)__gTessStep);
            int vx1 = (int)ceilf(b.z / (float)__gTessStep);
            int vy1 = (int)ceilf(b.w / (float)__gTessStep);

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
