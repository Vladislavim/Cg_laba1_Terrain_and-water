
#pragma once
#include "ResourceManager.h"

class TerrainMaterial {
public:
	TerrainMaterial(ResourceManager* rm, const char* fnNormals1, const char* fnNormals2, const char* fnNormals3,
		const char* fnNormals4, const char* fnDiff1, const char* fnDiff2, const char* fnDiff3, const char* fnDiff4,
		XMFLOAT4 colors[4]);
	~TerrainMaterial();

	void Attach(ID3D12GraphicsCommandList* cmdList, unsigned int srvDescTableIndex);
	XMFLOAT4* GetColors() { return m_listColors; }
private:
	ResourceManager* m_pResMgr;
	D3D12_CPU_DESCRIPTOR_HANDLE m_hdlTextureSRV_CPU;
	D3D12_GPU_DESCRIPTOR_HANDLE m_hdlTextureSRV_GPU;
	XMFLOAT4					m_listColors[4];
};

