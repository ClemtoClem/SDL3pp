#ifndef SDL3PP_ENGINE_OBJLOADER_H_
#define SDL3PP_ENGINE_OBJLOADER_H_

/**
 * @file ObjLoader.h
 * @brief Wavefront `.obj` (+ minimal `.mtl`) loader producing engine `Mesh`es.
 *
 * Supported `.obj` features:
 *   - `v`  positions, `vt` texture coords, `vn` normals
 *   - `f`  faces with `v`, `v/vt`, `v//vn`, `v/vt/vn` corners (1-based or
 *     negative/relative indices), polygons are triangulated as a fan
 *   - `o` / `g` object & group names (start a new `SubMesh`)
 *   - `usemtl` material assignment, `mtllib` material library reference
 *
 * Supported `.mtl` features: `newmtl`, `Kd`/`Ka`/`Ks`, `Ns`, `d`/`Tr`,
 * `map_Kd`. Anything else is ignored.
 *
 * Missing normals are generated (smooth) when requested. The result is a
 * `Mesh` ready for `GpuMesh::Upload()` plus the parsed material table.
 */

#include <cstdlib>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Mesh.h"

namespace SDL {

/**
 * @addtogroup CategoryEngineMesh
 * @{
 */

/// A material parsed from a `.mtl` library (Phong-style parameters).
struct ObjMaterial {
	std::string name;
	FVector4    diffuse{0.8f, 0.8f, 0.8f, 1.f};   ///< Kd
	FVector4    ambient{0.2f, 0.2f, 0.2f, 1.f};   ///< Ka
	FVector4    specular{0.f, 0.f, 0.f, 1.f};     ///< Ks
	float       shininess = 32.f;                 ///< Ns
	std::string diffuseMap;                        ///< map_Kd (texture path)
};

/// Result of loading an `.obj`: one mesh plus its material table.
struct ObjModel {
	Mesh                     mesh;
	std::vector<ObjMaterial> materials;

