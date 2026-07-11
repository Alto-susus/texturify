#include "core/loaders.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <set>

#include <miniz.h>

#include "core/jsmath.h"
#include "core/mat4.h"
#include "core/xml_mini.h"

namespace core {

namespace {

// ── little-endian readers ────────────────────────────────────────────────────
uint32_t rdU32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
float rdF32(const uint8_t* p) {
  uint32_t u = rdU32(p);
  float f;
  std::memcpy(&f, &u, 4);
  return f;
}

// JS parseFloat: leading whitespace ok, parses maximal float prefix, NaN if none.
double jsParseFloat(const std::string& s) {
  const char* c = s.c_str();
  char* end = nullptr;
  double v = std::strtod(c, &end);
  if (end == c) return std::numeric_limits<double>::quiet_NaN();
  return v;
}

// JS parseInt(s, 10): NaN if no digits.
double jsParseInt(const std::string& s) {
  const char* c = s.c_str();
  while (*c && std::isspace((unsigned char)*c)) c++;
  const char* start = c;
  if (*c == '+' || *c == '-') c++;
  if (!std::isdigit((unsigned char)*c)) return std::numeric_limits<double>::quiet_NaN();
  char* end = nullptr;
  double v = std::strtod(start, &end); // integer prefix parse
  return std::trunc(v);
}

// JS ToUint32 (Uint32Array store): NaN → 0, negative wraps modulo 2^32.
uint32_t jsToUint32(double x) {
  if (!std::isfinite(x)) return 0;
  double m = std::fmod(std::trunc(x), 4294967296.0);
  if (m < 0) m += 4294967296.0;
  return static_cast<uint32_t>(m);
}

// ── validateAndCleanGeometry (verbatim port) ─────────────────────────────────
struct CleanStats {
  int64_t nanCount = 0;
  int64_t degenerateCount = 0;
};

bool validateAndCleanGeometry(Geometry& geometry, CleanStats& stats,
                              std::string& error) {
  auto& src = geometry.positions;
  size_t triCount = src.size() / 9;

  size_t writeIdx = 0;
  int64_t nanCount = 0;
  int64_t degenerateCount = 0;

  for (size_t t = 0; t < triCount; t++) {
    size_t b = t * 9;
    double ax = src[b], ay = src[b + 1], az = src[b + 2];
    double bx = src[b + 3], by = src[b + 4], bz = src[b + 5];
    double cx = src[b + 6], cy = src[b + 7], cz = src[b + 8];

    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) ||
        !std::isfinite(bx) || !std::isfinite(by) || !std::isfinite(bz) ||
        !std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz)) {
      nanCount++;
      continue;
    }

    // Cross product of (B−A) × (C−A); skip if area² < 1e-24 (area < 1e-12)
    double ux = bx - ax, uy = by - ay, uz = bz - az;
    double vx = cx - ax, vy = cy - ay, vz = cz - az;
    double c1 = uy * vz - uz * vy, c2 = uz * vx - ux * vz, c3 = ux * vy - uy * vx;
    double area2 = c1 * c1 + c2 * c2 + c3 * c3;
    if (area2 < 1e-24) {
      degenerateCount++;
      continue;
    }

    if (writeIdx != b) {
      src[writeIdx] = (float)ax; src[writeIdx + 1] = (float)ay; src[writeIdx + 2] = (float)az;
      src[writeIdx + 3] = (float)bx; src[writeIdx + 4] = (float)by; src[writeIdx + 5] = (float)bz;
      src[writeIdx + 6] = (float)cx; src[writeIdx + 7] = (float)cy; src[writeIdx + 8] = (float)cz;
    }
    writeIdx += 9;
  }

  int64_t removed = nanCount + degenerateCount;
  if (removed > 0) {
    src.resize(writeIdx);
    geometry.normals.clear(); // stale — recomputed below
  }

  if (writeIdx == 0) {
    error = "All " + std::to_string(triCount) + " triangles in the mesh are invalid (" +
            std::to_string(nanCount) + " NaN, " + std::to_string(degenerateCount) +
            " degenerate). Cannot load file.";
    return false;
  }

  stats.nanCount = nanCount;
  stats.degenerateCount = degenerateCount;
  return true;
}

