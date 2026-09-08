#include "Camera.h"
#include "BVH.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace neurender {

Camera::Camera(glm::vec3 position, glm::vec3 target, glm::vec3 up, float fov)
    : m_Position(position), m_Target(target), m_WorldUp(up), m_Fov(fov) {
    UpdateFromTarget();
}

void Camera::SetLookAt(glm::vec3 eye, glm::vec3 target, glm::vec3 up) {
    m_Position = eye;
    m_Target = target;
    m_WorldUp = up;
    UpdateFromTarget();
    m_Dirty = true;
}

void Camera::ResetCornellBoxView() {
    m_Position = glm::vec3(3.2f, 1.0f, 0.0f);
    m_Target = glm::vec3(0.0f, 1.0f, 0.0f);
    m_WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    m_Fov = 49.0f;
    m_Aperture = 0.0f;
    m_FocusDist = 3.2f;
    UpdateFromTarget();
    m_Dirty = true;
}

void Camera::FrameBounds(const AABB& bounds) {
    glm::vec3 center = (bounds.min + bounds.max) * 0.5f;
    float radius = glm::length(bounds.max - bounds.min) * 0.5f;
    if (radius < 0.001f) radius = 1.0f;

    float fovRad = glm::radians(std::clamp(m_Fov, 10.0f, 120.0f));
    float dist = (radius / std::sin(fovRad * 0.5f)) * 1.25f;

    m_Target = center;
    m_Position = center + glm::vec3(dist * 0.85f, dist * 0.35f, dist * 0.45f);
    m_WorldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    m_FocusDist = glm::length(m_Position - m_Target);
    m_Aperture = 0.0f;

    UpdateFromTarget();
    m_Dirty = true;
}

void Camera::UpdateFromTarget() {
    glm::vec3 dir = glm::normalize(m_Target - m_Position);
    m_Pitch = glm::degrees(asin(std::clamp(dir.y, -0.999f, 0.999f)));
    m_Yaw = glm::degrees(atan2(dir.z, dir.x));
    UpdateCameraVectors();
}

void Camera::UpdateCameraVectors() {
    glm::vec3 front;
    front.x = cos(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
    front.y = sin(glm::radians(m_Pitch));
    front.z = sin(glm::radians(m_Yaw)) * cos(glm::radians(m_Pitch));
    m_Front = glm::normalize(front);

    m_Right = glm::normalize(glm::cross(m_Front, m_WorldUp));
    m_Up = glm::normalize(glm::cross(m_Right, m_Front));
}

void Camera::ProcessKeyboard(CameraMovement direction, float deltaTime, float speedMultiplier) {
    float velocity = m_Speed * deltaTime * speedMultiplier;
    if (direction == CameraMovement::Forward)
        m_Position += m_Front * velocity;
    if (direction == CameraMovement::Backward)
        m_Position -= m_Front * velocity;
    if (direction == CameraMovement::Left)
        m_Position -= m_Right * velocity;
    if (direction == CameraMovement::Right)
        m_Position += m_Right * velocity;
    if (direction == CameraMovement::Up)
        m_Position += m_WorldUp * velocity;
    if (direction == CameraMovement::Down)
        m_Position -= m_WorldUp * velocity;

    m_Target = m_Position + m_Front;
    m_Dirty = true;
}

void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= m_Sensitivity;
    yoffset *= m_Sensitivity;

    m_Yaw += xoffset;
    m_Pitch += yoffset;

    if (constrainPitch) {
        if (m_Pitch > 89.0f) m_Pitch = 89.0f;
        if (m_Pitch < -89.0f) m_Pitch = -89.0f;
    }

    UpdateCameraVectors();
    m_Target = m_Position + m_Front;
    m_Dirty = true;
}

void Camera::ProcessMouseOrbit(float xoffset, float yoffset, bool constrainPitch) {
    glm::vec3 focusPoint = m_Position + m_Front * m_FocusDist;

    xoffset *= m_Sensitivity;
    yoffset *= m_Sensitivity;

    m_Yaw += xoffset;
    m_Pitch += yoffset;

    if (constrainPitch) {
        if (m_Pitch > 89.0f) m_Pitch = 89.0f;
        if (m_Pitch < -89.0f) m_Pitch = -89.0f;
    }

    UpdateCameraVectors();
    m_Position = focusPoint - m_Front * m_FocusDist;
    m_Target = focusPoint;
    m_Dirty = true;
}

void Camera::ProcessMouseScroll(float yoffset) {
    m_Fov -= yoffset;
    if (m_Fov < 5.0f) m_Fov = 5.0f;
    if (m_Fov > 120.0f) m_Fov = 120.0f;
    m_Dirty = true;
}

} // namespace neurender
