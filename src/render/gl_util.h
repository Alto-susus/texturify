// Small OpenGL 3.3 helpers: shader programs with uniform cache, VAO/VBO
// meshes, and texture creation.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/gl_loader.h"

namespace render {

class ShaderProgram {
public:
  ShaderProgram() = default;
  ~ShaderProgram() { destroy(); }
  ShaderProgram(const ShaderProgram&) = delete;
  ShaderProgram& operator=(const ShaderProgram&) = delete;

  // Compiles + links; on failure logs to stderr and id() stays 0.
  bool build(const char* vertexSrc, const char* fragmentSrc,
             const char* debugName = "shader");
  void destroy();

  GLuint id() const { return _prog; }
  void use() const { glUseProgram(_prog); }

  GLint uniform(const char* name);
  void set1i(const char* name, int v) { glUniform1i(uniform(name), v); }
  void set1f(const char* name, float v) { glUniform1f(uniform(name), v); }
  void set2f(const char* name, float a, float b) { glUniform2f(uniform(name), a, b); }
  void set3f(const char* name, float a, float b, float c) { glUniform3f(uniform(name), a, b, c); }
  void set4f(const char* name, float a, float b, float c, float d) { glUniform4f(uniform(name), a, b, c, d); }
  void setMat4(const char* name, const float* m16) { glUniformMatrix4fv(uniform(name), 1, GL_FALSE, m16); }
  void setMat3(const char* name, const float* m9) { glUniformMatrix3fv(uniform(name), 1, GL_FALSE, m9); }

private:
  GLuint _prog = 0;
  std::unordered_map<std::string, GLint> _uniforms;
};

// Interleaved or multi-buffer vertex data with a VAO. Attributes are float.
struct VertexAttrib {
  GLuint location;
  GLint components; // 1..4
};

class GlMesh {
public:
  GlMesh() = default;
  ~GlMesh() { destroy(); }
  GlMesh(const GlMesh&) = delete;
  GlMesh& operator=(const GlMesh&) = delete;
  GlMesh(GlMesh&& o) noexcept
      : _vao(o._vao), _vbos(std::move(o._vbos)), _vertexCount(o._vertexCount) {
    o._vao = 0;
    o._vbos.clear();
    o._vertexCount = 0;
  }
  GlMesh& operator=(GlMesh&& o) noexcept {
    if (this != &o) {
      destroy();
      _vao = o._vao;
      _vbos = std::move(o._vbos);
      _vertexCount = o._vertexCount;
      o._vao = 0;
      o._vbos.clear();
      o._vertexCount = 0;
    }
    return *this;
  }

  // One tightly packed float buffer per attribute, all with `vertexCount`
  // vertices. buffers[i] pairs with attribs[i].
  void upload(const std::vector<VertexAttrib>& attribs,
              const std::vector<const float*>& buffers, size_t vertexCount,
              GLenum usage = GL_STATIC_DRAW);
  void destroy();

  void draw(GLenum mode) const;
  void drawInstanced(GLenum mode, GLsizei vertsPerInstance,
                     GLsizei instances) const;

  bool empty() const { return _vertexCount == 0; }
  size_t vertexCount() const { return _vertexCount; }
  GLuint vao() const { return _vao; }

private:
  GLuint _vao = 0;
  std::vector<GLuint> _vbos;
  size_t _vertexCount = 0;
};

// RGBA8 texture from raw pixels (row 0 = top, like Canvas ImageData).
// flipY replicates three.js CanvasTexture upload (texture V=0 at bottom).
GLuint createTextureRGBA(const uint8_t* pixels, int w, int h, bool repeat,
                         bool linear = true, bool mipmaps = true,
                         bool flipY = true);

// Single-row RGBA32F data texture (boundary edge points).
GLuint createDataTextureRGBA32F(const float* data, int texels);

} // namespace render
