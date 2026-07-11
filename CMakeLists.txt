cmake_minimum_required(VERSION 3.20)
project(TexturifyCpp VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
  # OBJC (not just OBJCXX) is required so GLFW's own vendored .m files
  # compile as plain Objective-C (permissive void*->T* conversions, like C) —
  # without it CMake drives .m files through the C++ frontend too, and
  # GLFW's Cocoa backend fails to build under C++'s stricter pointer-
  # conversion rules.
  enable_language(OBJC)
  enable_language(OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 20)
endif()

if(MSVC)
  add_compile_options(/utf-8 /fp:precise /bigobj)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# ── GLFW ─────────────────────────────────────────────────────────────────────
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
# X11 only — Wayland defaults ON on Linux and needs wayland-scanner/
# wayland-protocols at configure time; X11 alone is sufficient here and
# keeps the Linux CI/build dependency list shorter.
set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

# ── Dear ImGui (docking) ─────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)

# ── miniz ────────────────────────────────────────────────────────────────────
add_library(miniz STATIC third_party/miniz/miniz.c)
target_include_directories(miniz PUBLIC third_party/miniz)

# ── Core mesh library (UI-free port of the JS pipeline) ─────────────────────
file(GLOB CORE_SOURCES src/core/*.cpp)
add_library(texturify_core STATIC ${CORE_SOURCES})
target_include_directories(texturify_core PUBLIC src)
target_include_directories(texturify_core PRIVATE third_party/stb)
target_link_libraries(texturify_core PUBLIC miniz)

# ── Application ──────────────────────────────────────────────────────────────
# file_dialog_{win32,linux}.cpp and file_dialog_macos.mm/mac_bundle_path.mm
# all define the same symbols (or are Apple-only .mm, already excluded by the
# *.cpp glob) — pick exactly one native-dialog TU per platform below.
file(GLOB APP_SOURCES src/app/*.cpp src/render/*.cpp src/ui/*.cpp)
list(REMOVE_ITEM APP_SOURCES
  ${CMAKE_SOURCE_DIR}/src/app/file_dialog_win32.cpp
  ${CMAKE_SOURCE_DIR}/src/app/file_dialog_linux.cpp)

if(WIN32)
  list(APPEND APP_SOURCES src/app/file_dialog_win32.cpp)
elseif(APPLE)
  list(APPEND APP_SOURCES src/app/file_dialog_macos.mm src/app/mac_bundle_path.mm)
else()
  list(APPEND APP_SOURCES src/app/file_dialog_linux.cpp)
endif()

if(WIN32)
  add_executable(texturify WIN32 src/main.cpp ${APP_SOURCES} assets/icon/app.rc)
elseif(APPLE)
  add_executable(texturify MACOSX_BUNDLE src/main.cpp ${APP_SOURCES} assets/icon/app.icns)
  set_source_files_properties(assets/icon/app.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(texturify PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Texturify"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.texturify.app"
    MACOSX_BUNDLE_ICON_FILE "app.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  )
else()
  add_executable(texturify src/main.cpp ${APP_SOURCES})
endif()

target_include_directories(texturify PRIVATE src third_party/stb)

if(WIN32)
  target_link_libraries(texturify PRIVATE texturify_core imgui glfw opengl32 comdlg32 ole32 shell32)
elseif(APPLE)
  target_link_libraries(texturify PRIVATE texturify_core imgui glfw "-framework Cocoa")
else()
  target_link_libraries(texturify PRIVATE texturify_core imgui glfw)
endif()

# ── Verification harness ─────────────────────────────────────────────────────
file(GLOB VERIFY_SOURCES tools/verify/*.cpp)
if(VERIFY_SOURCES)
  add_executable(texturify_verify ${VERIFY_SOURCES})
  target_include_directories(texturify_verify PRIVATE src third_party/stb)
  target_link_libraries(texturify_verify PRIVATE texturify_core)
endif()

# ── Assets ───────────────────────────────────────────────────────────────────
if(WIN32)
  # Copy assets next to the binary for debugging convenience.
  add_custom_command(TARGET texturify POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:texturify>/assets)
elseif(APPLE)
  # Assets live inside the .app bundle (Contents/Resources/assets); see
  # resolveAssetDir() in main.cpp.
  add_custom_command(TARGET texturify POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/assets $<TARGET_BUNDLE_CONTENT_DIR:texturify>/Resources/assets)
else()
  # Copy next to the binary for local/dev builds; the .deb additionally
  # installs assets to /usr/share/texturify/assets (see install() rules and
  # resolveAssetDir()'s fallback below).
  add_custom_command(TARGET texturify POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:texturify>/assets)
endif()

# ── Linux packaging (.deb via CPack) ────────────────────────────────────────
if(UNIX AND NOT APPLE)
  install(TARGETS texturify RUNTIME DESTINATION bin)
  install(DIRECTORY assets/ DESTINATION share/texturify/assets)
  install(FILES assets/icon/icon_256.png
    DESTINATION share/icons/hicolor/256x256/apps RENAME texturify.png)
  install(FILES packaging/linux/texturify.desktop DESTINATION share/applications)

  set(CPACK_PACKAGE_NAME "texturify")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_PACKAGE_VENDOR "Texturify")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native mesh-texturing / displacement tool")
  set(CPACK_PACKAGE_CONTACT "otisfitzpatrickzxxf@outlook.com")
  set(CPACK_GENERATOR "DEB")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "otisfitzpatrickzxxf@outlook.com")
  set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
  set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
  # X11/GL runtime libs GLFW needs; zenity is the file-picker (see
  # file_dialog_linux.cpp) — Recommends rather than Depends since kdialog is
  # an equally valid fallback and the app degrades gracefully without either.
  set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6, libgl1, libx11-6, libxrandr2, libxinerama1, libxcursor1, libxi6")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "zenity")
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")
  include(CPack)
endif()
