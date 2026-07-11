#include "render/gl_util.h"

#include <cstdio>
#include <cstring>
#include <vector>

#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif

namespace render {

namespace {
GLuint compile(GLenum type, const char* src, const char* debugName) {
  GLuint sh = glCreateShader(type);
  glShaderSource(sh, 1, &src, nullptr);
  glCompileShader(sh);
  GLint ok = 0;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096];
    GLsizei len = 0;
    glGetShaderInfoLog(sh, sizeof(log), &len, log);
    std::fprintf(stderr, "[%s] %s shader compile failed:\n%.*s\n", debugName,
                 type == GL_VERTEX_SHADER ? "vertex" : "fragment", len, log);
    glDeleteShader(sh);
    return 0;
  }
  return sh;
}
} // namespace

bool ShaderProgram::build(const char* vertexSrc, const char* fragmentSrc,
                          const char* debugName) {
  destroy();
  GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc, debugName);
  GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc, debugName);
  if (!vs || !fs) {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return false;
  }
  _prog = glCreateProgram();
  glAttachShader(_prog, vs);
  glAttachShader(_prog, fs);
  glLinkProgram(_prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(_prog, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[4096];
    GLsizei len = 0;
    glGetProgramInfoLog(_prog, sizeof(log), &len, log);
    std::fprintf(stderr, "[%s] link failed:\n%.*s\n", debugName, len, log);
    glDeleteProgram(_prog);
    _prog = 0;
    return false;
  }
  return true;
}

void ShaderProgram::destroy() {
  if (_prog) {
    glDeleteProgram(_prog);
    _prog = 0;
  }
  _uniforms.clear();
}

GLint ShaderProgram::uniform(const char* name) {
  auto it = _uniforms.find(name);
  if (it != _uniforms.end()) return it->second;
  GLint loc = glGetUniformLocation(_prog, name);
  _uniforms.emplace(name, loc);
  return loc;
}

void GlMesh::upload(const std::vector<VertexAttrib>& attribs,
                    const std::vector<const float*>& buffers,
                    size_t vertexCount, GLenum usage) {
  destroy();
  _vertexCount = vertexCount;
  if (vertexCount == 0) return;
  glGenVertexArrays(1, &_vao);
  glBindVertexArray(_vao);
  _vbos.resize(attribs.size());
  glGenBuffers((GLsizei)_vbos.size(), _vbos.data());
  for (size_t i = 0; i < attribs.size(); i++) {
    glBindBuffer(GL_ARRAY_BUFFER, _vbos[i]);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(vertexCount * attribs[i].components * sizeof(float)),
                 buffers[i], usage);
    glEnableVertexAttribArray(attribs[i].location);
    glVertexAttribPointer(attribs[i].location, attribs[i].components, GL_FLOAT,
                          GL_FALSE, 0, nullptr);
  }
  glBindVertexArray(0);
}

void GlMesh::destroy() {
  if (!_vbos.empty()) {
    glDeleteBuffers((GLsizei)_vbos.size(), _vbos.data());
    _vbos.clear();
  }
  if (_vao) {
    glDeleteVertexArrays(1, &_vao);
    _vao = 0;
  }
  _vertexCount = 0;
}

void GlMesh::draw(GLenum mode) const {
  if (!_vao || _vertexCount == 0) return;
  glBindVertexArray(_vao);
  glDrawArrays(mode, 0, (GLsizei)_vertexCount);
  glBindVertexArray(0);
}

void GlMesh::drawInstanced(GLenum mode, GLsizei vertsPerInstance,
                           GLsizei instances) const {
  if (!_vao || instances == 0) return;
  glBindVertexArray(_vao);
  glDrawArraysInstanced(mode, 0, vertsPerInstance, instances);
  glBindVertexArray(0);
}

GLuint createTextureRGBA(const uint8_t* pixels, int w, int h, bool repeat,
                         bool linear, bool mipmaps, bool flipY) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  std::vector<uint8_t> flipped;
  const uint8_t* upload = pixels;
  if (flipY && pixels) {
    flipped.resize((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
      std::memcpy(&flipped[(size_t)(h - 1 - y) * w * 4],
                  &pixels[(size_t)y * w * 4], (size_t)w * 4);
    upload = flipped.data();
  }
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               upload);
  GLenum wrap = repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                  linear ? GL_LINEAR : GL_NEAREST);
  if (mipmaps) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    glGenerateMipmap(GL_TEXTURE_2D);
  } else {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    linear ? GL_LINEAR : GL_NEAREST);
  }
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

GLuint createDataTextureRGBA32F(const float* data, int texels) {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, texels, 1, 0, GL_RGBA, GL_FLOAT,
               data);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

} // namespace render
