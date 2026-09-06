#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace neurender {

enum class CameraMovement {
    Forward,
    Backward,
    Left,
    Right,
    Up,
    Down
};

class Camera {
public:
    Camera(glm::vec3 position = glm::vec3(3.2f, 1.0f, 0.0f),
           glm::vec3 target = glm::vec3(0.0f, 1.0f, 0.0f),
           glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f),
           float fov = 40.0f);

    void SetLookAt(glm::vec3 eye, glm::vec3 target, glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f));

    void ProcessKeyboard(CameraMovement direction, float deltaTime);
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    void ProcessMouseScroll(float yoffset);

    glm::vec3 GetPosition() const { return m_Position; }
    void SetPosition(glm::vec3 pos) { m_Position = pos; UpdateCameraVectors(); m_Dirty = true; }

    glm::vec3 GetTarget() const { return m_Target; }
    void SetTarget(glm::vec3 target) { m_Target = target; UpdateFromTarget(); m_Dirty = true; }

    glm::vec3 GetForward() const { return m_Front; }
    glm::vec3 GetUp() const { return m_Up; }
    glm::vec3 GetRight() const { return m_Right; }

    float GetFov() const { return m_Fov; }
    void SetFov(float fov) { m_Fov = fov; m_Dirty = true; }

    float GetAperture() const { return m_Aperture; }
    void SetAperture(float a) { m_Aperture = a; m_Dirty = true; }

    float GetFocusDistance() const { return m_FocusDist; }
    void SetFocusDistance(float d) { m_FocusDist = d; m_Dirty = true; }

    float GetYaw() const { return m_Yaw; }
    void SetYaw(float y) { m_Yaw = y; UpdateCameraVectors(); m_Dirty = true; }

    float GetPitch() const { return m_Pitch; }
    void SetPitch(float p) { m_Pitch = p; UpdateCameraVectors(); m_Dirty = true; }

    float GetSpeed() const { return m_Speed; }
    void SetSpeed(float s) { m_Speed = s; }

    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }

    void ResetCornellBoxView();

private:
    void UpdateCameraVectors();
    void UpdateFromTarget();

    glm::vec3 m_Position;
    glm::vec3 m_Target;
    glm::vec3 m_Front;
    glm::vec3 m_Up;
    glm::vec3 m_Right;
    glm::vec3 m_WorldUp;

    float m_Yaw;
    float m_Pitch;
    float m_Fov;
    float m_Speed = 2.5f;
    float m_Sensitivity = 0.15f;

    float m_Aperture = 0.0f;     // 0 = pinhole
    float m_FocusDist = 3.2f;    // focal plane distance

    bool m_Dirty = true;
};

} // namespace neurender