// three.js BufferGeometry.translate — double math, float32 store.
void translateGeometry(Geometry& g, double tx, double ty, double tz) {
  for (size_t i = 0; i < g.positions.size(); i += 3) {
    g.positions[i] = (float)((double)g.positions[i] + tx);
    g.positions[i + 1] = (float)((double)g.positions[i + 1] + ty);
    g.positions[i + 2] = (float)((double)g.positions[i + 2] + tz);
  }
}

// setupGeometry (verbatim port): clean, centre, compute normals when absent.
bool setupGeometry(Geometry& geometry, CleanStats& stats, Vec3& originOffset,
                   std::string& error) {
  if (!validateAndCleanGeometry(geometry, stats, error)) return false;
  Bounds box = computeBounds(geometry);
  Vec3 centre = box.center;
  translateGeometry(geometry, -centre.x, -centre.y, -centre.z);
  if (geometry.normals.empty()) computeVertexNormals(geometry);
  originOffset = centre;
  return true;
}

LoadResult finishLoad(Geometry&& geometry, std::string parseError = {}) {
  LoadResult r;
  if (!parseError.empty()) {
    r.error = std::move(parseError);
    return r;
  }
  r.geometry = std::move(geometry);
  CleanStats stats;
  if (!setupGeometry(r.geometry, stats, r.originOffset, r.error)) return r;
  r.bounds = computeBounds(r.geometry);
  r.nanCount = stats.nanCount;
  r.degenerateCount = stats.degenerateCount;
  r.ok = true;
  return r;
}

// ── STL (three.js STLLoader parse behavior) ─────────────────────────────────
bool stlIsBinary(const uint8_t* data, size_t size) {
  if (size < 84) return false; // too small for binary header; treat as ASCII
  const int64_t faceSize = (32 / 8 * 3) + ((32 / 8 * 3) * 3) + (16 / 8);
  int64_t nFaces = rdU32(data + 80);
  int64_t expect = 80 + (32 / 8) + nFaces * faceSize;
  if (expect == (int64_t)size) return true;
  // Not a matching binary size: ASCII files begin with "solid"; anything else
  // is assumed binary.
  static const char solid[5] = {'s', 'o', 'l', 'i', 'd'};
  for (int off = 0; off < 5; off++) {
    if (size < (size_t)off + 5) return true;
    if (std::memcmp(data + off, solid, 5) == 0) return false;
  }
  return true;
}

Geometry parseBinarySTL(const uint8_t* data, size_t size, std::string& error) {
  Geometry g;
  if (size < 84) {
    error = "Invalid binary STL: file too small";
    return g;
  }
  uint32_t faces = rdU32(data + 80);
  const size_t dataOffset = 84;
  const size_t faceLength = 12 * 4 + 2;
  if (dataOffset + (size_t)faces * faceLength > size) {
    error = "Invalid binary STL: truncated file";
    return g;
  }
  g.positions.resize((size_t)faces * 9);
  g.normals.resize((size_t)faces * 9);
  for (uint32_t face = 0; face < faces; face++) {
    const uint8_t* start = data + dataOffset + (size_t)face * faceLength;
    float nx = rdF32(start), ny = rdF32(start + 4), nz = rdF32(start + 8);
    for (int i = 1; i <= 3; i++) {
      const uint8_t* vs = start + i * 12;
      size_t o = (size_t)face * 9 + (size_t)(i - 1) * 3;
      g.positions[o] = rdF32(vs);
      g.positions[o + 1] = rdF32(vs + 4);
      g.positions[o + 2] = rdF32(vs + 8);
      g.normals[o] = nx;
      g.normals[o + 1] = ny;
      g.normals[o + 2] = nz;
    }
  }
  return g;
}

// Scanner helpers for ASCII STL — tolerant like the three.js regex parser.
struct TextScan {
  std::string_view s;
  size_t p = 0;

  bool findWord(std::string_view word, size_t limit) {
    size_t found = s.substr(0, limit).find(word, p);
    if (found == std::string_view::npos) return false;
    p = found + word.size();
    return true;
  }

  bool readFloat(double& out, size_t limit) {
    while (p < limit && std::isspace((unsigned char)s[p])) p++;
    if (p >= limit) return false;
    char buf[64];
    size_t n = std::min<size_t>(63, limit - p);
    std::memcpy(buf, s.data() + p, n);
    buf[n] = 0;
    char* end = nullptr;
    double v = std::strtod(buf, &end);
    if (end == buf) return false;
    p += (size_t)(end - buf);
    out = v;
    return true;
  }
};

