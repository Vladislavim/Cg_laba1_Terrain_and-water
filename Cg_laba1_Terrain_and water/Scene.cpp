// Scene.cpp
#include "Scene.h"
#include <stdlib.h>
#include <math.h>
#include <DirectXTex.h>
using std::chrono::steady_clock;
using std::chrono::duration;

// простой dot для vec3
static inline float __dot3(const XMFLOAT3& a, const XMFLOAT3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// проверка: AABB вне фрустума? (плоскости frustum в виде (n.xyz, w))
// kPad = запас, чтобы не отрезать по границе
static bool __AABBOutsideFrustum(const XMFLOAT4 planes[6], const XMFLOAT3& c, const XMFLOAT3& e) {
    const float kPad = 1000.0f;
    for (int i = 0; i < 6; ++i) {
        XMFLOAT3 n(planes[i].x, planes[i].y, planes[i].z);
        float s = c.x * n.x + c.y * n.y + c.z * n.z + planes[i].w; // расстояние центра до плоскости
        XMFLOAT3 an(fabsf(n.x), fabsf(n.y), fabsf(n.z));
        float r = e.x * an.x + e.y * an.y + e.z * an.z + kPad;     // проекция полуразмеров на нормаль + запас
        if (s + r < 0.0f) return true; // полностью вне
    }
    return false;
}

// сцена: ресурсы, камера, террейн, пайплайны
Scene::Scene(int height, int width, Device* DEV) :
    m_ResMgr(DEV, FRAME_BUFFER_COUNT, 6, 23, 0), m_Cam(height, width), m_DNC(6000, 1024) {
    m_pDev = DEV;
    m_pT = nullptr;

    m_drawMode = 1;          // режим рисования (1 — 3D террейн, 4 — 2D и т.п.)
    m_LockToTerrain = false; // привязка камеры к высоте террейна

    m_prevTime = steady_clock::now();
    m_WaterTime = 0.0f;

    // создаем кадры (frame resources)
    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) {
        m_pFrames[i] = new Frame(i, m_pDev, &m_ResMgr, height, width, 4096);
    }

    // базовые цвета материалов (песок/снег/земля/скала)
    XMFLOAT4 colors[] = {
        XMFLOAT4(0.20f, 0.55f, 0.32f, 0.0f),
        XMFLOAT4(0.96f, 0.98f, 1.00f, 0.0f),
        XMFLOAT4(0.50f, 0.38f, 0.28f, 0.0f),
        XMFLOAT4(0.40f, 0.42f, 0.47f, 0.0f)
    };
    // создаем террейн + материал + карты
    m_pT = new Terrain(
        &m_ResMgr,
        new TerrainMaterial(&m_ResMgr,
            "coast_sand_rocks_02_nor_dx_1k.png", "snow_02_nor_dx_1k.png",
            "dirt_nor_dx_1k.png", "rock_surface_nor_dx_1k.png",
            "coast_sand_rocks_02_diff_1k.png", "snow_02_diff_1k.png",
            "dirt_diff_1k.png", "rock_surface_diff_1k.png",
            colors),
        "hm6.png",
        "disp_4k.png"
    );

    m_ResMgr.WaitForGPU(); // ждем аплоада ресурсов

    // создаем командный список
    m_pDev->CreateGraphicsCommandList(D3D12_COMMAND_LIST_TYPE_DIRECT, m_pFrames[0]->GetAllocator(), m_pCmdList);
    CloseCommandLists();

    // настраиваем вьюпорт/ножницы
    m_vpMain.TopLeftX = 0;
    m_vpMain.TopLeftY = 0;
    m_vpMain.Width = (float)width;
    m_vpMain.Height = (float)height;
    m_vpMain.MinDepth = 0;
    m_vpMain.MaxDepth = 1;

    m_srMain.left = 0;
    m_srMain.top = 0;
    m_srMain.right = width;
    m_srMain.bottom = height;

    // инициализируем пайплайны: 3D террейн, тени, вода, дебаг-вайрфрейм
    //InitPipelineTerrain2D();
    InitPipelineTerrain3D();
    InitPipelineShadowMap();
    InitPipelineWater();
    InitPipelineTerrain3D_Debug();
}

