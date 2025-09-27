#include "Camera.h"
#include <cmath>

using namespace DirectX;

Camera::Camera(int h, int w)
{
    m_angleYaw = m_anglePitch = m_angleRoll = 0.0f;
    m_wScreen = w;
    m_hScreen = h;

    m_fovVertical = 60.0f;
    const double tmp =
        std::atan(std::tan(XMConvertToRadians(m_fovVertical) * 0.5) * double(w) / double(h)) * 2.0;
    m_fovHorizontal = XMConvertToDegrees(float(tmp));

    // LH-проекция
    const XMMATRIX proj = XMMatrixPerspectiveFovLH(
        XMConvertToRadians(m_fovVertical),
        float(w) / float(h),
        0.1f, 3000.0f);
    XMStoreFloat4x4(&m_mProjection, proj);

    // Базис камеры
    m_vPos = XMFLOAT4(450.0f, -300.0f, 250.0f, 0.0f);

    // ВАЖНО: не брать адрес временных XMFLOAT4
    XMVECTOR look = XMVector3Normalize(XMVectorSet(1.0f, 1.0f, 0.0f, 0.0f));
    XMStoreFloat4(&m_vStartLook, look);

    XMVECTOR worldUp = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    XMVECTOR left = XMVector3Normalize(XMVector3Cross(look, worldUp));
    XMStoreFloat4(&m_vStartLeft, left);

    XMVECTOR up = XMVector3Normalize(XMVector3Cross(left, look));
    XMStoreFloat4(&m_vStartUp, up);

    Update();
}

Camera::~Camera() {}

XMFLOAT4X4 Camera::GetViewProjectionMatrixTransposed()
{
    const XMMATRIX view = XMLoadFloat4x4(&m_mView);
    const XMMATRIX proj = XMLoadFloat4x4(&m_mProjection);
    const XMMATRIX viewprojT = XMMatrixTranspose(view * proj);

    XMFLOAT4X4 out{};
    XMStoreFloat4x4(&out, viewprojT);
    return out;
}

void Camera::GetViewFrustum(XMFLOAT4 planes[6])
{
    const XMMATRIX view = XMLoadFloat4x4(&m_mView);
    const XMMATRIX proj = XMLoadFloat4x4(&m_mProjection);

    XMFLOAT4X4 M{};
    XMStoreFloat4x4(&M, view * proj);

    // row/col -> _mn (1-based)
    // left
    planes[0].x = M._14 + M._11;  planes[0].y = M._24 + M._21;
    planes[0].z = M._34 + M._31;  planes[0].w = M._44 + M._41;
    // right
    planes[1].x = M._14 - M._11;  planes[1].y = M._24 - M._21;
    planes[1].z = M._34 - M._31;  planes[1].w = M._44 - M._41;
    // bottom
    planes[2].x = M._14 + M._12;  planes[2].y = M._24 + M._22;
    planes[2].z = M._34 + M._32;  planes[2].w = M._44 + M._42;
    // top
    planes[3].x = M._14 - M._12;  planes[3].y = M._24 - M._22;
    planes[3].z = M._34 - M._32;  planes[3].w = M._44 - M._42;
    // near
    planes[4].x = M._14 + M._13;  planes[4].y = M._24 + M._23;
    planes[4].z = M._34 + M._33;  planes[4].w = M._44 + M._43;
    // far
    planes[5].x = M._14 - M._13;  planes[5].y = M._24 - M._23;
    planes[5].z = M._34 - M._33;  planes[5].w = M._44 - M._43;

    for (int i = 0; i < 6; ++i) {
        const XMVECTOR v = XMPlaneNormalize(XMLoadFloat4(&planes[i]));
        XMStoreFloat4(&planes[i], v);
    }
}

