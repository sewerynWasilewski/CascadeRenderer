#define TINYOBJLOADER_IMPLEMENTATION
#include "tinyobjloader/tiny_obj_loader.h"

#include "model_loader.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace {

struct VertexKey {
  int v;
  int n;
  int t;

  bool operator==(const VertexKey& other) const {
    return v == other.v && n == other.n && t == other.t;
  }
};

struct VertexKeyHash {
  std::size_t operator()(const VertexKey& k) const {
    std::size_t h = static_cast<std::size_t>(static_cast<unsigned>(k.v)) * 73856093u;
    h ^= static_cast<std::size_t>(static_cast<unsigned>(k.n)) * 19349663u;
    h ^= static_cast<std::size_t>(static_cast<unsigned>(k.t)) * 83492791u;
    return h;
  }
};

glm::vec3 readVec3(const std::vector<tinyobj::real_t>& data, std::size_t index) {
  const std::size_t base = index * 3;
  return glm::vec3(data[base], data[base + 1], data[base + 2]);
}

glm::vec2 readVec2(const std::vector<tinyobj::real_t>& data, std::size_t index) {
  const std::size_t base = index * 2;
  return glm::vec2(data[base], data[base + 1]);
}

template <typename Fn>
void forEachTriangle(const tinyobj::mesh_t& mesh, std::size_t faceOffset,
                     unsigned int faceVerts, Fn&& emit) {
  for (unsigned int i = 2; i < faceVerts; ++i) {
    emit(mesh.indices[faceOffset],
         mesh.indices[faceOffset + i - 1],
         mesh.indices[faceOffset + i]);
  }
}