Geometry parseASCIISTL(std::string_view text, std::string& /*error*/) {
  Geometry g;
  size_t solidPos = 0;
  while (true) {
    size_t solidStart = text.find("solid", solidPos);
    if (solidStart == std::string_view::npos) break;
    size_t solidEnd = text.find("endsolid", solidStart);
    if (solidEnd == std::string_view::npos) break;
    solidPos = solidEnd + 8;

    TextScan scan{text, solidStart};
    while (true) {
      size_t facetStart = text.find("facet", scan.p);
      if (facetStart == std::string_view::npos || facetStart >= solidEnd) break;
      size_t facetEnd = text.find("endfacet", facetStart);
      if (facetEnd == std::string_view::npos || facetEnd > solidEnd) break;

      double nx = 0, ny = 0, nz = 0;
      TextScan f{text, facetStart};
      if (f.findWord("normal", facetEnd)) {
        f.readFloat(nx, facetEnd);
        f.readFloat(ny, facetEnd);
        f.readFloat(nz, facetEnd);
      }
      f.p = facetStart;
      while (f.findWord("vertex", facetEnd)) {
        double x = 0, y = 0, z = 0;
        if (!f.readFloat(x, facetEnd) || !f.readFloat(y, facetEnd) ||
            !f.readFloat(z, facetEnd))
          break;
        g.positions.push_back((float)x);
        g.positions.push_back((float)y);
        g.positions.push_back((float)z);
        g.normals.push_back((float)nx);
        g.normals.push_back((float)ny);
        g.normals.push_back((float)nz);
      }
      scan.p = facetEnd + 8;
    }
  }
  // Drop any trailing partial triangle (tolerance parity with the JS parser,
  // which would have mismatched vertex counts logged but kept the arrays).
  size_t whole = (g.positions.size() / 9) * 9;
  g.positions.resize(whole);
  g.normals.resize(whole);
  return g;
}

// ── OBJ (geometry subset of three.js OBJLoader) ─────────────────────────────
Geometry parseOBJ(std::string_view text, std::string& error) {
  Geometry g;
  std::vector<double> verts;   // v
  std::vector<double> normals; // vn

  size_t lineStart = 0;
  const size_t N = text.size();
  bool anyFace = false;

  auto resolveIndex = [](long idx, size_t count) -> long {
    // OBJ: 1-based; negative = relative to end.
    if (idx > 0) return idx - 1;
    if (idx < 0) return (long)count + idx;
    return -1;
  };

  while (lineStart < N) {
    size_t lineEnd = text.find('\n', lineStart);
    if (lineEnd == std::string_view::npos) lineEnd = N;
    std::string_view line = text.substr(lineStart, lineEnd - lineStart);
    lineStart = lineEnd + 1;

    // trim leading whitespace
    size_t b = 0;
    while (b < line.size() && (line[b] == ' ' || line[b] == '\t' || line[b] == '\r')) b++;
    line = line.substr(b);
    if (line.empty() || line[0] == '#') continue;

    auto parseFloats = [&](std::string_view rest, std::vector<double>& out, int want) {
      char buf[256];
      size_t n = std::min<size_t>(255, rest.size());
      std::memcpy(buf, rest.data(), n);
      buf[n] = 0;
      char* c = buf;
      for (int i = 0; i < want; i++) {
        char* end = nullptr;
        double v = std::strtod(c, &end);
        if (end == c) v = 0;
        out.push_back(v);
        c = end;
      }
    };

    if (line.rfind("v ", 0) == 0) {
      parseFloats(line.substr(2), verts, 3);
    } else if (line.rfind("vn ", 0) == 0) {
      parseFloats(line.substr(3), normals, 3);
    } else if (line.rfind("f ", 0) == 0) {
      // gather face vertex specs
      struct FV { long v = -1, vn = -1; };
      std::vector<FV> fvs;
      size_t p = 1;
      while (p < line.size()) {
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t' || line[p] == '\r')) p++;
        if (p >= line.size()) break;
        size_t e = p;
        while (e < line.size() && line[e] != ' ' && line[e] != '\t' && line[e] != '\r') e++;
        std::string spec(line.substr(p, e - p));
        p = e;
        // v, v/vt, v//vn, v/vt/vn
        FV fv;
        long parts[3] = {0, 0, 0};
        bool has[3] = {false, false, false};
        int pi = 0;
        const char* c = spec.c_str();
        while (*c && pi < 3) {
          if (*c == '/') {
            pi++;
            c++;
            continue;
          }
          char* end = nullptr;
          long v = std::strtol(c, &end, 10);
          if (end != c) {
            parts[pi] = v;
            has[pi] = true;
            c = end;
          } else {
            c++;
          }
        }
        if (has[0]) fv.v = resolveIndex(parts[0], verts.size() / 3);
        if (has[2]) fv.vn = resolveIndex(parts[2], normals.size() / 3);
        if (fv.v >= 0) fvs.push_back(fv);
      }
      // fan triangulation
      for (size_t i = 1; i + 1 < fvs.size(); i++) {
        const FV tri[3] = {fvs[0], fvs[i], fvs[i + 1]};
        bool triHasNormals = tri[0].vn >= 0 && tri[1].vn >= 0 && tri[2].vn >= 0;
        for (const FV& fv : tri) {
          size_t vo = (size_t)fv.v * 3;
          if (vo + 2 >= verts.size()) {
            error = "Invalid vertex index in OBJ file";
            return {};
          }
          g.positions.push_back((float)verts[vo]);
          g.positions.push_back((float)verts[vo + 1]);
          g.positions.push_back((float)verts[vo + 2]);
          if (triHasNormals) {
            size_t no = (size_t)fv.vn * 3;
            if (no + 2 < normals.size()) {
              g.normals.push_back((float)normals[no]);
              g.normals.push_back((float)normals[no + 1]);
              g.normals.push_back((float)normals[no + 2]);
            } else {
              g.normals.push_back(0); g.normals.push_back(0); g.normals.push_back(0);
            }
          }
        }
        anyFace = true;
      }
    }
  }

  if (!anyFace) {
    error = "No mesh data found in file";
    return {};
  }
  // Mixed faces with/without normals leave the normal array inconsistent —
  // only keep normals if complete (mirrors "every geometry has normals" merge
  // condition in the original).
  if (g.normals.size() != g.positions.size()) g.normals.clear();
  return g;
}

