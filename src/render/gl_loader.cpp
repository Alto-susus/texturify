#include "render/gl_loader.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#define GL_DEFINE(ret, name, args) PFN_##name name = nullptr;
GL_FUNC_LIST(GL_DEFINE)
#undef GL_DEFINE

bool glLoaderInit() {
  bool ok = true;
#define GL_LOAD(ret, name, args) \
  name = (PFN_##name)glfwGetProcAddress(#name); \
  if (!name) ok = false;
  GL_FUNC_LIST(GL_LOAD)
#undef GL_LOAD
  return ok;
}
