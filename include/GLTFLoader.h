#pragma once
#include <string>

namespace neurender {

class Scene;

bool LoadGLTF(const std::string& filepath, Scene& scene);

} // namespace neurender
