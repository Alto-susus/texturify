// Minimal OpenGL 3.3 core loader — loads exactly the functions this app uses
// via glfwGetProcAddress. No external loader dependency.
#pragma once

#if defined(_WIN32)
  #ifndef GLAPIENTRY
    #define GLAPIENTRY __stdcall
  #endif
#else
  #define GLAPIENTRY
#endif

#include <cstddef>
#include <cstdint>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

// ── Enums (subset) ───────────────────────────────────────────────────────────
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_ZERO 0
#define GL_ONE 1
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LEQUAL 0x0203
#define GL_LESS 0x0201
#define GL_ALWAYS 0x0207
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_MULTISAMPLE 0x809D
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_RED 0x1903
#define GL_DEPTH_COMPONENT 0x1902
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_R8 0x8229
#define GL_RGBA8 0x8058
#define GL_RGB8 0x8051
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_MAX_SAMPLES 0x8D57
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_VIEWPORT 0x0BA2
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_LINE_SMOOTH 0x0B20
#define GL_NO_ERROR 0

// ── Function pointer declarations ────────────────────────────────────────────
#define GL_FUNC_LIST(X) \
  X(void, glEnable, (GLenum cap)) \
  X(void, glDisable, (GLenum cap)) \
  X(void, glClear, (GLbitfield mask)) \
  X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
  X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h)) \
  X(void, glDepthFunc, (GLenum func)) \
  X(void, glDepthMask, (GLboolean flag)) \
  X(void, glBlendFunc, (GLenum s, GLenum d)) \
  X(void, glCullFace, (GLenum mode)) \
  X(void, glLineWidth, (GLfloat width)) \
  X(void, glPolygonMode, (GLenum face, GLenum mode)) \
  X(void, glPolygonOffset, (GLfloat factor, GLfloat units)) \
  X(void, glPixelStorei, (GLenum pname, GLint param)) \
  X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void* pixels)) \
  X(GLenum, glGetError, (void)) \
  X(void, glGetIntegerv, (GLenum pname, GLint* data)) \
  X(void, glGetFloatv, (GLenum pname, GLfloat* data)) \
  X(const GLubyte*, glGetString, (GLenum name)) \
  X(void, glGenTextures, (GLsizei n, GLuint* textures)) \
  X(void, glDeleteTextures, (GLsizei n, const GLuint* textures)) \
  X(void, glBindTexture, (GLenum target, GLuint texture)) \
  X(void, glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void* data)) \
  X(void, glTexSubImage2D, (GLenum target, GLint level, GLint xo, GLint yo, GLsizei w, GLsizei h, GLenum fmt, GLenum type, const void* data)) \
  X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
  X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param)) \
  X(void, glGenerateMipmap, (GLenum target)) \
  X(void, glActiveTexture, (GLenum texture)) \
  X(GLuint, glCreateShader, (GLenum type)) \
  X(void, glDeleteShader, (GLuint shader)) \
  X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
  X(void, glCompileShader, (GLuint shader)) \
  X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params)) \
  X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
  X(GLuint, glCreateProgram, (void)) \
  X(void, glDeleteProgram, (GLuint program)) \
  X(void, glAttachShader, (GLuint program, GLuint shader)) \
  X(void, glLinkProgram, (GLuint program)) \
  X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params)) \
  X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
  X(void, glUseProgram, (GLuint program)) \
  X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name)) \
  X(void, glUniform1i, (GLint location, GLint v0)) \
  X(void, glUniform1f, (GLint location, GLfloat v0)) \
  X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
  X(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2)) \
  X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
  X(void, glUniform2fv, (GLint location, GLsizei count, const GLfloat* value)) \
  X(void, glUniform3fv, (GLint location, GLsizei count, const GLfloat* value)) \
  X(void, glUniformMatrix3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
  X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
  X(void, glGenBuffers, (GLsizei n, GLuint* buffers)) \
  X(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers)) \
  X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
  X(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage)) \
  X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data)) \
  X(void, glGenVertexArrays, (GLsizei n, GLuint* arrays)) \
  X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays)) \
  X(void, glBindVertexArray, (GLuint array)) \
  X(void, glEnableVertexAttribArray, (GLuint index)) \
  X(void, glDisableVertexAttribArray, (GLuint index)) \
  X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer)) \
  X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
  X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices)) \
  X(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers)) \
  X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* framebuffers)) \
  X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
  X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
  X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer)) \
  X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
  X(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter)) \
  X(void, glGenRenderbuffers, (GLsizei n, GLuint* renderbuffers)) \
  X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* renderbuffers)) \
  X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer)) \
  X(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei w, GLsizei h)) \
  X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h)) \
  X(void, glTexImage2DMultisample, (GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h, GLboolean fixedsamplelocations)) \
  X(void, glVertexAttribDivisor, (GLuint index, GLuint divisor)) \
  X(void, glDrawArraysInstanced, (GLenum mode, GLint first, GLsizei count, GLsizei instancecount))

#define GL_DECLARE(ret, name, args) \
  typedef ret (GLAPIENTRY *PFN_##name) args; \
  extern PFN_##name name;
GL_FUNC_LIST(GL_DECLARE)
#undef GL_DECLARE

// Loads all functions above. Returns false if any core function is missing.
bool glLoaderInit();