// ── 3MF (port of the custom parser in stlLoader.js) ─────────────────────────
struct MeshData {
  std::vector<float> vertices;
  std::vector<uint32_t> triangles;
};

std::string normalizePath(std::string p) {
  if (!p.empty() && p[0] == '/') p.erase(0, 1);
  std::replace(p.begin(), p.end(), '\\', '/');
  return p;
}

struct ZipFiles {
  std::map<std::string, std::vector<uint8_t>> files;

  const std::vector<uint8_t>* get(const std::string& path) const {
    std::string clean = path;
    if (!clean.empty() && clean[0] == '/') clean.erase(0, 1);
    auto it = files.find(clean);
    if (it != files.end()) return &it->second;
    it = files.find("/" + clean);
    if (it != files.end()) return &it->second;
    return nullptr;
  }
};

bool unzipAll(const uint8_t* data, size_t size, ZipFiles& out, std::string& error) {
  mz_zip_archive zip;
  std::memset(&zip, 0, sizeof(zip));
  if (!mz_zip_reader_init_mem(&zip, data, size, 0)) {
    error = "Could not read 3MF archive";
    return false;
  }
  mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; i++) {
    mz_zip_archive_file_stat st;
    if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
    if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
    std::vector<uint8_t> buf(st.m_uncomp_size);
    if (st.m_uncomp_size > 0 &&
        !mz_zip_reader_extract_to_mem(&zip, i, buf.data(), buf.size(), 0))
      continue;
    out.files.emplace(st.m_filename, std::move(buf));
  }
  mz_zip_reader_end(&zip);
  return true;
}

Mat4 parse3MFTransform(const std::string& str) {
  if (str.empty()) return Mat4::identity();
  std::vector<double> v;
  const char* c = str.c_str();
  while (*c) {
    while (*c && std::isspace((unsigned char)*c)) c++;
    if (!*c) break;
    char* end = nullptr;
    double d = std::strtod(c, &end);
    if (end == c) break;
    v.push_back(d);
    c = end;
  }
  if (v.size() == 12) {
    // 3MF row-major 3×4: m00 m01 m02  m10 m11 m12  m20 m21 m22  tx ty tz
    return Mat4::fromRowMajor(v[0], v[3], v[6], v[9],
                              v[1], v[4], v[7], v[10],
                              v[2], v[5], v[8], v[11],
                              0, 0, 0, 1);
  }
  return Mat4::identity();
}

