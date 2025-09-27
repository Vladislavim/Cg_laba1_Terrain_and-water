#include "Water.h"

using namespace DirectX;
using namespace graphics;

Water::Water(Device* dev, float /*waterHeight*/, float /*halfSize*/)
    : m_pDev(dev)
{
    // Root signature: только 1 слот root constants (WaterConstants).
    CD3DX12_ROOT_PARAMETER params[1];
    params[0].InitAsConstants(static_cast<UINT>(sizeof(WaterConstants) / sizeof(float)), 0, 0, D3D12_SHADER_VISIBILITY_ALL);

    CD3DX12_ROOT_SIGNATURE_DESC rootDesc;
    rootDesc.Init(_countof(params), params, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
        D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS);

    m_pDev->CreateRootSig(&rootDesc, m_pRootSig);

    // Ўейдеры
    D3D12_SHADER_BYTECODE bcVS = {};
    D3D12_SHADER_BYTECODE bcPS = {};
    CompileShader(L"WaterVS.hlsl", VERTEX_SHADER, bcVS);
    CompileShader(L"WaterPS.hlsl", PIXEL_SHADER, bcPS);

    // PSO
    DXGI_SAMPLE_DESC sampleDesc = {};
    sampleDesc.Count = 1;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_pRootSig;
    pso.VS = bcVS;
    pso.PS = bcPS;
    pso.InputLayout = { nullptr, 0 }; // null IA Ч вершины генерируютс€ в VS
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc = sampleDesc;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    pso.BlendState.RenderTarget[0].BlendEnable = FALSE; // если нужен полупрозрачный блэнд Ч включите TRUE
    pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    pso.DepthStencilState.DepthEnable = TRUE;
    pso.DepthStencilState.StencilEnable = FALSE;

    m_pDev->CreatePSO(&pso, m_pPSO);
}

Water::~Water()
{
    if (m_pPSO) { m_pPSO->Release();   m_pPSO = nullptr; }
    if (m_pRootSig) { m_pRootSig->Release(); m_pRootSig = nullptr; }
}

void Water::Draw(ID3D12GraphicsCommandList* cmdList, const WaterConstants& constants)
{
    cmdList->SetPipelineState(m_pPSO);
    cmdList->SetGraphicsRootSignature(m_pRootSig);

    // «аливаем root constants (cbuffer b0 в шейдере)
    cmdList->SetGraphicsRoot32BitConstants(0, static_cast<UINT>(sizeof(WaterConstants) / 4), &constants, 0);

    // Quad в triangle strip: 4 вершины, индексы не нужны
    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmdList->DrawInstanced(4, 1, 0, 0);
}
