// Verification harness: compares the C++ core against golden outputs dumped
// from the reference JS modules (tools/golden/*.mjs → tools/golden/out/).
// Usage: texturify_verify <test> [args...]
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"

#include "core/exporter.h"
#include "core/geometry.h"
#include "core/loaders.h"
#include "core/mesh_index.h"

namespace {

// Default cube from the app (loadDefaultCube in main.js): 20mm cube, 12 tris.
core::Geometry makeCubeSoup(double half = 10.0) {
  // 8 corners
  const double c[8][3] = {
      {-half, -half, -half}, {half, -half, -half}, {half, half, -half},
      {-half, half, -half},  {-half, -half, half}, {half, -half, half},
      {half, half, half},    {-half, half, half}};
  // 12 triangles (2 per face), CCW outward
  const int f[12][3] = {{0, 3, 2}, {0, 2, 1},   // bottom (z-)
                        {4, 5, 6}, {4, 6, 7},   // top (z+)
                        {0, 1, 5}, {0, 5, 4},   // front (y-)
                        {2, 3, 7}, {2, 7, 6},   // back (y+)
                        {1, 2, 6}, {1, 6, 5},   // right (x+)
                        {3, 0, 4}, {3, 4, 7}};  // left (x-)
  core::Geometry g;
  for (auto& tri : f)
    for (int v : tri) {
      g.positions.push_back((float)c[v][0]);
      g.positions.push_back((float)c[v][1]);
      g.positions.push_back((float)c[v][2]);
    }
  return g;
}

void testWeld() {
  std::printf("[weld]\n");
  core::Geometry cube = makeCubeSoup();
  auto weld = core::weldVertices(cube.positions.data(), cube.positions.size() / 3, 1e5);
  check(weld.uniqueCount == 8, "cube welds to 8 unique vertices");
  check(weld.vertexId.size() == 36, "36 vertex ids");

  // near-duplicate points within grid cell collapse
  float pts[6] = {1.000001f, 2.0f, 3.0f, 1.000004f, 2.0f, 3.0f};
  auto w2 = core::weldVertices(pts, 2, 1e5);
  check(w2.uniqueCount == 1, "1e5 grid merges 0.003um-apart points");
  auto w3 = core::weldVertices(pts, 2, 1e6);
  // at 1e6, 1.000001*1e6 rounds to 1000001, 1.000004*1e6 → 1000004 → distinct
  check(w3.uniqueCount == 2, "1e6 grid keeps them distinct");
}

void testSTLRoundTrip() {
  std::printf("[stl round-trip]\n");
  core::Geometry cube = makeCubeSoup();
  core::computeVertexNormals(cube);
  auto stl = core::exportSTL(cube);
  check(stl.size() == 84 + 50 * 12, "binary STL size for 12 tris");

  auto loaded = core::loadSTL(stl.data(), stl.size());
  check(loaded.ok, "binary STL loads: " + loaded.error);
  check(loaded.geometry.triangleCount() == 12, "12 triangles after load");
  check(loaded.bounds.size.x == 20 && loaded.bounds.size.y == 20 &&
            loaded.bounds.size.z == 20,
        "bounds 20x20x20");

  // positions must round-trip bit-exactly (cube already centred)
  bool exact = loaded.geometry.positions.size() == cube.positions.size();
  if (exact)
    exact = std::memcmp(loaded.geometry.positions.data(), cube.positions.data(),
                        cube.positions.size() * 4) == 0;
  check(exact, "positions bit-exact after round-trip");
}

void testASCIISTL() {
  std::printf("[ascii stl]\n");
  const char* ascii =
      "solid test\n"
      " facet normal 0 0 1\n"
      "  outer loop\n"
      "   vertex 0 0 0\n"
      "   vertex 10 0 0\n"
      "   vertex 0 10 0\n"
      "  endloop\n"
      " endfacet\n"
      "endsolid test\n";
  auto r = core::loadSTL((const uint8_t*)ascii, std::strlen(ascii));
  check(r.ok, "ascii STL loads: " + r.error);
  check(r.geometry.triangleCount() == 1, "1 triangle");
  check(r.originOffset.x == 5 && r.originOffset.y == 5 && r.originOffset.z == 0,
        "centered with originOffset (5,5,0)");
}

void test3MFRoundTrip() {
  std::printf("[3mf round-trip]\n");
  core::Geometry cube = makeCubeSoup();
  auto zip = core::export3MF(cube);
  check(zip.size() > 200, "3MF produced");
  auto r = core::load3MF(zip.data(), zip.size());
  check(r.ok, "3MF loads: " + r.error);
  check(r.geometry.triangleCount() == 12, "12 triangles after 3MF round-trip");
  check(r.bounds.size.x == 20 && r.bounds.size.y == 20 && r.bounds.size.z == 20,
        "bounds preserved");
}

void testFmt() {
  std::printf("[3mf coord fmt]\n");
  check(core::fmt3MFCoord(1.0) == "1", "1.0 -> 1");
  check(core::fmt3MFCoord(-2.5) == "-2.5", "-2.5 -> -2.5");
  check(core::fmt3MFCoord(0.12345) == "0.1235" || core::fmt3MFCoord(0.12345) == "0.1234",
        "0.12345 -> 4 decimals (" + core::fmt3MFCoord(0.12345) + ")");
  check(core::fmt3MFCoord(3.10000) == "3.1", "3.1 trims zeros");
  check(core::fmt3MFCoord(0.0) == "0", "0 -> 0");
  check(core::fmt3MFCoord((double)0.03125f) == "0.0313", "tie 0.03125 rounds up (JS toFixed)");
}

void testSurfaceArea() {
  std::printf("[surface area]\n");
  core::Geometry cube = makeCubeSoup();
  double area = core::computeSurfaceArea(cube);
  check(std::abs(area - 2400.0) < 1e-9, "cube area 2400 mm^2");
}

void testWeldGolden() {
  std::printf("[weld golden vs reference JS]\n");
  auto input = readFile("tools/golden/out/weld_input.f32");
  if (input.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_weld.mjs` first\n");
    return;
  }
  size_t count = input.size() / 12;
  const float* pos = (const float*)input.data();

  const struct { double quant; const char* file; } grids[] = {
      {1e4, "tools/golden/out/weld_1e4.bin"},
      {1e5, "tools/golden/out/weld_1e5.bin"},
      {1e6, "tools/golden/out/weld_1e6.bin"},
  };
  for (const auto& grid : grids) {
    auto golden = readFile(grid.file);
    if (golden.size() < 4) {
      std::printf("  SKIP  missing %s\n", grid.file);
      continue;
    }
    uint32_t goldenUnique;
    std::memcpy(&goldenUnique, golden.data(), 4);
    auto weld = core::weldVertices(pos, count, grid.quant);
    bool idsMatch =
        golden.size() == 4 + count * 4 &&
        std::memcmp(weld.vertexId.data(), golden.data() + 4, count * 4) == 0;
    check(weld.uniqueCount == goldenUnique && idsMatch,
          std::string(grid.file) + " ids bit-identical (unique=" +
              std::to_string(weld.uniqueCount) + ")");
  }
}

} // namespace

int main(int argc, char** argv) {
  std::string filter = argc > 1 ? argv[1] : "";
  auto want = [&](const char* name) {
    return filter.empty() || filter == name;
  };

  if (want("weld")) testWeld();
  if (want("weldgolden")) testWeldGolden();
  if (want("stl")) testSTLRoundTrip();
  if (want("ascii")) testASCIISTL();
  if (want("3mf")) test3MFRoundTrip();
  if (want("fmt")) testFmt();
  if (want("area")) testSurfaceArea();

  runPipelineGoldenTests(filter);

  std::printf("\n%s (%d failures)\n", g_failures ? "FAILED" : "OK",
              g_failures);
  return g_failures ? 1 : 0;
}
