# Texturify

A native C++ desktop app for applying surface displacement textures to 3D meshes (STL / OBJ / 3MF
in, textured STL / 3MF out). Texturify is inspired by **[BumpMesh](https://bumpmesh.com)**
([CNCKitchen/stlTexturizer](https://github.com/CNCKitchen/stlTexturizer)) by Stefan Hermann / CNC
Kitchen — this is an independent native rewrite, not the original project.

The mesh-processing pipeline (adaptive subdivision → regularization → displacement → QEM decimation → repair), the 24 built-in textures, and all functionality are ported one-to-one from the original web application. The UI is a new dark "liquid glass" design (Dear ImGui + GLFW + OpenGL 3.3).

## Building (Windows)

Requires Visual Studio 2022 and CMake ≥ 3.20.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
build/Release/texturify.exe
```

The original web app source is expected in `reference/` (git-ignored) for the golden verification tests:

```bash
git clone https://github.com/CNCKitchen/stlTexturizer.git reference
```

## Third-party

- [Dear ImGui](https://github.com/ocornut/imgui) (MIT), [GLFW](https://glfw.org) (zlib), [stb](https://github.com/nothings/stb) (public domain/MIT), [miniz](https://github.com/richgel999/miniz) (MIT)
- Fonts: Instrument Sans, Noto Sans / JP / KR (OFL), JetBrains Mono (OFL)
- Texture images and translations from the original project (AGPL-3.0)

## License

GNU AGPL v3.0 — see [LICENSE](LICENSE).

Original work © CNCKitchen (Stefan Hermann). Texturify is derivative work under the same license.