Geometry parse3MF(const uint8_t* data, size_t size, std::string& error) {
  Geometry out;
  ZipFiles files;
  if (!unzipAll(data, size, files, error)) return out;

  // Parse each .model XML once, cache by path.
  std::map<std::string, xml::Document> docs;
  auto readXML = [&](const std::string& path) -> const xml::Element* {
    std::string clean = path;
    if (!clean.empty() && clean[0] == '/') clean.erase(0, 1);
    auto it = docs.find(clean);
    if (it == docs.end()) {
      const std::vector<uint8_t>* bytes = files.get(clean);
      if (!bytes) return nullptr;
      xml::Document doc = xml::parse(
          std::string_view((const char*)bytes->data(), bytes->size()));
      it = docs.emplace(clean, std::move(doc)).first;
    }
    return it->second.root.get();
  };

  // 3MF Core Spec unit values → millimeters.
  auto unitToMm = [](const std::string& u) -> double {
    if (u == "micron") return 0.001;
    if (u == "millimeter") return 1;
    if (u == "centimeter") return 10;
    if (u == "inch") return 25.4;
    if (u == "foot") return 304.8;
    if (u == "meter") return 1000;
    return 1;
  };

  // Collect objects by "path#id"
  std::map<std::string, MeshData> objectMap;
  std::vector<std::string> modelPaths;
  for (const auto& [name, bytes] : files.files) {
    if (name.size() >= 6 && name.compare(name.size() - 6, 6, ".model") == 0)
      modelPaths.push_back(name);
  }

  for (const std::string& path : modelPaths) {
    const xml::Element* doc = readXML(path);
    if (!doc) continue;
    for (const xml::Element* obj : doc->findAll("object")) {
      std::string id = obj->attrLocal("id");
      const xml::Element* meshEl = obj->findFirst("mesh");
      if (!meshEl) continue; // component-only object, no inline mesh

      auto vertEls = meshEl->findAll("vertex");
      auto triEls = meshEl->findAll("triangle");
      MeshData mesh;
      mesh.vertices.resize(vertEls.size() * 3);
      for (size_t i = 0; i < vertEls.size(); i++) {
        mesh.vertices[i * 3] = (float)jsParseFloat(vertEls[i]->attrLocal("x"));
        mesh.vertices[i * 3 + 1] = (float)jsParseFloat(vertEls[i]->attrLocal("y"));
        mesh.vertices[i * 3 + 2] = (float)jsParseFloat(vertEls[i]->attrLocal("z"));
      }
      mesh.triangles.resize(triEls.size() * 3);
      for (size_t i = 0; i < triEls.size(); i++) {
        mesh.triangles[i * 3] = jsToUint32(jsParseInt(triEls[i]->attrLocal("v1")));
        mesh.triangles[i * 3 + 1] = jsToUint32(jsParseInt(triEls[i]->attrLocal("v2")));
        mesh.triangles[i * 3 + 2] = jsToUint32(jsParseInt(triEls[i]->attrLocal("v3")));
      }

      size_t vertCount = vertEls.size();
      for (uint32_t t : mesh.triangles) {
        if (t >= vertCount) {
          error = "Invalid triangle index in 3MF file";
          return out;
        }
      }

      objectMap.emplace(normalizePath(path) + "#" + id, std::move(mesh));
    }
  }

  if (objectMap.empty()) {
    error = "No mesh data found in 3MF file";
    return out;
  }

  // Root model: 3D/3dmodel.model (case-insensitive) else first model file.
  std::string rootPath;
  for (const std::string& p : modelPaths) {
    std::string norm = normalizePath(p);
    std::string lower = norm;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower == "3d/3dmodel.model") {
      rootPath = p;
      break;
    }
  }
  if (rootPath.empty() && !modelPaths.empty()) rootPath = modelPaths[0];
  const xml::Element* rootDoc = readXML(rootPath);
  if (!rootDoc) {
    error = "No mesh data found in 3MF file";
    return out;
  }

  std::string rootUnit = rootDoc->attrLocal("unit");
  if (rootUnit.empty()) rootUnit = "millimeter";
  std::transform(rootUnit.begin(), rootUnit.end(), rootUnit.begin(), ::tolower);
  double unitScale = unitToMm(rootUnit);
  Mat4 unitMatrix = Mat4::makeScale(unitScale, unitScale, unitScale);

  struct Instance {
    std::string meshKey;
    Mat4 matrix;
  };
  std::vector<Instance> instances;

  // Recursive component resolution with cycle/depth guards.
  std::set<std::string> visiting;
  std::string recursionError;

  std::function<void(const std::string&, const std::string&, const Mat4&, int)>
      resolveObject = [&](const std::string& filePath, const std::string& objectId,
                          const Mat4& parentMatrix, int depth) {
        if (!recursionError.empty()) return;
        if (depth > kMax3MFDepth) {
          recursionError = "3MF component hierarchy too deep — possible cyclic reference";
          return;
        }
        std::string normFile = normalizePath(filePath);
        std::string key = normFile + "#" + objectId;
        if (visiting.count(key)) {
          recursionError = "Cyclic component reference detected in 3MF file (" + key + ")";
          return;
        }
        visiting.insert(key);

        if (objectMap.count(key)) instances.push_back({key, parentMatrix});

        const xml::Element* doc = readXML(filePath);
        if (!doc) {
          visiting.erase(key);
          return;
        }
        for (const xml::Element* obj : doc->findAll("object")) {
          if (obj->attrLocal("id") != objectId) continue;
          for (const xml::Element* comp : obj->findAll("component")) {
            std::string compObjId = comp->attrLocal("objectid");
            std::string compPath = comp->attrLocal("path");
            if (compPath.empty()) compPath = filePath;
            if (!compPath.empty() && compPath[0] != '/' &&
                compPath.rfind("3D", 0) != 0)
              compPath = "/" + compPath;
            Mat4 compTransform = parse3MFTransform(comp->attrLocal("transform"));
            Mat4 combined = parentMatrix.multiplied(compTransform);
            resolveObject(compPath, compObjId, combined, depth + 1);
            if (!recursionError.empty()) return;
          }
        }
        visiting.erase(key);
      };

  auto buildItems = rootDoc->findAll("item");
  if (!buildItems.empty()) {
    for (const xml::Element* item : buildItems) {
      std::string objId = item->attrLocal("objectid");
      Mat4 itemTransform = parse3MFTransform(item->attrLocal("transform"));
      Mat4 seedMatrix = unitMatrix.multiplied(itemTransform);
      resolveObject(rootPath, objId, seedMatrix, 0);
      if (!recursionError.empty()) {
        error = recursionError;
        return out;
      }
    }
  } else {
    // No build section — just use all meshes directly with unit scale applied.
    for (const auto& [key, mesh] : objectMap)
      instances.push_back({key, unitMatrix});
  }

  if (instances.empty()) {
    // Fallback: use all parsed meshes with the unit scale applied.
    for (const auto& [key, mesh] : objectMap)
      instances.push_back({key, unitMatrix});
  }

  int64_t totalTris = 0;
  for (const Instance& inst : instances) {
    auto it = objectMap.find(inst.meshKey);
    if (it != objectMap.end()) totalTris += (int64_t)(it->second.triangles.size() / 3);
  }
  if (totalTris > kMax3MFTriangles) {
    error = "3MF file contains " + std::to_string(totalTris) +
            " triangles, exceeding the " + std::to_string(kMax3MFTriangles) + " limit";
    return out;
  }

  out.positions.resize((size_t)totalTris * 9);
  size_t writeOffset = 0;
  for (const Instance& inst : instances) {
    auto it = objectMap.find(inst.meshKey);
    if (it == objectMap.end()) continue;
    const MeshData& mesh = it->second;
    for (size_t t = 0; t + 2 < mesh.triangles.size(); t += 3) {
      for (int v = 0; v < 3; v++) {
        uint32_t vi = mesh.triangles[t + v];
        Vec3 tmp = {(double)mesh.vertices[vi * 3], (double)mesh.vertices[vi * 3 + 1],
                    (double)mesh.vertices[vi * 3 + 2]};
        tmp = inst.matrix.apply(tmp);
        out.positions[writeOffset++] = (float)tmp.x;
        out.positions[writeOffset++] = (float)tmp.y;
        out.positions[writeOffset++] = (float)tmp.z;
      }
    }
  }
  return out;
}

} // namespace

