#pragma once
#include <DirectXMath.h>
#include <d3d12.h>
#include "Graphics.h"

// Простая водная плоскость как один quad (triangle strip), вершины генерируются в VS по SV_VertexID.
using namespace DirectX;
using namespace graphics;

struct WaterConstants
{
    XMFLOAT4X4 viewProj;   // уже транспонированная (как и в остальном проекте)
    XMFLOAT4   eye;        // позиция камеры
    XMFLOAT4   lightDir;   // xyz = нормализованное направление света
    XMFLOAT4   lightColor; // rgb цвет света
    float      time;       // секунды
    float      height;     // z-высота плоскости воды
    float      halfSize;   // половина размера квадрата воды (в мире)
    float      waveAmp;    // амплитуда волн
    float      waveFreq;   // базовая частота
    float      waveSpeed;  // скорость
    float      padding[3]; // выравнивание до 16 байт
};

class Water
{
public:
    Water(Device* dev, float waterHeight, float halfSize);
    ~Water();

    void Draw(ID3D12GraphicsCommandList* cmdList, const WaterConstants& constants);

private:
    Device* m_pDev;
    ID3D12RootSignature* m_pRootSig = nullptr;
    ID3D12PipelineState* m_pPSO = nullptr;
};
