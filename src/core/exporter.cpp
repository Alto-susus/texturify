#include "core/exporter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include <miniz.h>

#include "core/mesh_index.h"

namespace core {

namespace {

void wrF32(std::vector<uint8_t>& buf, size_t off, float v) {
  std::memcpy(buf.data() + off, &v, 4);
}
void wrU32(std::vector<uint8_t>& buf, size_t off, uint32_t v) {
  buf[off] = (uint8_t)(v & 0xFF);
  buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
  buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
  buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

} // namespace

std::string fmt3MFCoord(double n) {
  // JS toFixed(4): decimal ties pick the larger integer (round toward +inf on
  // the scaled value). Detect via a 30-decimal correctly-rounded expansion.
  bool neg = std::signbit(n) && n != 0.0;
  double a = std::fabs(n);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.30f", a);
  // buf = "iii.dddddddddd..." — find decimal point
  char* dot = std::strchr(buf, '.');
  std::string intPart(buf, dot - buf);
  std::string frac(dot + 1);
  std::string keep = frac.substr(0, 4);
  std::string rest = frac.substr(4);
  // Compare rest against "5000...": >= "5" followed by anything nonzero → up;
  // exactly 5 then zeros → tie → up (larger n). < → down.
  bool roundUp = false;
  if (!rest.empty()) {
    if (rest[0] > '5') {
      roundUp = true;
    } else if (rest[0] == '5') {
      roundUp = true; // tie or above-tie both round up (JS picks larger n)
    }
  }
  if (roundUp) {
    // increment decimal string keep, with carry into intPart
    int i = 3;
    while (i >= 0) {
      if (keep[i] != '9') {
        keep[i]++;
        break;
      }
      keep[i] = '0';
      i--;
    }
    if (i < 0) {
      // carry into integer part
      int j = (int)intPart.size() - 1;
      while (j >= 0) {
        if (intPart[j] != '9') {
          intPart[j]++;
          break;
        }
        intPart[j] = '0';
        j--;
      }
      if (j < 0) intPart.insert(intPart.begin(), '1');
    }
  }
  std::string s = intPart + "." + keep;
  // strip trailing zeros and "."
  size_t last = s.find_last_not_of('0');
  if (last != std::string::npos && s[last] == '.') last--;
  s.erase(last + 1);
  if (neg && s != "0") s.insert(s.begin(), '-');
  return s;
}

std::vector<uint8_t> exportSTL(const Geometry& geometry) {
  const auto& posArr = geometry.positions;
  const float* norArr = geometry.normals.empty() ? nullptr : geometry.normals.data();
  size_t triCount = posArr.size() / 9;

  // Binary STL: 80-byte header + 4-byte tri count + 50 bytes per triangle
  std::vector<uint8_t> buf(84 + 50 * triCount, 0);
  wrU32(buf, 80, (uint32_t)triCount);

  for (size_t i = 0; i < triCount; i++) {
    size_t dst = 84 + i * 50;
    size_t b = i * 9;

    if (norArr) {
      // Normal: copy first vertex normal — flat shading, all 3 identical
      wrF32(buf, dst, norArr[b]);
      wrF32(buf, dst + 4, norArr[b + 1]);
      wrF32(buf, dst + 8, norArr[b + 2]);
    } else {
      // Compute face normal from cross product
      double ux = (double)posArr[b + 3] - posArr[b];
      double uy = (double)posArr[b + 4] - posArr[b + 1];
      double uz = (double)posArr[b + 5] - posArr[b + 2];
      double vx = (double)posArr[b + 6] - posArr[b];
      double vy = (double)posArr[b + 7] - posArr[b + 1];
      double vz = (double)posArr[b + 8] - posArr[b + 2];
      double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
      double len = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (len == 0) len = 1;
      wrF32(buf, dst, (float)(nx / len));
      wrF32(buf, dst + 4, (float)(ny / len));
      wrF32(buf, dst + 8, (float)(nz / len));
    }

    // Vertices: 36 bytes (3 vertices * 3 floats * 4 bytes)
    std::memcpy(buf.data() + dst + 12, posArr.data() + b, 36);
    // Attribute byte count: 0 (already zero-filled)
  }
  return buf;
}

std::vector<uint8_t> export3MF(const Geometry& geometry) {
  const auto& posArr = geometry.positions;
  size_t triCount = posArr.size() / 9;

  // ── Deduplicate vertices on the 1e4 grid ─────────────────────────────────
  QuantizedPointMap indexMap(1e4, std::min(triCount * 3, (size_t)1 << 22));
  std::vector<float> uniqueXYZ;
  std::vector<uint32_t> triIdx(triCount * 3);

  for (size_t i = 0; i < triCount; i++) {
    for (int j = 0; j < 3; j++) {
      size_t b = i * 9 + (size_t)j * 3;
      float x = posArr[b], y = posArr[b + 1], z = posArr[b + 2];
      int32_t idx = indexMap.getOrSet(x, y, z, (int32_t)(uniqueXYZ.size() / 3));
      if (indexMap.inserted) {
        uniqueXYZ.push_back(x);
        uniqueXYZ.push_back(y);
        uniqueXYZ.push_back(z);
      }
      triIdx[i * 3 + j] = (uint32_t)idx;
    }
  }

  size_t vertCount = uniqueXYZ.size() / 3;

  // ── Build 3dmodel.model XML ──────────────────────────────────────────────
  std::string model;
  model.reserve(vertCount * 44 + triCount * 40 + 512);
  model +=
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<model unit=\"millimeter\" xml:lang=\"en-US\" "
      "xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
      "<resources>\n"
      "<object id=\"1\" type=\"model\">\n"
      "<mesh>\n"
      "<vertices>\n";

  for (size_t i = 0; i < vertCount; i++) {
    size_t b = i * 3;
    model += "<vertex x=\"";
    model += fmt3MFCoord(uniqueXYZ[b]);
    model += "\" y=\"";
    model += fmt3MFCoord(uniqueXYZ[b + 1]);
    model += "\" z=\"";
    model += fmt3MFCoord(uniqueXYZ[b + 2]);
    model += "\"/>\n";
  }

  model += "</vertices>\n<triangles>\n";

  char tribuf[96];
  for (size_t i = 0; i < triCount; i++) {
    size_t b = i * 3;
    std::snprintf(tribuf, sizeof(tribuf),
                  "<triangle v1=\"%u\" v2=\"%u\" v3=\"%u\"/>\n", triIdx[b],
                  triIdx[b + 1], triIdx[b + 2]);
    model += tribuf;
  }

  model +=
      "</triangles>\n"
      "</mesh>\n"
      "</object>\n"
      "</resources>\n"
      "<build>\n<item objectid=\"1\"/>\n</build>\n"
      "</model>\n";

  // ── Static package files ─────────────────────────────────────────────────
  const char* contentTypesXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
      "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
      "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/>\n"
      "</Types>\n";

  const char* relsXml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
      "<Relationship Id=\"rel-1\" Target=\"/3D/3dmodel.model\" "
      "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/>\n"
      "</Relationships>\n";

  // ── Zip (deflate level 6, same file order as the original) ───────────────
  mz_zip_archive zip;
  std::memset(&zip, 0, sizeof(zip));
  mz_zip_writer_init_heap(&zip, 0, model.size() / 2 + 4096);
  mz_zip_writer_add_mem(&zip, "[Content_Types].xml", contentTypesXml,
                        std::strlen(contentTypesXml), 6);
  mz_zip_writer_add_mem(&zip, "_rels/.rels", relsXml, std::strlen(relsXml), 6);
  mz_zip_writer_add_mem(&zip, "3D/3dmodel.model", model.data(), model.size(), 6);

  void* pBuf = nullptr;
  size_t outSize = 0;
  mz_zip_writer_finalize_heap_archive(&zip, &pBuf, &outSize);
  std::vector<uint8_t> out((uint8_t*)pBuf, (uint8_t*)pBuf + outSize);
  mz_zip_writer_end(&zip);
  return out;
}

} // namespace core