Scene::~Scene() {
    // чистим все root сигнатуры
    while (!m_listRootSigs.empty()) {
        ID3D12RootSignature* sigRoot = m_listRootSigs.back();
        if (sigRoot) sigRoot->Release();
        m_listRootSigs.pop_back();
    }
    // чистим все PSO
    while (!m_listPSOs.empty()) {
        ID3D12PipelineState* pso = m_listPSOs.back();
        if (pso) pso->Release();
        m_listPSOs.pop_back();
    }
    if (m_pT) { delete m_pT; } // террейн удаляем
    for (int i = 0; i < FRAME_BUFFER_COUNT; ++i) { delete m_pFrames[i]; } // фреймы тоже
    m_pDev = nullptr;
}

// закрываем командные списки, если ошибка — кидаем исключение
void Scene::CloseCommandLists() {
    if (FAILED(m_pCmdList->Close())) {
        throw GFX_Exception("Scene::CloseCommandLists failed.");
    }
}

// RS+PSO для 2D версии террейна (без тесселяции), простая картинка
/*void Scene::InitPipelineTerrain2D() {
    CD3DX12_DESCRIPTOR_RANGE rangesRoot[3];
    CD3DX12_ROOT_PARAMETER paramsRoot[3];
    rangesRoot[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    paramsRoot[0].InitAsDescriptorTable(1, &rangesRoot[0]);
    rangesRoot[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    paramsRoot[1].InitAsDescriptorTable(1, &rangesRoot[1]);
    rangesRoot[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    paramsRoot[2].InitAsDescriptorTable(1, &rangesRoot[2]);

    CD3DX12_STATIC_SAMPLER_DESC descSamplers[1];
    descSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);

    CD3DX12_ROOT_SIGNATURE_DESC descRoot;
    descRoot.Init(_countof(paramsRoot), paramsRoot, 1, descSamplers,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS);
    ID3D12RootSignature* sigRoot;
    m_pDev->CreateRootSig(&descRoot, sigRoot);
    m_listRootSigs.push_back(sigRoot);

    D3D12_SHADER_BYTECODE bcPS = {};
    D3D12_SHADER_BYTECODE bcVS = {};
    CompileShader(L"RenderTerrain2dVS.hlsl", VERTEX_SHADER, bcVS);
    CompileShader(L"RenderTerrain2dPS.hlsl", PIXEL_SHADER, bcPS);

    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = 1;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPSO = {};
    descPSO.pRootSignature = sigRoot;
    descPSO.VS = bcVS;
    descPSO.PS = bcPS;
    descPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    descPSO.NumRenderTargets = 1;
    descPSO.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    descPSO.SampleDesc = sampleDesc;
    descPSO.SampleMask = UINT_MAX;
    descPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    descPSO.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    descPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    descPSO.DepthStencilState.DepthEnable = false;
    descPSO.DepthStencilState.StencilEnable = false;

    ID3D12PipelineState* pso;
    m_pDev->CreatePSO(&descPSO, pso);
    m_listPSOs.push_back(pso);
}*/

