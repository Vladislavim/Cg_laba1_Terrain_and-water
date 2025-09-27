#include "DayNightCycle.h"

static inline XMFLOAT4 ColorLerp(const XMFLOAT4& c1, const XMFLOAT4& c2, float t)
{
    XMVECTOR v1 = XMLoadFloat4(&c1);
    XMVECTOR v2 = XMLoadFloat4(&c2);
    XMFLOAT4 out;
    XMStoreFloat4(&out, XMVectorLerp(v1, v2, t));
    return out;
}

DayNightCycle::DayNightCycle(UINT period, UINT shadowSize)
    : m_dlSun(
        XMFLOAT4(0.30f, 0.30f, 0.30f, 1.0f),   // ambient
        XMFLOAT4(1.00f, 1.00f, 1.00f, 1.0f),   // diffuse
        XMFLOAT4(0.60f, 0.60f, 0.60f, 1.0f),   // specular
        XMFLOAT3(0.0f, 0.0f, -1.0f)            // direction (downwards)
    ),
    m_dlMoon(
        XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
        XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
        XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f),
        XMFLOAT3(0.0f, 0.0f, 1.0f)
    ),
    m_Period(period),
    m_sizeShadowMap(shadowSize),
    m_angleSun(0.0f),
    m_isPaused(true)
{
    m_tLast = system_clock::now();
}

DayNightCycle::~DayNightCycle() {}

void DayNightCycle::Update(const BoundingSphere& bsScene, Camera* cam)
{
    m_tLast = system_clock::now();

    LightSource ls = m_dlSun.GetLight();
    XMVECTOR dir = XMLoadFloat3(&ls.direction);
    dir = XMVector3Normalize(dir);
    XMFLOAT3 forcedDir;
    XMStoreFloat3(&forcedDir, dir);
    if (forcedDir.x == 0.0f && forcedDir.y == 0.0f && forcedDir.z > -0.5f) {
        forcedDir = XMFLOAT3(0.0f, 0.0f, -1.0f);
        m_dlSun.SetLightDirection(forcedDir);
    }

    CalculateShadowMatrices(bsScene, cam);
}