// ── public API ───────────────────────────────────────────────────────────────

void computeVertexNormals(Geometry& g) {
  g.normals.assign(g.positions.size(), 0.0f);
  const auto& p = g.positions;
  for (size_t i = 0; i + 8 < p.size(); i += 9) {
    double pAx = p[i], pAy = p[i + 1], pAz = p[i + 2];
    double pBx = p[i + 3], pBy = p[i + 4], pBz = p[i + 5];
    double pCx = p[i + 6], pCy = p[i + 7], pCz = p[i + 8];
    // cb = pC - pB; ab = pA - pB; cb x ab  (three.js order)
    double cbx = pCx - pBx, cby = pCy - pBy, cbz = pCz - pBz;
    double abx = pAx - pBx, aby = pAy - pBy, abz = pAz - pBz;
    double nx = cby * abz - cbz * aby;
    double ny = cbz * abx - cbx * abz;
    double nz = cbx * aby - cby * abx;
    for (int v = 0; v < 3; v++) {
      g.normals[i + v * 3] = (float)nx;
      g.normals[i + v * 3 + 1] = (float)ny;
      g.normals[i + v * 3 + 2] = (float)nz;
    }
  }
  // normalizeNormals: per-vertex normalization (read f32, math f64, store f32)
  for (size_t i = 0; i + 2 < g.normals.size(); i += 3) {
    double x = g.normals[i], y = g.normals[i + 1], z = g.normals[i + 2];
    double len = std::sqrt(x * x + y * y + z * z);
    if (len > 0) {
      g.normals[i] = (float)(x / len);
      g.normals[i + 1] = (float)(y / len);
      g.normals[i + 2] = (float)(z / len);
    }
  }
}

