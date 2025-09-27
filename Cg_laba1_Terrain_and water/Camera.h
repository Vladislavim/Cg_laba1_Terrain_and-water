#pragma once
#include <DirectXMath.h>
#include "BoundingVolume.h"

using namespace DirectX;

struct Frustum {
	XMFLOAT3 nlb;
	XMFLOAT3 nlt;
	XMFLOAT3 nrb;
	XMFLOAT3 nrt;
	XMFLOAT3 flb;
	XMFLOAT3 flt;
	XMFLOAT3 frb;
	XMFLOAT3 frt;
	BoundingSphere bs;
};

class Camera
{
public:
	Camera(int h, int w);
	~Camera();


	XMFLOAT4X4 GetViewProjectionMatrixTransposed();

	XMFLOAT4 GetEyePosition() { return m_vPos; }

	void GetViewFrustum(XMFLOAT4 planes[6]);

	void Translate(XMFLOAT3 move);

	void Pitch(float theta);

	void Yaw(float theta);

	void Roll(float theta);

	Frustum CalculateFrustumByNearFar(float near, float far);

	void LockPosition(XMFLOAT4 p);

private:
	void Update();

	XMFLOAT4X4	m_mProjection;
	XMFLOAT4X4	m_mView;
	XMFLOAT4	m_vPos;
	XMFLOAT4	m_vStartLook;
	XMFLOAT4	m_vStartUp;
	XMFLOAT4	m_vStartLeft;
	XMFLOAT4	m_vCurLook;
	XMFLOAT4	m_vCurUp;
	XMFLOAT4	m_vCurLeft;
	int			m_wScreen;
	int			m_hScreen;
	float		m_fovHorizontal;
	float		m_fovVertical;
	float		m_angleYaw;
	float		m_anglePitch;
	float		m_angleRoll;
};