// RS+PSO для 3D террейна с тесселяцией (VS+HS+DS+PS)
void Scene::InitPipelineTerrain3D() {
    CD3DX12_ROOT_PARAMETER paramsRoot[6];
    CD3DX12_DESCRIPTOR_RANGE rangesRoot[6];

    // слоты: height, displacement, per-terrain CB, per-frame CB, shadow map, material
    rangesRoot[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    paramsRoot[0].InitAsDescriptorTable(1, &rangesRoot[0]);
    rangesRoot[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    paramsRoot[1].InitAsDescriptorTable(1, &rangesRoot[1]);
    rangesRoot[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    paramsRoot[2].InitAsDescriptorTable(1, &rangesRoot[2]);
    rangesRoot[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    paramsRoot[3].InitAsDescriptorTable(1, &rangesRoot[3]);
    rangesRoot[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
    paramsRoot[4].InitAsDescriptorTable(1, &rangesRoot[4]);
    rangesRoot[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);
    paramsRoot[5].InitAsDescriptorTable(1, &rangesRoot[5], D3D12_SHADER_VISIBILITY_PIXEL);

    // самплеры: общий, для DS, сравнения для теней, еще один общий
    CD3DX12_STATIC_SAMPLER_DESC descSamplers[4];
    descSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    descSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    descSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    descSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    descSamplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    descSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_DOMAIN;
    descSamplers[2].Init(2, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT);
    descSamplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    descSamplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    descSamplers[2].MaxAnisotropy = 1;
    descSamplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    descSamplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    descSamplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    descSamplers[3].Init(3, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    descSamplers[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    CD3DX12_ROOT_SIGNATURE_DESC descRoot;
    descRoot.Init(_countof(paramsRoot), paramsRoot, 4, descSamplers,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    ID3D12RootSignature* sigRoot;
    m_pDev->CreateRootSig(&descRoot, sigRoot);
    m_listRootSigs.push_back(sigRoot);

    // шейдеры террейна
    D3D12_SHADER_BYTECODE bcPS = {};
    D3D12_SHADER_BYTECODE bcVS = {};
    D3D12_SHADER_BYTECODE bcHS = {};
    D3D12_SHADER_BYTECODE bcDS = {};
    CompileShader(L"RenderTerrainTessVS.hlsl", VERTEX_SHADER, bcVS);
    CompileShader(L"RenderTerrainTessPS.hlsl", PIXEL_SHADER, bcPS);
    CompileShader(L"RenderTerrainTessHS.hlsl", HULL_SHADER, bcHS);
    CompileShader(L"RenderTerrainTessDS.hlsl", DOMAIN_SHADER, bcDS);

    DXGI_SAMPLE_DESC descSample = {};
    descSample.Count = 1;

    // входной лэйаут: позиция0..2 + SKIRT
    D3D12_INPUT_LAYOUT_DESC descInputLayout = {};
    D3D12_INPUT_ELEMENT_DESC descElementLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "SKIRT",   0, DXGI_FORMAT_R32_UINT,        0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    descInputLayout.NumElements = sizeof(descElementLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
    descInputLayout.pInputElementDescs = descElementLayout;

    // PSO для тесселяции террейна
    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPSO = {};
    descPSO.pRootSignature = sigRoot;
    descPSO.InputLayout = descInputLayout;
    descPSO.VS = bcVS;
    descPSO.PS = bcPS;
    descPSO.HS = bcHS;
    descPSO.DS = bcDS;
    descPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    descPSO.NumRenderTargets = 1;
    descPSO.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    descPSO.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    descPSO.SampleDesc = descSample;
    descPSO.SampleMask = UINT_MAX;
    descPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    descPSO.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    descPSO.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    descPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    descPSO.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    ID3D12PipelineState* pso;
    m_pDev->CreatePSO(&descPSO, pso);
    m_listPSOs.push_back(pso);
}

// RS+PSO для шэдоу-карты (без цветного RTV, только depth)
void Scene::InitPipelineShadowMap() {
    CD3DX12_ROOT_PARAMETER paramsRoot[4];
    CD3DX12_DESCRIPTOR_RANGE rangesRoot[4];

    // height, displacement, per-terrain CB, per-shadow-pass CB
    rangesRoot[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    paramsRoot[0].InitAsDescriptorTable(1, &rangesRoot[0]);
    rangesRoot[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
    paramsRoot[1].InitAsDescriptorTable(1, &rangesRoot[1]);
    rangesRoot[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
    paramsRoot[2].InitAsDescriptorTable(1, &rangesRoot[2]);
    rangesRoot[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);
    paramsRoot[3].InitAsDescriptorTable(1, &rangesRoot[3]);

    CD3DX12_STATIC_SAMPLER_DESC descSamplers[2];
    descSamplers[0].Init(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    descSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_DOMAIN;
    descSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    descSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    descSamplers[1].Init(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR);
    descSamplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_DOMAIN;

    CD3DX12_ROOT_SIGNATURE_DESC descRoot;
    descRoot.Init(_countof(paramsRoot), paramsRoot, 2, descSamplers,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);
    ID3D12RootSignature* sigRoot;
    m_pDev->CreateRootSig(&descRoot, sigRoot);
    m_listRootSigs.push_back(sigRoot);

    // те же VS, специализированные HS/DS под шэдоу мэп
    D3D12_SHADER_BYTECODE bcVS = {};
    D3D12_SHADER_BYTECODE bcHS = {};
    D3D12_SHADER_BYTECODE bcDS = {};
    CompileShader(L"RenderTerrainTessVS.hlsl", VERTEX_SHADER, bcVS);
    CompileShader(L"RenderShadowMapHS.hlsl", HULL_SHADER, bcHS);
    CompileShader(L"RenderShadowMapDS.hlsl", DOMAIN_SHADER, bcDS);

    DXGI_SAMPLE_DESC descSample = {};
    descSample.Count = 1;

    // входной лэйаут террейна
    D3D12_INPUT_LAYOUT_DESC descInputLayout = {};
    D3D12_INPUT_ELEMENT_DESC descElementLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "SKIRT",   0, DXGI_FORMAT_R32_UINT,        0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    descInputLayout.NumElements = sizeof(descElementLayout) / sizeof(D3D12_INPUT_ELEMENT_DESC);
    descInputLayout.pInputElementDescs = descElementLayout;

    // PSO: только depth (без RTV), смещаем глубину для борьбы с акне
    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPSO = {};
    descPSO.pRootSignature = sigRoot;
    descPSO.InputLayout = descInputLayout;
    descPSO.VS = bcVS;
    descPSO.HS = bcHS;
    descPSO.DS = bcDS;
    descPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    descPSO.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    descPSO.NumRenderTargets = 0;
    descPSO.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    descPSO.SampleDesc = descSample;
    descPSO.SampleMask = UINT_MAX;
    descPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    descPSO.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    descPSO.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    descPSO.RasterizerState.DepthBias = 10000;                // большой bias
    descPSO.RasterizerState.DepthBiasClamp = 0.0f;
    descPSO.RasterizerState.SlopeScaledDepthBias = 1.0f;
    descPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    descPSO.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    ID3D12PipelineState* pso;
    m_pDev->CreatePSO(&descPSO, pso);
    m_listPSOs.push_back(pso);
}

// RS+PSO для воды (прозрачность, без тесселяции)
void Scene::InitPipelineWater() {
    // корень: два набора констант (матрица и параметры волн)
    CD3DX12_ROOT_PARAMETER paramsRoot[2];
    paramsRoot[0].InitAsConstants(16, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    paramsRoot[1].InitAsConstants(9, 1, 0, D3D12_SHADER_VISIBILITY_VERTEX);

    CD3DX12_ROOT_SIGNATURE_DESC descRoot;
    descRoot.Init(_countof(paramsRoot), paramsRoot, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);
    ID3D12RootSignature* sigRoot;
    m_pDev->CreateRootSig(&descRoot, sigRoot);
    m_listRootSigs.push_back(sigRoot);

    // шейдеры воды
    D3D12_SHADER_BYTECODE bcVS = {}, bcPS = {};
    CompileShader(L"WaterVS.hlsl", VERTEX_SHADER, bcVS);
    CompileShader(L"WaterPS.hlsl", PIXEL_SHADER, bcPS);

    DXGI_SAMPLE_DESC descSample = {}; descSample.Count = 1;

    // PSO воды: TRIANGLE, 1 RTV, depth read-only (Zero write)
    D3D12_GRAPHICS_PIPELINE_STATE_DESC descPSO = {};
    descPSO.pRootSignature = sigRoot;
    descPSO.VS = bcVS; descPSO.PS = bcPS;
    descPSO.InputLayout = { nullptr, 0 };
    descPSO.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    descPSO.NumRenderTargets = 1; descPSO.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    descPSO.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    descPSO.SampleDesc = descSample; descPSO.SampleMask = UINT_MAX;
    descPSO.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    descPSO.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    descPSO.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    // включаем альфа-блендинг
    descPSO.BlendState.RenderTarget[0].BlendEnable = TRUE;
    descPSO.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    descPSO.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    descPSO.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    descPSO.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    descPSO.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
    descPSO.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    descPSO.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    descPSO.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    descPSO.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;       // не пишем глубину
    descPSO.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    ID3D12PipelineState* pso;
    m_pDev->CreatePSO(&descPSO, pso);
    m_listPSOs.push_back(pso);
}

// установить вьюпорт/ножницы
void Scene::SetViewport(ID3D12GraphicsCommandList* cmdList) {
    cmdList->RSSetViewports(1, &m_vpMain);
    cmdList->RSSetScissorRects(1, &m_srMain);
}

// рендер карты теней (4 каскада)
void Scene::DrawShadowMap(ID3D12GraphicsCommandList* cmdList) {
    m_pFrames[m_iFrame]->BeginShadowPass(cmdList);

    cmdList->SetPipelineState(m_listPSOs[1]);
    cmdList->SetGraphicsRootSignature(m_listRootSigs[1]);

    ID3D12DescriptorHeap* heaps[] = { m_ResMgr.GetCBVSRVUAVHeap() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    m_pT->AttachTerrainResources(cmdList, 0, 1, 2); // height/disp/CBV террейна

    for (int i = 0; i < 4; ++i) {
        ShadowMapShaderConstants constants;
        constants.shadowViewProj = m_DNC.GetShadowViewProjMatrix(i);  // матрица каскада
        constants.eye = m_Cam.GetEyePosition();                       // позиция камеры
        m_DNC.GetShadowFrustum(i, constants.frustum);                 // плоскости фрустума источника
        m_pFrames[m_iFrame]->SetShadowConstants(constants, i);
        m_pFrames[m_iFrame]->AttachShadowPassResources(i, cmdList, 3);
        m_pT->Draw(cmdList, true); // рисуем террейн в depth
    }

    m_pFrames[m_iFrame]->EndShadowPass(cmdList);
}

// рендер основной сцены: террейн (+вода), с установкой констант кадра
void Scene::DrawTerrain(ID3D12GraphicsCommandList* cmdList) {
    const float clearColor[] = { 0.2f, 0.6f, 1.0f, 1.0f };
    m_pFrames[m_iFrame]->BeginRenderPass(cmdList, clearColor);

    if (m_drawMode == 4) {

        cmdList->SetPipelineState(m_listPSOs[3]);
        cmdList->SetGraphicsRootSignature(m_listRootSigs[0]);
    }
    else {

        cmdList->SetPipelineState(m_listPSOs[0]);
        cmdList->SetGraphicsRootSignature(m_listRootSigs[0]);
    }

    SetViewport(cmdList);

    ID3D12DescriptorHeap* heaps[] = { m_ResMgr.GetCBVSRVUAVHeap() };
    cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    m_pT->AttachTerrainResources(cmdList, 0, 1, 2); // SRV height/disp + CBV террейна

    if (m_drawMode) {
        // собираем константы кадра (матрицы, тени, фрустум, свет, флаги текстур)
        XMFLOAT4 frustum[6];
        m_Cam.GetViewFrustum(frustum);

        PerFrameConstantBuffer constants;
        constants.viewproj = m_Cam.GetViewProjectionMatrixTransposed();
        for (int i = 0; i < 4; ++i) constants.shadowtexmatrices[i] = m_DNC.GetShadowViewProjTexMatrix(i);
        constants.eye = m_Cam.GetEyePosition();
        constants.frustum[0] = frustum[0];
        constants.frustum[1] = frustum[1];
        constants.frustum[2] = frustum[2];
        constants.frustum[3] = frustum[3];
        constants.frustum[4] = frustum[4];
        constants.frustum[5] = frustum[5];
        constants.light = m_DNC.GetLight();
        constants.useTextures = m_UseTextures;

        m_pFrames[m_iFrame]->SetFrameConstants(constants);
        m_pFrames[m_iFrame]->AttachFrameResources(cmdList, 4, 3); // CBV кадра + тени
        m_pT->AttachMaterialResources(cmdList, 5);                // материал террейна
    }

    if (m_drawMode) {
        // 3D: рисуем только выбранные кватро-узлы (LOD)
        XMFLOAT4 eye4 = m_Cam.GetEyePosition();
        XMFLOAT3 eye3(eye4.x, eye4.y, eye4.z);
        m_pT->DrawLOD(cmdList, eye3);
    }



    if (m_drawMode == 1) {
        DrawWater(cmdList); // поверх — вода (если 3D режим)
    }

    m_pFrames[m_iFrame]->EndRenderPass(cmdList);
}

// общий цикл кадра: ресет ? шадоу ? террейн ? презент
void Scene::Draw() {
    m_pFrames[m_iFrame]->Reset();
    m_pFrames[m_iFrame]->AttachCommandList(m_pCmdList);

    DrawShadowMap(m_pCmdList);
    DrawTerrain(m_pCmdList);

    CloseCommandLists();
    ID3D12CommandList* lCmds[] = { m_pCmdList };
    m_pDev->ExecuteCommandLists(lCmds, __crt_countof(lCmds));
    m_pDev->Present();
}

// обновление времени/камеры/света + вызов Draw()
void Scene::Update() {
    auto now = steady_clock::now();
    float dt = static_cast<float>(std::chrono::duration_cast<std::chrono::duration<double>>(now - m_prevTime).count());
    m_prevTime = now;
    if (dt < 0.25f) m_WaterTime += dt; // ограничиваем скачки времени

    if (m_LockToTerrain) {
        // держим камеру на высоте террейна + оффсет
        XMFLOAT4 eye = m_Cam.GetEyePosition();
        float h = m_pT->GetHeightAtPoint(eye.x, eye.y) + 2;
        m_Cam.LockPosition(XMFLOAT4(eye.x, eye.y, h, 1.0f));
    }

    BoundingSphere bs = m_pT->GetBoundingSphere();   // lvalue
    m_DNC.Update(bs, &m_Cam);

    m_iFrame = m_pDev->GetCurrentBackBuffer(); // какой бэк-буфер рисуем
    Draw();
}

// управление с клавиатуры: движение, режимы, текстуры, пауза теней, уровень воды
void Scene::HandleKeyboardInput(UINT key) {
    switch (key) {
    case _W: if (m_drawMode > 0) m_Cam.Translate(XMFLOAT3(MOVE_STEP, 0.0f, 0.0f)); break;
    case _S: if (m_drawMode > 0) m_Cam.Translate(XMFLOAT3(-MOVE_STEP, 0.0f, 0.0f)); break;
    case _A: if (m_drawMode > 0) m_Cam.Translate(XMFLOAT3(0.0f, MOVE_STEP, 0.0f)); break;
    case _D: if (m_drawMode > 0) m_Cam.Translate(XMFLOAT3(0.0f, -MOVE_STEP, 0.0f)); break;
    case _Q: if (m_drawMode > 0) m_Cam.Translate(XMFLOAT3(0.0f, 0.0f, MOVE_STEP)); break;
    case _Z: if (m_drawMode > 0) m_Cam.Translate(XMFLOAT3(0.0f, 0.0f, -MOVE_STEP)); break;
    case _T: m_UseTextures = !m_UseTextures; break;     // вкл/выкл текстуры террейна
    case _L: m_LockToTerrain = !m_LockToTerrain; break; // прилипание к земле
    case VK_SPACE: m_DNC.TogglePause(); break;          // стоп анимацию света/теней
    case VK_OEM_PLUS: m_WaterLevel += 1.0f; break;      // поднимаем воду
    case VK_OEM_MINUS: m_WaterLevel -= 1.0f; break;     // опускаем воду
    case _1: m_drawMode = 4; break;                     // режим 2D/дебаг
    case _2: m_drawMode = 1; break;                     // режим 3D
    case _3: m_drawMode = 1; break;                     // тот же 3D
    }
}

// мышь: поворот камеры (pitch/yaw)
void Scene::HandleMouseInput(int x, int y) {
    if (m_drawMode > 0) {
        m_Cam.Pitch(ROT_ANGLE * y);
        m_Cam.Yaw(-ROT_ANGLE * x);
    }
}

// рисуем воду (сначала AABB-фрустум тест — если не видно, выходим)
void Scene::DrawWater(ID3D12GraphicsCommandList* cmdList) {
    BoundingSphere bs = m_pT->GetBoundingSphere();
    XMFLOAT3 c = bs.GetCenter();
    float half = bs.GetRadius();

    XMFLOAT4 frustum[6];
    m_Cam.GetViewFrustum(frustum);

    XMFLOAT3 aabbCenter(c.x, c.y, m_WaterLevel);
    XMFLOAT3 aabbExtent(half, half, 20.0f);
    if (__AABBOutsideFrustum(frustum, aabbCenter, aabbExtent)) return; // не рисуем

    ID3D12PipelineState* pso = m_listPSOs[2];
    ID3D12RootSignature* rs = m_listRootSigs[2];

    cmdList->SetPipelineState(pso);
    cmdList->SetGraphicsRootSignature(rs);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // матрица VP
    XMFLOAT4X4 vp = m_Cam.GetViewProjectionMatrixTransposed();
    cmdList->SetGraphicsRoot32BitConstants(0, 16, &vp, 0);

    // параметры воды (центр, размер, уровень, волны)
    struct WaterParams { float centerX, centerY, halfSize, level; float time, grid, amp, waveLen, waveSpeed; } wp;
    wp.centerX = c.x; wp.centerY = c.y; wp.halfSize = half; wp.level = m_WaterLevel;
    wp.time = m_WaterTime; wp.grid = (float)m_WaterGrid; wp.amp = m_WaveAmp; wp.waveLen = m_WaveLen; wp.waveSpeed = m_WaveSpeed;

    cmdList->SetGraphicsRoot32BitConstants(1, 9, &wp, 0);

    // рисуем грид воды (двойные треугольники)
    UINT vertexCount = (UINT)m_WaterGrid * (UINT)m_WaterGrid * 6u;
    cmdList->DrawInstanced(vertexCount, 1, 0, 0);
}

// дебаг-PSO для террейна: вайрфрейм, тот же RS/шейдеры
void Scene::InitPipelineTerrain3D_Debug() {
    ID3D12RootSignature* sigRoot = m_listRootSigs[0];

    D3D12_SHADER_BYTECODE bcVS = {}, bcPS = {}, bcHS = {}, bcDS = {};
    CompileShader(L"RenderTerrainTessVS.hlsl", VERTEX_SHADER, bcVS);
    CompileShader(L"RenderTerrainTessPS.hlsl", PIXEL_SHADER, bcPS);
    CompileShader(L"RenderTerrainTessHS.hlsl", HULL_SHADER, bcHS);
    CompileShader(L"RenderTerrainTessDS.hlsl", DOMAIN_SHADER, bcDS);

    DXGI_SAMPLE_DESC samp = {}; samp.Count = 1;

    D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                           D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 1, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "POSITION", 2, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "SKIRT",    0, DXGI_FORMAT_R32_UINT,        0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
    D3D12_INPUT_LAYOUT_DESC ild{ layout, (UINT)_countof(layout) };

    // тот же пайплайн, только FillMode = WIREFRAME
    D3D12_GRAPHICS_PIPELINE_STATE_DESC p = {};
    p.pRootSignature = sigRoot;
    p.InputLayout = ild;
    p.VS = bcVS; p.PS = bcPS; p.HS = bcHS; p.DS = bcDS;
    p.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    p.NumRenderTargets = 1; p.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    p.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    p.SampleDesc = samp; p.SampleMask = UINT_MAX;
    p.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    p.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    p.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME; // <-- режим сетки
    p.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    p.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    ID3D12PipelineState* psoDbg = nullptr;
    m_pDev->CreatePSO(&p, psoDbg);
    m_listPSOs.push_back(psoDbg);
}
