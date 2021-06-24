#include "util/Camera.h"
#include <cmath>

using namespace DirectX;

namespace util {
    Camera::Camera() : m_eye(), m_target(), m_up(), m_buttonType(-1), m_isDragged(false)
    {
        m_mtxView = XMMatrixIdentity();
        m_mtxProj = XMMatrixIdentity();
    }

    void Camera::SetLookAt(XMFLOAT3 vPos, XMFLOAT3 vTarget, XMFLOAT3 vUp)
    {
        m_eye = XMLoadFloat3(&vPos);
        m_target = XMLoadFloat3(&vTarget);
        m_up = XMLoadFloat3(&vUp);
        m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
    }

    void Camera::SetPerspective(float fovY, float aspect, float znear, float zfar)
    {
        m_mtxProj = XMMatrixPerspectiveFovRH(fovY, aspect, znear, zfar);
    }

    void Camera::OnMouseButtonDown(int buttonType, float, float )
    {
        m_buttonType = buttonType;
    }

    void Camera::OnMouseMove(float dx, float dy) 
    {
        if (m_buttonType < 0)
        {
            return;
        }
        if (m_buttonType == 0)
        {
            CalcOrbit(dx, dy);
        }
        if (m_buttonType == 1)
        {
            CalcDolly(dy);
        }
    }
    void Camera::OnMouseButtonUp()
    {
        m_buttonType = -1;
    }

    void Camera::CalcOrbit(float dx, float dy)
    {
        auto toEye = m_eye - m_target;
        auto toEyeLength = XMVectorGetX(XMVector3Length(toEye));
        toEye = XMVector3Normalize(toEye);

        auto phi = std::atan2(XMVectorGetX(toEye), XMVectorGetZ(toEye)); // ���ʊp.
        auto theta = std::acos(XMVectorGetY(toEye));  // �p.
        
        // �E�B���h�E�̃T�C�Y�ړ����ɂ�
        //  - ���ʊp�� 360�x�����
        //  - �p�� ��180�x�����.
        auto x = (XM_PI + phi) / XM_2PI;
        auto y = theta / XM_PI;

        x += dx;
        y -= dy;
        y = std::fmax(0.02f, std::fmin(y, 0.98f));

        // �������烉�W�A���p�֕ϊ�.
        phi = x * XM_2PI;
        theta = y * XM_PI;

        auto st = std::sinf(theta);
        auto sp = std::sinf(phi);
        auto ct = std::cosf(theta);
        auto cp = std::cosf(phi);

        // �e�������V�J�����ʒu�ւ�3�����x�N�g���𐶐�.
        auto newToEye = XMVector3Normalize(XMVectorSet(-st * sp, ct, -st * cp, 0.0f));
        newToEye *= toEyeLength;
        m_eye = m_target + newToEye;

        m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
    }

    void Camera::CalcDolly(float d)
    {
        auto toTarget = m_target - m_eye;
        auto toTargetLength = XMVectorGetX(XMVector3Length(toTarget));
        if (toTargetLength < FLT_EPSILON) {
            return;
        }
        toTarget = XMVector3Normalize(toTarget);

        auto delta = toTargetLength * d;
        auto newLen = toTargetLength + delta;
        m_eye += toTarget * delta;

        m_mtxView = XMMatrixLookAtRH(m_eye, m_target, m_up);
    }
}
