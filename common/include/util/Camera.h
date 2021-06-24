#pragma once

#include <DirectXMath.h>

namespace util {
    class Camera {
    public:
        using XMFLOAT3 = DirectX::XMFLOAT3;
        using XMFLOAT4 = DirectX::XMFLOAT4;
        using XMMATRIX = DirectX::XMMATRIX;
        using XMVECTOR = DirectX::XMVECTOR;

        Camera();
        void SetLookAt(
            XMFLOAT3 vPos, XMFLOAT3 vTarget, XMFLOAT3 vUp = XMFLOAT3(0.0f, 1.0f, 0.0f)
        );
        void SetPerspective(
            float fovY, float aspect, float znear, float zfar);

        XMMATRIX GetViewMatrix() const { return m_mtxView; }
        XMMATRIX GetProjectionMatrix() const { return m_mtxProj; }

        XMVECTOR GetPosition() const { return m_eye; }
        XMVECTOR GetTarget() const { return m_target; }

        void OnMouseButtonDown(int buttonType, float dx, float dy);
        void OnMouseMove(float dx, float dy);
        void OnMouseButtonUp();

    private:
        void CalcOrbit(float dx, float dy);
        void CalcDolly(float d);
    private:
        XMVECTOR m_eye;
        XMVECTOR m_target;
        XMVECTOR m_up;

        XMMATRIX m_mtxView;
        XMMATRIX m_mtxProj;

        bool m_isDragged;
        int m_buttonType;
    };
}