size_t getTriangleCount(const Geometry& g) { return g.positions.size() / 9; }

double computeSurfaceArea(const Geometry& g) {
  const auto& pos = g.positions;
  double area = 0;
  size_t triCount = pos.size() / 9;
  for (size_t t = 0; t < triCount; t++) {
    size_t o = t * 9;
    double ax = pos[o], ay = pos[o + 1], az = pos[o + 2];
    double bx = pos[o + 3], by = pos[o + 4], bz = pos[o + 5];
    double cx = pos[o + 6], cy = pos[o + 7], cz = pos[o + 8];
    double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    double crx = e1y * e2z - e1z * e2y;
    double cry = e1z * e2x - e1x * e2z;
    double crz = e1x * e2y - e1y * e2x;
    area += 0.5 * std::sqrt(crx * crx + cry * cry + crz * crz);
  }
  return area;
}

LoadResult loadSTL(const uint8_t* data, size_t size) {
  std::string error;
  Geometry g;
  if (stlIsBinary(data, size)) {
    g = parseBinarySTL(data, size, error);
  } else {
    g = parseASCIISTL(std::string_view((const char*)data, size), error);
  }
  return finishLoad(std::move(g), error);
}

LoadResult loadOBJ(const uint8_t* data, size_t size) {
  std::string error;
  Geometry g = parseOBJ(std::string_view((const char*)data, size), error);
  return finishLoad(std::move(g), error);
}

LoadResult load3MF(const uint8_t* data, size_t size) {
  std::string error;
  Geometry g = parse3MF(data, size, error);
  return finishLoad(std::move(g), error);
}

LoadResult loadModelBytes(const std::string& filename, const uint8_t* data, size_t size) {
  if ((int64_t)size > kMaxFileSize) {
    LoadResult r;
    r.error = "File too large (" + std::to_string(size / 1024 / 1024) +
              " MB). Maximum supported: " + std::to_string(kMaxFileSize / 1024 / 1024) + " MB.";
    return r;
  }
  std::string ext;
  auto dot = filename.rfind('.');
  if (dot != std::string::npos) ext = filename.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  if (ext == "obj") return loadOBJ(data, size);
  if (ext == "3mf") return load3MF(data, size);
  return loadSTL(data, size);
}

LoadResult loadModelFile(const std::string& path) {
  LoadResult r;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    r.error = "Could not read file";
    return r;
  }
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data((size_t)size);
  size_t rd = size > 0 ? std::fread(data.data(), 1, (size_t)size, f) : 0;
  std::fclose(f);
  if (rd != (size_t)size) {
    r.error = "Could not read file";
    return r;
  }
  std::string name = path;
  auto slash = name.find_last_of("/\\");
  if (slash != std::string::npos) name = name.substr(slash + 1);
  return loadModelBytes(name, data.data(), data.size());
}

} // namespace core
