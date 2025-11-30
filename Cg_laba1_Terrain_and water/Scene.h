#pragma once

#include <chrono>
#include "Frame.h"
#include "ResourceManager.h"
#include "Terrain.h"
#include "Camera.h"
#include "DayNightCycle.h"

using namespace graphics;

enum InputKeys { _0 = 0x30, _1, _2, _3, _4, _5, _6, _7, _8, _9, _A = 0x41, _B, _C, _D, _E, _F, _G, _H, _I, _J, _K, _L, _M, _N, _O, _P, _Q, _R, _S, _T, _U, _V, _W, _X, _Y, _Z };
#define MOVE_STEP 5.0f
#define ROT_ANGLE 0.75f

static const int FRAME_BUFFER_COUNT = 3; // triple buffering.

class Scene {
public:
    Scene(int height, int width, Device* DEV);
    ~Scene();

    void Update();
    void Draw();

    void HandleKeyboardInput(UINT key);

    void HandleMouseInput(int x, int y);
    void HandleMouseClick(int mouseX, int mouseY, int screenWidth, int screenHeight);

    bool RaycastTerrain(int mouseX, int mouseY, int screenWidth, int screenHeight, XMFLOAT3& outHit);
    void ResetBrushStroke();
private:

    void CloseCommandLists();

    void SetViewport(ID3D12GraphicsCommandList* cmdList);

    void InitPipelineTerrain2D();

    void InitPipelineTerrain3D();

    void InitPipelineShadowMap();

    // --- Water ---
    void InitPipelineWater();
    void DrawWater(ID3D12GraphicsCommandList* cmdList);


    void DrawTerrain(ID3D12GraphicsCommandList* cmdList);

    void DrawShadowMap(ID3D12GraphicsCommandList* cmdList);
    void InitPipelineTerrain3D_Debug();


    Device* m_pDev = nullptr;
    ResourceManager                     m_ResMgr;
    Frame* m_pFrames[::FRAME_BUFFER_COUNT]{};
    ID3D12GraphicsCommandList* m_pCmdList = nullptr;
    Terrain* m_pT = nullptr;
    Camera                              m_Cam;
    DayNightCycle                       m_DNC;
    D3D12_VIEWPORT                      m_vpMain{};
    D3D12_RECT                          m_srMain{};
    int                                 m_drawMode = 1;
    std::vector<ID3D12RootSignature*>   m_listRootSigs;
    std::vector<ID3D12PipelineState*>   m_listPSOs;
    int                                 m_iFrame = 0;
    bool                                m_UseTextures = false;
    bool                                m_LockToTerrain = false;

    float                               m_WaterLevel = 100.0f;
    int                                 m_WaterGrid = 256;
    float                               m_WaterTime = 0.0f;
    float                               m_WaveAmp = 0.8f;
    float                               m_WaveLen = 50.0f;
    float                               m_WaveSpeed = 0.8f;
    std::chrono::steady_clock::time_point m_prevTime;
    bool     m_hasLastBrushPoint = false;
    XMFLOAT3 m_lastBrushPoint = XMFLOAT3(0, 0, 0);
};