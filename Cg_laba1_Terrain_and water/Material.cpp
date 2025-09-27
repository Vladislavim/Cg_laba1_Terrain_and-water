#include "Material.h"

TerrainMaterial::TerrainMaterial(ResourceManager* rm,
    const char* fnNormals1, const char* fnNormals2, const char* fnNormals3, const char* fnNormals4,
    const char* fnDiff1, const char* fnDiff2, const char* fnDiff3, const char* fnDiff4,
    XMFLOAT4 colors[4])
    : m_pResMgr(rm)
{
    unsigned int index[8], height = 0, width = 0;

    index[0] = m_pResMgr->LoadFile(fnNormals1, height, width);
    index[1] = m_pResMgr->LoadFile(fnNormals2, height, width);
    index[2] = m_pResMgr->LoadFile(fnNormals3, height, width);
    index[3] = m_pResMgr->LoadFile(fnNormals4, height, width);
    index[4] = m_pResMgr->LoadFile(fnDiff1, height, width);
    index[5] = m_pResMgr->LoadFile(fnDiff2, height, width);
    index[6] = m_pResMgr->LoadFile(fnDiff3, height, width);
    index[7] = m_pResMgr->LoadFile(fnDiff4, height, width);

    // 2D array на 8 слоЄв
    D3D12_RESOURCE_DESC descTex = {};
    descTex.MipLevels = 1;
    descTex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descTex.Width = width;
    descTex.Height = height;
    descTex.Flags = D3D12_RESOURCE_FLAG_NONE;
    descTex.DepthOrArraySize = 8;                                // 8 слоЄв
    descTex.SampleDesc.Count = 1;
    descTex.SampleDesc.Quality = 0;
    descTex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;

    ID3D12Resource* textures = nullptr;
    CD3DX12_HEAP_PROPERTIES defHeap(D3D12_HEAP_TYPE_DEFAULT);

    // начальное/финальное состо€ние Ч SRV (дл€ pixel и non-pixel шейдеров)
    unsigned int iBuffer = m_pResMgr->NewBuffer(
        textures, &descTex, &defHeap, D3D12_HEAP_FLAG_NONE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr
    );
    textures->SetName(L"Texture Array Buffer");

    // 8 сабресурсов: по одному на слой
    D3D12_SUBRESOURCE_DATA dataTex[8] = {};
    for (int i = 0; i < 8; ++i) {
        dataTex[i].pData = m_pResMgr->GetFileData(index[i]);
        dataTex[i].RowPitch = width * 4;                // RGBA8
        dataTex[i].SlicePitch = height * dataTex[i].RowPitch;
    }

    m_pResMgr->UploadToBuffer(iBuffer, 8, dataTex,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    // SRV дл€ Texture2DArray
    D3D12_SHADER_RESOURCE_VIEW_DESC descSRV = {};
    descSRV.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    descSRV.Format = descTex.Format;
    descSRV.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    descSRV.Texture2DArray.MostDetailedMip = 0;
    descSRV.Texture2DArray.MipLevels = descTex.MipLevels; // 1
    descSRV.Texture2DArray.FirstArraySlice = 0;
    descSRV.Texture2DArray.ArraySize = descTex.DepthOrArraySize; // 8
    descSRV.Texture2DArray.PlaneSlice = 0;
    descSRV.Texture2DArray.ResourceMinLODClamp = 0.0f;

    m_pResMgr->AddSRV(textures, &descSRV, m_hdlTextureSRV_CPU, m_hdlTextureSRV_GPU);

    m_listColors[0] = colors[0];
    m_listColors[1] = colors[1];
    m_listColors[2] = colors[2];
    m_listColors[3] = colors[3];
}

TerrainMaterial::~TerrainMaterial() {
    m_pResMgr = nullptr;
}

void TerrainMaterial::Attach(ID3D12GraphicsCommandList* cmdList, unsigned int srvDescTableIndex) {
    cmdList->SetGraphicsRootDescriptorTable(srvDescTableIndex, m_hdlTextureSRV_GPU);
}