Frustum Camera::CalculateFrustumByNearFar(float zNear, float zFar)
{
    Frustum f{};

    const float tanHalfHFOV = std::tanf(XMConvertToRadians(m_fovHorizontal * 0.5f));
    const float tanHalfVFOV = std::tanf(XMConvertToRadians(m_fovVertical * 0.5f));

    const float xNear = zNear * tanHalfHFOV;
    const float xFar = zFar * tanHalfHFOV;
    const float yNear = zNear * tanHalfVFOV;
    const float yFar = zFar * tanHalfVFOV;

    // В вид-пространстве (LH): +z от камеры
    f.nlb = XMFLOAT3(-xNear, -yNear, zNear);
    f.nrb = XMFLOAT3(xNear, -yNear, zNear);
    f.nlt = XMFLOAT3(-xNear, yNear, zNear);
    f.nrt = XMFLOAT3(xNear, yNear, zNear);

    f.flb = XMFLOAT3(-xFar, -yFar, zFar);
    f.frb = XMFLOAT3(xFar, -yFar, zFar);
    f.flt = XMFLOAT3(-xFar, yFar, zFar);
    f.frt = XMFLOAT3(xFar, yFar, zFar);

    // Переход в мир: inverse(View)
    const XMMATRIX view = XMLoadFloat4x4(&m_mView);
    const XMMATRIX invView = XMMatrixInverse(nullptr, view);

    XMVECTOR nlb = XMLoadFloat3(&f.nlb);
    XMVECTOR nrb = XMLoadFloat3(&f.nrb);
    XMVECTOR nlt = XMLoadFloat3(&f.nlt);
    XMVECTOR nrt = XMLoadFloat3(&f.nrt);
    XMVECTOR flb = XMLoadFloat3(&f.flb);
    XMVECTOR frb = XMLoadFloat3(&f.frb);
    XMVECTOR flt = XMLoadFloat3(&f.flt);
    XMVECTOR frt = XMLoadFloat3(&f.frt);

    // Используем TransformCoord и деление на w
    nlb = XMVector3TransformCoord(nlb, invView);
    nrb = XMVector3TransformCoord(nrb, invView);
    nlt = XMVector3TransformCoord(nlt, invView);
    nrt = XMVector3TransformCoord(nrt, invView);
    flb = XMVector3TransformCoord(flb, invView);
    frb = XMVector3TransformCoord(frb, invView);
    flt = XMVector3TransformCoord(flt, invView);
    frt = XMVector3TransformCoord(frt, invView);

    XMStoreFloat3(&f.nlb, nlb);
    XMStoreFloat3(&f.nrb, nrb);
    XMStoreFloat3(&f.nlt, nlt);
    XMStoreFloat3(&f.nrt, nrt);
    XMStoreFloat3(&f.flb, flb);
    XMStoreFloat3(&f.frb, frb);
    XMStoreFloat3(&f.flt, flt);
    XMStoreFloat3(&f.frt, frt);

    f.bs = FindBoundingSphere(f.nlb, f.flb, f.frt);
    return f;
}

void Camera::Translate(XMFLOAT3 move)
{
    XMVECTOR look = XMLoadFloat4(&m_vCurLook);
    XMVECTOR left = XMLoadFloat4(&m_vCurLeft);
    XMVECTOR up = XMLoadFloat4(&m_vCurUp);
    XMVECTOR p = XMLoadFloat4(&m_vPos);

    p += look * move.x + left * move.y + up * move.z;
    XMStoreFloat4(&m_vPos, p);

    Update();
}

void Camera::Pitch(float theta)
{
    m_anglePitch += theta;
    if (m_anglePitch > 360.0f) m_anglePitch -= 360.0f;
    else if (m_anglePitch < -360.0f) m_anglePitch += 360.0f;
    Update();
}

void Camera::Yaw(float theta)
{
    m_angleYaw += theta;
    if (m_angleYaw > 360.0f) m_angleYaw -= 360.0f;
    else if (m_angleYaw < -360.0f) m_angleYaw += 360.0f;
    Update();
}

void Camera::Roll(float theta)
{
    m_angleRoll += theta;
    if (m_angleRoll > 360.0f) m_angleRoll -= 360.0f;
    else if (m_angleRoll < -360.0f) m_angleRoll += 360.0f;
    Update();
}

void Camera::LockPosition(XMFLOAT4 p)
{
    m_vPos = p;
    Update();
}

void Camera::Update()
{
    XMVECTOR look = XMLoadFloat4(&m_vStartLook);
    XMVECTOR up0 = XMLoadFloat4(&m_vStartUp);
    XMVECTOR left0 = XMLoadFloat4(&m_vStartLeft);

    const float pitch_rad = XMConvertToRadians(m_anglePitch);
    const float yaw_rad = XMConvertToRadians(m_angleYaw);
    const float roll_rad = XMConvertToRadians(m_angleRoll);

    const XMMATRIX rotp = XMMatrixRotationAxis(left0, pitch_rad);
    const XMMATRIX roty = XMMatrixRotationAxis(up0, yaw_rad);
    const XMMATRIX rotr = XMMatrixRotationAxis(look, roll_rad);

    const XMMATRIX rot = rotp * roty * rotr;

    look = XMVector3Normalize(XMVector3TransformNormal(look, rot));
    XMVECTOR left = XMVector3Normalize(XMVector3TransformNormal(left0, rot));
    XMVECTOR up = XMVector3Normalize(XMVector3Cross(left, look));

    XMStoreFloat4(&m_vCurLook, look);
    XMStoreFloat4(&m_vCurLeft, left);
    XMStoreFloat4(&m_vCurUp, up);

    const XMVECTOR cam = XMLoadFloat4(&m_vPos);
    const XMVECTOR tgt = cam + look;

    const XMMATRIX view = XMMatrixLookAtLH(cam, tgt, up);
    XMStoreFloat4x4(&m_mView, view);
}