void DayNightCycle::CalculateShadowMatrices(const BoundingSphere& bsScene, Camera* cam)
{
    LightSource light = m_dlSun.GetLight();
    XMVECTOR lightdir = XMLoadFloat3(&light.direction);

    XMFLOAT3 vCenterScene = bsScene.GetCenter();
    XMVECTOR targetpos = XMLoadFloat3(&vCenterScene);

    float offset = (float)(m_sizeShadowMap + 8) / (float)m_sizeShadowMap;
    float radiusScene = ceilf(bsScene.GetRadius()) * offset;

    XMVECTOR lightpos = targetpos - 2.0f * radiusScene * lightdir;
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    up = XMVector3Cross(up, lightdir);

    XMMATRIX V = XMMatrixLookAtLH(lightpos, targetpos, up);

    XMFLOAT4 spherecenterls;

    for (int i = 0; i < 3; ++i) {
        Frustum fCascade = cam->CalculateFrustumByNearFar(CASCADE_PLANES[i], CASCADE_PLANES[i + 1]);

        float radius = ceilf(fCascade.bs.GetRadius()) * offset;

        // Нельзя брать & от временного: сначала в именованную переменную
        XMFLOAT3 cascadeCenter = fCascade.bs.GetCenter();
        XMVECTOR c = XMLoadFloat3(&cascadeCenter);

        XMStoreFloat4(&spherecenterls, XMVector3TransformCoord(c, V));

        XMVECTOR sc = XMLoadFloat3(&vCenterScene);
        XMFLOAT4 cbs;
        XMStoreFloat4(&cbs, XMVector3TransformCoord(sc, V));

        float l = spherecenterls.x - radius;
        float b = spherecenterls.y - radius;
        float n = spherecenterls.z - cbs.z - radiusScene;
        float r = spherecenterls.x + radius;
        float t = spherecenterls.y + radius;
        float f = spherecenterls.z + cbs.z + radiusScene;

        XMMATRIX P = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);
        XMMATRIX S = V * P;

        XMVECTOR shadowOrigin = XMVector3Transform(XMVectorZero(), S);
        shadowOrigin *= ((float)(m_sizeShadowMap + offset) / 4.0f);

        XMFLOAT2 so;
        XMStoreFloat2(&so, shadowOrigin);

        // Нельзя &XMFLOAT2(...): используем XMVectorSet
        XMVECTOR roundedOrigin = XMVectorSet(roundf(so.x), roundf(so.y), 0.0f, 0.0f);
        XMVECTOR rounding = roundedOrigin - shadowOrigin;
        rounding /= ((m_sizeShadowMap + offset) / 4.0f);

        XMStoreFloat2(&so, rounding);
        XMMATRIX roundMatrix = XMMatrixTranslation(so.x, so.y, 0.0f);
        S *= roundMatrix;

        CalculateShadowFrustum(i, S);

        XMStoreFloat4x4(&m_amShadowViewProjs[i], XMMatrixTranspose(S));

        float tx = (i == 0 || i == 1) ? 0.25f : 0.75f;
        float ty = (i == 0 || i == 2) ? 0.25f : 0.75f;

        XMMATRIX T(0.25f, 0.0f, 0.0f, 0.0f,
            0.0f, -0.25f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            tx, ty, 0.0f, 1.0f);

        S *= T;
        XMStoreFloat4x4(&m_amShadowViewProjTexs[i], XMMatrixTranspose(S));
    }

    // Общая сфера сцены
    XMVECTOR cScene = XMLoadFloat3(&vCenterScene);
    XMStoreFloat4(&spherecenterls, XMVector3TransformCoord(cScene, V));

    float l = spherecenterls.x - radiusScene;
    float b = spherecenterls.y - radiusScene;
    float n = spherecenterls.z - radiusScene;
    float r = spherecenterls.x + radiusScene;
    float t = spherecenterls.y + radiusScene;
    float f = spherecenterls.z + radiusScene;

    XMMATRIX P = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);
    XMMATRIX S = V * P;

    XMVECTOR shadowOrigin = XMVector3Transform(XMVectorZero(), S);
    shadowOrigin *= ((float)(m_sizeShadowMap + offset) / 4.0f);

    XMFLOAT2 so;
    XMStoreFloat2(&so, shadowOrigin);

    // Здесь тоже без временного адреса
    XMVECTOR roundedOrigin = XMVectorSet(roundf(so.x), roundf(so.y), 0.0f, 0.0f);
    XMVECTOR rounding = roundedOrigin - shadowOrigin;
    rounding /= ((m_sizeShadowMap + offset) / 4.0f);
    XMStoreFloat2(&so, rounding);

    XMMATRIX roundMatrix = XMMatrixTranslation(so.x, so.y, 0.0f);
    S *= roundMatrix;

    CalculateShadowFrustum(3, S);
    XMStoreFloat4x4(&m_amShadowViewProjs[3], XMMatrixTranspose(S));

    XMMATRIX T(0.25f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.25f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.75f, 0.75f, 0.0f, 1.0f);
    S *= T;
    XMStoreFloat4x4(&m_amShadowViewProjTexs[3], XMMatrixTranspose(S));
}

void DayNightCycle::CalculateShadowFrustum(int i, XMMATRIX VP)
{
    XMFLOAT4X4 M;
    XMStoreFloat4x4(&M, VP);

    // left
    m_aShadowFrustums[i][0].x = M(0, 3) + M(0, 0);
    m_aShadowFrustums[i][0].y = M(1, 3) + M(1, 0);
    m_aShadowFrustums[i][0].z = M(2, 3) + M(2, 0);
    m_aShadowFrustums[i][0].w = M(3, 3) + M(3, 0);

    // right
    m_aShadowFrustums[i][1].x = M(0, 3) - M(0, 0);
    m_aShadowFrustums[i][1].y = M(1, 3) - M(1, 0);
    m_aShadowFrustums[i][1].z = M(2, 3) - M(2, 0);
    m_aShadowFrustums[i][1].w = M(3, 3) - M(3, 0);

    // bottom
    m_aShadowFrustums[i][2].x = M(0, 3) + M(0, 1);
    m_aShadowFrustums[i][2].y = M(1, 3) + M(1, 1);
    m_aShadowFrustums[i][2].z = M(2, 3) + M(2, 1);
    m_aShadowFrustums[i][2].w = M(3, 3) + M(3, 1);

    // top
    m_aShadowFrustums[i][3].x = M(0, 3) - M(0, 1);
    m_aShadowFrustums[i][3].y = M(1, 3) - M(1, 1);
    m_aShadowFrustums[i][3].z = M(2, 3) - M(2, 1);
    m_aShadowFrustums[i][3].w = M(3, 3) - M(3, 1);

    for (int j = 0; j < 4; ++j) {
        XMVECTOR v = XMPlaneNormalize(XMLoadFloat4(&m_aShadowFrustums[i][j]));
        XMStoreFloat4(&m_aShadowFrustums[i][j], v);
    }
}

void DayNightCycle::GetShadowFrustum(int i, XMFLOAT4 planes[6])
{
    for (int j = 0; j < 4; ++j) {
        planes[j] = m_aShadowFrustums[i][j];
    }
}