	[[nodiscard]] bool IsValid() const noexcept { return !mesh.Empty(); }
};

namespace detail {

/// Hash key for a unique (position, uv, normal) index triple.
struct ObjIndex {
	int v = 0, t = 0, n = 0;
	bool operator==(const ObjIndex& o) const noexcept { return v == o.v && t == o.t && n == o.n; }
};

struct ObjIndexHash {
	size_t operator()(const ObjIndex& k) const noexcept {
		size_t h = static_cast<size_t>(k.v) * 73856093u;
		h ^= static_cast<size_t>(k.t) * 19349663u;
		h ^= static_cast<size_t>(k.n) * 83492791u;
		return h;
	}
};

/// Resolve a possibly-negative/1-based OBJ index against the current count.
[[nodiscard]] inline int ResolveObjIndex(int raw, size_t count) noexcept {
	if (raw > 0)  return raw - 1;            // 1-based
	if (raw < 0)  return static_cast<int>(count) + raw; // relative
	return -1;                                // 0 = unspecified
}

/// Parse "v/vt/vn" (any field may be empty) into an ObjIndex.
[[nodiscard]] inline ObjIndex ParseFaceCorner(std::string_view tok) noexcept {
	ObjIndex idx;
	int field = 0;
	size_t start = 0;
	for (size_t i = 0; i <= tok.size(); ++i) {
		if (i == tok.size() || tok[i] == '/') {
			if (i > start) {
				int val = std::atoi(std::string(tok.substr(start, i - start)).c_str());
				if      (field == 0) idx.v = val;
				else if (field == 1) idx.t = val;
				else if (field == 2) idx.n = val;
			}
			++field;
			start = i + 1;
		}
	}
	return idx;
}

/// Parse a minimal `.mtl` library, appending materials to `out`.
inline void ParseMtl(std::string_view text, std::vector<ObjMaterial>& out) {
	std::istringstream in{std::string(text)};
	std::string line;
	ObjMaterial* cur = nullptr;
	while (std::getline(in, line)) {
		std::istringstream ls{line};
		std::string tag;
		ls >> tag;
		if (tag == "newmtl") {
			std::string name; ls >> name;
			out.push_back(ObjMaterial{}); cur = &out.back(); cur->name = name;
		} else if (!cur) {
			continue;
		} else if (tag == "Kd") { ls >> cur->diffuse.x  >> cur->diffuse.y  >> cur->diffuse.z;
		} else if (tag == "Ka") { ls >> cur->ambient.x  >> cur->ambient.y  >> cur->ambient.z;
		} else if (tag == "Ks") { ls >> cur->specular.x >> cur->specular.y >> cur->specular.z;
		} else if (tag == "Ns") { ls >> cur->shininess;
		} else if (tag == "d")  { ls >> cur->diffuse.w;
		} else if (tag == "Tr") { float tr; ls >> tr; cur->diffuse.w = 1.f - tr;
		} else if (tag == "map_Kd") { ls >> cur->diffuseMap; }
	}
}

} // namespace detail

/**
 * Parse an `.obj` document already held in memory.
 *
 * @param objText    the full contents of the `.obj` file.
 * @param mtlLoader  optional callback that returns the contents of a referenced
 *                   `.mtl` file given the name from a `mtllib` directive. When
 *                   null, materials are still tracked by name from `usemtl`.
 * @param generateNormalsIfMissing recompute smooth normals when the file has none.
 */
[[nodiscard]] inline ObjModel ParseOBJ(
		std::string_view objText,
		const std::function<std::string(const std::string&)>& mtlLoader = {},
		bool generateNormalsIfMissing = true) {
	using detail::ObjIndex;
	ObjModel model;
	Mesh& mesh = model.mesh;

	std::vector<FVector3> positions;
	std::vector<FVector2> uvs;
	std::vector<FVector3> normals;
	std::unordered_map<ObjIndex, Uint32, detail::ObjIndexHash> vertexCache;
	bool anyNormals = false;

	int currentMaterial = -1;
	SubMesh current;          // the submesh being filled
	bool    haveSubmesh = false;

	auto flushSubmesh = [&] {
		if (haveSubmesh && current.indexCount > 0) mesh.submeshes.push_back(current);
		haveSubmesh = false;
	};
	auto startSubmesh = [&](const std::string& name) {
		flushSubmesh();
		current = SubMesh{};
		current.indexOffset   = mesh.IndexCount();
		current.materialIndex = currentMaterial;
		current.name          = name;
		haveSubmesh = true;
	};
	auto findMaterial = [&](const std::string& name) -> int {
		for (size_t i = 0; i < model.materials.size(); ++i)
			if (model.materials[i].name == name) return static_cast<int>(i);
		model.materials.push_back(ObjMaterial{}); // forward reference placeholder
		model.materials.back().name = name;
		return static_cast<int>(model.materials.size()) - 1;
	};

	auto emitCorner = [&](const ObjIndex& raw) -> Uint32 {
		ObjIndex key{
			detail::ResolveObjIndex(raw.v, positions.size()),
			detail::ResolveObjIndex(raw.t, uvs.size()),
			detail::ResolveObjIndex(raw.n, normals.size())};
		if (auto it = vertexCache.find(key); it != vertexCache.end()) return it->second;
		MeshVertex vtx;
		if (key.v >= 0 && key.v < static_cast<int>(positions.size())) vtx.position = positions[key.v];
		if (key.t >= 0 && key.t < static_cast<int>(uvs.size()))       vtx.uv       = uvs[key.t];
		if (key.n >= 0 && key.n < static_cast<int>(normals.size())) { vtx.normal = normals[key.n]; anyNormals = true; }
		Uint32 index = mesh.AddVertex(vtx);
		vertexCache.emplace(key, index);
		return index;
	};

	std::istringstream in{std::string(objText)};
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#') continue;
		std::istringstream ls{line};
		std::string tag;
		ls >> tag;

		if (tag == "v") {
			FVector3 p; ls >> p.x >> p.y >> p.z; positions.push_back(p);
		} else if (tag == "vt") {
			FVector2 t; ls >> t.x >> t.y; uvs.push_back(t);
		} else if (tag == "vn") {
			FVector3 n; ls >> n.x >> n.y >> n.z; normals.push_back(n);
		} else if (tag == "f") {
			std::vector<Uint32> corners;
			std::string corner;
			while (ls >> corner) corners.push_back(emitCorner(detail::ParseFaceCorner(corner)));
			if (!haveSubmesh) startSubmesh("default");
			// Fan triangulation.
			for (size_t i = 1; i + 1 < corners.size(); ++i) {
				mesh.AddTriangle(corners[0], corners[i], corners[i + 1]);
				current.indexCount += 3;
			}
		} else if (tag == "o" || tag == "g") {
			std::string name; std::getline(ls, name);
			if (!name.empty() && name[0] == ' ') name.erase(0, 1);
			startSubmesh(name);
		} else if (tag == "usemtl") {
			std::string name; ls >> name;
			currentMaterial = findMaterial(name);
			startSubmesh(name);
		} else if (tag == "mtllib") {
			std::string name; ls >> name;
			if (mtlLoader) {
				std::string mtl = mtlLoader(name);
				if (!mtl.empty()) detail::ParseMtl(mtl, model.materials);
			}
		}
	}
	flushSubmesh();

	mesh.RecomputeBounds();
	if (!anyNormals && generateNormalsIfMissing) mesh.RecomputeNormals();
	return model;
}

/**
 * Load an `.obj` file from disk (resolving a sibling `.mtl` automatically).
 *
 * @param path the path to the `.obj` file.
 * @param generateNormalsIfMissing recompute smooth normals when the file has none.
 * @returns the parsed model; `IsValid()` is false on read/parse failure.
 */
[[nodiscard]] inline ObjModel LoadOBJ(const std::string& path,
																			bool generateNormalsIfMissing = true) {
	std::ifstream file(path, std::ios::binary);
	if (!file) return {};
	std::ostringstream ss;
	ss << file.rdbuf();
	const std::string text = ss.str();

	// Resolve .mtl relative to the .obj directory.
	const size_t slash = path.find_last_of("/\\");
	const std::string dir = (slash == std::string::npos) ? std::string{} : path.substr(0, slash + 1);
	auto mtlLoader = [&dir](const std::string& name) -> std::string {
		std::ifstream mf(dir + name, std::ios::binary);
		if (!mf) return {};
		std::ostringstream mss; mss << mf.rdbuf();
		return mss.str();
	};
	return ParseOBJ(text, mtlLoader, generateNormalsIfMissing);
}

/** @} */ // CategoryEngineMesh

} // namespace SDL

#endif // SDL3PP_ENGINE_OBJLOADER_H_