ModelLoadResult buildFromParsed(const tinyobj::attrib_t& attrib,
                                const std::vector<tinyobj::shape_t>& shapes,
                                const std::string& sourceName,
                                Scene& scene) {
  ModelLoadResult result;

  const std::size_t vertexCount   = attrib.vertices.size() / 3;
  const std::size_t normalCount   = attrib.normals.size() / 3;
  const std::size_t texcoordCount = attrib.texcoords.size() / 2;

  if (vertexCount == 0) {
    result.error = sourceName + ": no vertices";
    return result;
  }

  std::size_t triangleCount = 0;
  for (const tinyobj::shape_t& shape : shapes) {
    if (shape.mesh.indices.empty()) {
      continue;
    }
    for (const tinyobj::index_t& idx : shape.mesh.indices) {
      if (idx.vertex_index < 0 || static_cast<std::size_t>(idx.vertex_index) >= vertexCount) {
        result.error = sourceName + ": vertex index out of range";
        return result;
      }
      if (idx.normal_index >= 0 && static_cast<std::size_t>(idx.normal_index) >= normalCount) {
        result.error = sourceName + ": normal index out of range";
        return result;
      }
      if (idx.texcoord_index >= 0 && static_cast<std::size_t>(idx.texcoord_index) >= texcoordCount) {
        result.error = sourceName + ": texcoord index out of range";
        return result;
      }
    }
    for (unsigned int faceVerts : shape.mesh.num_face_vertices) {
      if (faceVerts >= 3) triangleCount += faceVerts - 2;
    }
  }

  if (triangleCount == 0) {
    result.error = sourceName + ": no faces";
    return result;
  }

  std::vector<glm::vec3> generatedNormals;
  if (normalCount == 0) {
    generatedNormals.assign(vertexCount, glm::vec3(0.0f));

    for (const tinyobj::shape_t& shape : shapes) {
      const tinyobj::mesh_t& mesh = shape.mesh;
      if (mesh.indices.empty()) {
        continue;
      }
      std::size_t offset = 0;
      for (unsigned int faceVerts : mesh.num_face_vertices) {
        forEachTriangle(mesh, offset, faceVerts,
                        [&](const tinyobj::index_t& a, const tinyobj::index_t& b,
                            const tinyobj::index_t& c) {
          const glm::vec3 p0 = readVec3(attrib.vertices, static_cast<std::size_t>(a.vertex_index));
          const glm::vec3 p1 = readVec3(attrib.vertices, static_cast<std::size_t>(b.vertex_index));
          const glm::vec3 p2 = readVec3(attrib.vertices, static_cast<std::size_t>(c.vertex_index));

          const glm::vec3 weighted = glm::cross(p1 - p0, p2 - p0);
          generatedNormals[static_cast<std::size_t>(a.vertex_index)] += weighted;
          generatedNormals[static_cast<std::size_t>(b.vertex_index)] += weighted;
          generatedNormals[static_cast<std::size_t>(c.vertex_index)] += weighted;
        });
        offset += faceVerts;
      }
    }

    for (glm::vec3& n : generatedNormals) {
      const float len = glm::length(n);
      n = (len > 0.0f) ? n / len : glm::vec3(0.0f, 0.0f, 1.0f);
    }
  }

  std::vector<Vertex> vertices;
  std::vector<u32>    indices;
  std::vector<Mesh>   meshes;

  const std::size_t vertexBase = scene.vertices.size();
  const std::size_t indexBase  = scene.indices.size();

  std::unordered_map<VertexKey, u32, VertexKeyHash> unique;
  unique.reserve(vertexCount);

  for (const tinyobj::shape_t& shape : shapes) {
    const tinyobj::mesh_t& mesh = shape.mesh;
    if (mesh.indices.empty()) {
      continue;
    }

    const std::size_t localFirst = indices.size();

    std::size_t offset = 0;
    for (unsigned int faceVerts : mesh.num_face_vertices) {
      forEachTriangle(mesh, offset, faceVerts,
                      [&](const tinyobj::index_t& a, const tinyobj::index_t& b,
                          const tinyobj::index_t& c) {
        for (const tinyobj::index_t* corner : {&a, &b, &c}) {
          const VertexKey key{corner->vertex_index, corner->normal_index, corner->texcoord_index};

          auto it = unique.find(key);
          if (it == unique.end()) {
            const std::size_t v = static_cast<std::size_t>(corner->vertex_index);

            Vertex vertex;
            vertex.position = readVec3(attrib.vertices, v);

            if (corner->normal_index >= 0) {
              vertex.normal = readVec3(attrib.normals, static_cast<std::size_t>(corner->normal_index));
            } else if (!generatedNormals.empty()) {
              vertex.normal = generatedNormals[v];
            } else {
              vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            vertex.uv = (corner->texcoord_index >= 0)
                          ? readVec2(attrib.texcoords, static_cast<std::size_t>(corner->texcoord_index))
                          : glm::vec2(0.0f);

            it = unique.emplace(key, static_cast<u32>(vertices.size())).first;
            vertices.push_back(vertex);
          }

          indices.push_back(static_cast<u32>(vertexBase + it->second));
        }
      });
      offset += faceVerts;
    }

    Mesh out;
    out.name       = shape.name;
    out.sourcePath = sourceName;
    out.transform  = glm::mat4(1.0f);
    out.firstIndex = static_cast<u32>(indexBase + localFirst);
    out.indexCount = static_cast<u32>(indices.size() - localFirst);
    meshes.push_back(out);
  }

  if (vertexBase + vertices.size() > UINT32_MAX || indexBase + indices.size() > UINT32_MAX) {
    result.error = sourceName + ": geometry exceeds 32-bit index range";
    return result;
  }

  scene.vertices.insert(scene.vertices.end(), vertices.begin(), vertices.end());
  scene.indices.insert(scene.indices.end(), indices.begin(), indices.end());
  scene.meshes.insert(scene.meshes.end(), meshes.begin(), meshes.end());

  result.ok          = true;
  result.meshCount   = static_cast<u32>(meshes.size());
  result.vertexCount = static_cast<u32>(vertices.size());
  result.indexCount  = static_cast<u32>(indices.size());
  return result;
}

tinyobj::ObjReaderConfig makeConfig() {
  tinyobj::ObjReaderConfig config;
  config.triangulate = true;
  config.vertex_color = false;
  return config;
}

} // namespace

ModelLoadResult loadOBJFromMemory(const std::string& objText,
                                 const std::string& sourceName,
                                 Scene& scene) {
  tinyobj::ObjReader reader;
  if (!reader.ParseFromString(objText, std::string(), makeConfig())) {
    ModelLoadResult result;
    result.error = reader.Error().empty() ? (sourceName + ": could not parse OBJ") : reader.Error();
    return result;
  }

  return buildFromParsed(reader.GetAttrib(), reader.GetShapes(), sourceName, scene);
}

ModelLoadResult loadOBJ(const std::string& path, Scene& scene) {
  tinyobj::ObjReader reader;
  if (!reader.ParseFromFile(path, makeConfig())) {
    ModelLoadResult result;
    result.error = reader.Error().empty() ? (path + ": could not load OBJ") : reader.Error();
    return result;
  }

  return buildFromParsed(reader.GetAttrib(), reader.GetShapes(), path, scene);
}
