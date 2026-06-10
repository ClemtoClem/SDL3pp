#ifndef SDL3PP_ENGINE_MESH_H_
#define SDL3PP_ENGINE_MESH_H_

/**
 * @file Mesh.h
 * @brief CPU geometry (`Mesh`) and GPU mesh resource (`GpuMesh`) for the
 *        SDL3pp engine.
 *
 * ## Overview
 *
 * - `MeshVertex`   — the standard interleaved vertex layout (position, normal, uv,
 *   colour) shared by every built-in 3D shader.
 * - `Mesh`     — CPU-side geometry: a vertex array, a 32-bit index array, an
 *   optional list of `SubMesh` ranges and a cached bounding box. Comes with
 *   helpers to recompute normals/bounds and with primitive factories (cube,
 *   quad, plane, grid, UV sphere).
 * - `GpuMesh`  — an RAII resource that owns a vertex buffer and an index buffer
 *   on a `GPUDevice`. `Upload()` streams a `Mesh` to VRAM through a transfer
 *   buffer + copy pass; `BindAndDraw()` records the draw on a render pass.
 *
 * The vertex layout matches what you must declare in your graphics pipeline's
 * `GPUVertexInputState` (see `GetVertexAttributes()`), so a mesh uploaded here
 * can be drawn by any pipeline using the same layout.
 */

#include <array>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "../SDL3pp_gpu.h"
#include "../SDL3pp_stdinc.h"
#include "Math3D.h"

namespace SDL {

/**
 * @defgroup CategoryEngineMesh Engine — Meshes
 *
 * Geometry containers and GPU mesh resources.
 *
 * @{
 */

// ─────────────────────────────────────────────────────────────────────────────
// MeshVertex
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Standard interleaved vertex used by all built-in 3D rendering.
 *
 * Layout (matching `GetVertexAttributes()`):
 *   - location 0: `position` — FLOAT3
 *   - location 1: `normal`   — FLOAT3
 *   - location 2: `uv`       — FLOAT2
 *   - location 3: `color`    — FLOAT4
 */
struct MeshVertex {
	FVector3 position;
	FVector3 normal{0.f, 0.f, 1.f};
	FVector2 uv;
	FVector4 color{1.f, 1.f, 1.f, 1.f};

	constexpr MeshVertex() noexcept = default;
	constexpr MeshVertex(const FVector3& p,
									 const FVector3& n  = {0.f, 0.f, 1.f},
									 const FVector2& t  = {},
									 const FVector4& c  = {1.f, 1.f, 1.f, 1.f}) noexcept
		: position(p), normal(n), uv(t), color(c) {}
};

/**
 * Describe the four vertex attributes of `MeshVertex` for a given binding slot.
 *
 * Use this when filling the `GPUVertexInputState` of a graphics pipeline so the
 * pipeline reads `GpuMesh` data correctly.
 *
 * @param bufferSlot the vertex buffer binding slot the attributes read from.
 */
[[nodiscard]] inline std::array<GPUVertexAttribute, 4>
GetVertexAttributes(Uint32 bufferSlot = 0) noexcept {
	return {{
		{0, bufferSlot, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, position)},
		{1, bufferSlot, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(MeshVertex, normal)},
		{2, bufferSlot, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(MeshVertex, uv)},
		{3, bufferSlot, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(MeshVertex, color)},
	}};
}

/// One contiguous index range, optionally bound to a material slot.
struct SubMesh {
	Uint32      indexOffset   = 0;   ///< First index (into `Mesh::indices`).
	Uint32      indexCount    = 0;   ///< Number of indices in this range.
	int         materialIndex = -1;  ///< Index into the owner's material table, or -1.
	std::string name;
};

// ─────────────────────────────────────────────────────────────────────────────
// Mesh (CPU geometry)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * CPU-side triangle mesh: interleaved vertices + 32-bit indices.
 *
 * Triangles are defined by consecutive index triplets. When `submeshes` is
 * empty the whole index range is treated as one draw.
 */
class Mesh {
public:
	std::vector<MeshVertex>  vertices;
	std::vector<Uint32>  indices;
	std::vector<SubMesh> submeshes;
	FAABB                bounds;

	Mesh() = default;

	[[nodiscard]] bool   Empty()        const noexcept { return indices.empty(); }
	[[nodiscard]] Uint32 VertexCount()  const noexcept { return static_cast<Uint32>(vertices.size()); }
	[[nodiscard]] Uint32 IndexCount()   const noexcept { return static_cast<Uint32>(indices.size()); }
	[[nodiscard]] Uint32 TriangleCount()const noexcept { return static_cast<Uint32>(indices.size() / 3); }

	void Clear() {
		vertices.clear();
		indices.clear();
		submeshes.clear();
		bounds = {};
	}

	/// Append a vertex and return its index.
	Uint32 AddVertex(const MeshVertex& v) {
		Uint32 i = static_cast<Uint32>(vertices.size());
		vertices.push_back(v);
		return i;
	}

	/// Append a triangle by referencing three existing vertex indices.
	void AddTriangle(Uint32 a, Uint32 b, Uint32 c) {
		indices.push_back(a);
		indices.push_back(b);
		indices.push_back(c);
	}

	/// Recompute `bounds` from the current vertex positions.
	FAABB& RecomputeBounds() noexcept {
		bounds = {};
		for (const MeshVertex& v : vertices) bounds.Expand(v.position);
		return bounds;
	}

	/**
	 * Recompute smooth per-vertex normals by area-weighted face averaging.
	 *
	 * Existing normals are overwritten. Requires an indexed triangle list.
	 */
	void RecomputeNormals() noexcept {
		for (MeshVertex& v : vertices) v.normal = {};
		for (size_t i = 0; i + 2 < indices.size(); i += 3) {
			Uint32 ia = indices[i], ib = indices[i + 1], ic = indices[i + 2];
			const FVector3& a = vertices[ia].position;
			const FVector3& b = vertices[ib].position;
			const FVector3& c = vertices[ic].position;
			FVector3 faceN = (b - a).Cross(c - a); // length ∝ 2 × triangle area
			vertices[ia].normal += faceN;
			vertices[ib].normal += faceN;
			vertices[ic].normal += faceN;
		}
		for (MeshVertex& v : vertices) v.normal = v.normal.Normalize();
	}

	/// Iterate every triangle as a triplet of positions.
	template<class Fn>
	void ForEachTriangle(Fn&& fn) const {
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
			fn(vertices[indices[i]].position,
				 vertices[indices[i + 1]].position,
				 vertices[indices[i + 2]].position);
	}

	// ── Primitive factories ─────────────────────────────────────────────────

	/// Axis-aligned cube centred on the origin with per-face normals.
	[[nodiscard]] static Mesh Cube(float size = 1.f) {
		const float h = size * 0.5f;
		Mesh m;
		struct Face { FVector3 n, u, v; };
		const Face faces[6] = {
			{{ 0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, // +Z
			{{ 0, 0,-1}, {-1,0, 0}, {0, 1, 0}}, // -Z
			{{ 1, 0, 0}, {0, 0,-1}, {0, 1, 0}}, // +X
			{{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, // -X
			{{ 0, 1, 0}, {1, 0, 0}, {0, 0,-1}}, // +Y
			{{ 0,-1, 0}, {1, 0, 0}, {0, 0, 1}}, // -Y
		};
		for (const Face& f : faces) {
			Uint32 base = m.VertexCount();
			FVector3 c = f.n * h;
			m.AddVertex({c - f.u * h - f.v * h, f.n, {0, 0}});
			m.AddVertex({c + f.u * h - f.v * h, f.n, {1, 0}});
			m.AddVertex({c + f.u * h + f.v * h, f.n, {1, 1}});
			m.AddVertex({c - f.u * h + f.v * h, f.n, {0, 1}});
			m.AddTriangle(base, base + 1, base + 2);
			m.AddTriangle(base, base + 2, base + 3);
		}
		m.RecomputeBounds();
		return m;
	}

	/// Unit quad on the XY plane (facing +Z), centred on the origin.
	[[nodiscard]] static Mesh Quad(float w = 1.f, float h = 1.f) {
		const float hw = w * 0.5f, hh = h * 0.5f;
		Mesh m;
		m.AddVertex({{-hw, -hh, 0}, {0, 0, 1}, {0, 1}});
		m.AddVertex({{ hw, -hh, 0}, {0, 0, 1}, {1, 1}});
		m.AddVertex({{ hw,  hh, 0}, {0, 0, 1}, {1, 0}});
		m.AddVertex({{-hw,  hh, 0}, {0, 0, 1}, {0, 0}});
		m.AddTriangle(0, 1, 2);
		m.AddTriangle(0, 2, 3);
		m.RecomputeBounds();
		return m;
	}

	/// Subdivided ground plane on the XZ plane (facing +Y).
	[[nodiscard]] static Mesh Plane(float size = 10.f, int subdivisions = 1) {
		const int n = subdivisions < 1 ? 1 : subdivisions;
		const float step = size / n;
		const float start = -size * 0.5f;
		Mesh m;
		for (int z = 0; z <= n; ++z)
			for (int x = 0; x <= n; ++x)
				m.AddVertex({{start + x * step, 0.f, start + z * step},
										 {0.f, 1.f, 0.f},
										 {static_cast<float>(x) / n, static_cast<float>(z) / n}});
		for (int z = 0; z < n; ++z)
			for (int x = 0; x < n; ++x) {
				Uint32 i0 = static_cast<Uint32>(z * (n + 1) + x);
				Uint32 i1 = i0 + 1;
				Uint32 i2 = i0 + (n + 1);
				Uint32 i3 = i2 + 1;
				m.AddTriangle(i0, i2, i1);
				m.AddTriangle(i1, i2, i3);
			}
		m.RecomputeBounds();
		return m;
	}

	/// UV sphere centred on the origin.
	[[nodiscard]] static Mesh Sphere(float radius = 0.5f, int stacks = 16, int slices = 24) {
		stacks = stacks < 2 ? 2 : stacks;
		slices = slices < 3 ? 3 : slices;
		Mesh m;
		for (int i = 0; i <= stacks; ++i) {
			float v   = static_cast<float>(i) / stacks;
			float phi = v * SDL_PI_F;
			float y   = SDL::Cos(phi);
			float r   = SDL::Sin(phi);
			for (int j = 0; j <= slices; ++j) {
				float u     = static_cast<float>(j) / slices;
				float theta = u * 2.f * SDL_PI_F;
				FVector3 nrm{r * SDL::Cos(theta), y, r * SDL::Sin(theta)};
				m.AddVertex({nrm * radius, nrm, {u, v}});
			}
		}
		const int ring = slices + 1;
		for (int i = 0; i < stacks; ++i)
			for (int j = 0; j < slices; ++j) {
				Uint32 a = static_cast<Uint32>(i * ring + j);
				Uint32 b = static_cast<Uint32>((i + 1) * ring + j);
				m.AddTriangle(a, b, a + 1);
				m.AddTriangle(a + 1, b, b + 1);
			}
		m.RecomputeBounds();
		return m;
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuMesh (GPU resource)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * GPU resident mesh: owns a vertex buffer and a 32-bit index buffer.
 *
 * Move-only RAII: the buffers are released back to the device on destruction.
 *
 * ```cpp
 * SDL::GpuMesh gm;
 * gm.Upload(device, SDL::Mesh::Cube());
 * // inside a render pass, after binding a compatible pipeline:
 * gm.BindAndDraw(pass);
 * ```
 */
class GpuMesh {
public:
	GpuMesh() = default;

	GpuMesh(const GpuMesh&)            = delete;
	GpuMesh& operator=(const GpuMesh&) = delete;

	GpuMesh(GpuMesh&& o) noexcept { _Steal(o); }
	GpuMesh& operator=(GpuMesh&& o) noexcept {
		if (this != &o) { Release(); _Steal(o); }
		return *this;
	}

	~GpuMesh() { Release(); }

	/**
	 * Stream a CPU `Mesh` into freshly created GPU buffers.
	 *
	 * Any previously held buffers are released first. Returns false (and leaves
	 * the resource empty) when the mesh has no geometry or a buffer/command
	 * could not be created.
	 *
	 * @param device the GPU device that owns the buffers.
	 * @param mesh   the geometry to upload.
	 */
	bool Upload(GPUDeviceRef device, const Mesh& mesh) {
		Release();
		if (mesh.vertices.empty() || mesh.indices.empty()) return false;

		m_device      = device;
		m_vertexCount = mesh.VertexCount();
		m_indexCount  = mesh.IndexCount();
		m_bounds      = mesh.bounds;

		const Uint32 vbSize = static_cast<Uint32>(mesh.vertices.size() * sizeof(MeshVertex));
		const Uint32 ibSize = static_cast<Uint32>(mesh.indices.size()  * sizeof(Uint32));

		m_vbo = GPUBuffer(device, GPUBufferCreateInfo{
			.usage = SDL_GPU_BUFFERUSAGE_VERTEX, .size = vbSize, .props = 0});
		m_ibo = GPUBuffer(device, GPUBufferCreateInfo{
			.usage = SDL_GPU_BUFFERUSAGE_INDEX, .size = ibSize, .props = 0});
		if (!static_cast<GPUBufferRaw>(m_vbo) || !static_cast<GPUBufferRaw>(m_ibo)) {
			Release();
			return false;
		}

		// Stage both buffers through a single transfer buffer.
		GPUTransferBuffer staging(device, GPUTransferBufferCreateInfo{
			.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = vbSize + ibSize, .props = 0});
		if (auto* dst = static_cast<Uint8*>(device.MapTransferBuffer(staging, false))) {
			std::memcpy(dst,          mesh.vertices.data(), vbSize);
			std::memcpy(dst + vbSize, mesh.indices.data(),  ibSize);
			device.UnmapTransferBuffer(staging);
		} else {
			device.ReleaseTransferBuffer(staging);
			Release();
			return false;
		}

		GPUCommandBuffer cmd = device.AcquireCommandBuffer();
		GPUCopyPass copy = cmd.BeginCopyPass();
		copy.UploadToBuffer(
			GPUTransferBufferLocation{.transfer_buffer = staging, .offset = 0},
			GPUBufferRegion{.buffer = m_vbo, .offset = 0, .size = vbSize}, false);
		copy.UploadToBuffer(
			GPUTransferBufferLocation{.transfer_buffer = staging, .offset = vbSize},
			GPUBufferRegion{.buffer = m_ibo, .offset = 0, .size = ibSize}, false);
		copy.End();
		cmd.Submit();

		device.WaitForIdle();                 // ensure the copy finished…
		device.ReleaseTransferBuffer(staging); // …before freeing the staging buffer
		return true;
	}

	/// Release the GPU buffers and reset to the empty state.
	void Release() {
		if (m_device) {
			if (static_cast<GPUBufferRaw>(m_vbo)) m_device.ReleaseBuffer(m_vbo);
			if (static_cast<GPUBufferRaw>(m_ibo)) m_device.ReleaseBuffer(m_ibo);
		}
		m_vbo = {};
		m_ibo = {};
		m_vertexCount = m_indexCount = 0;
		m_bounds = {};
		m_device = {};
	}

	/**
	 * Bind this mesh's buffers on the render pass and issue an indexed draw.
	 *
	 * A compatible graphics pipeline must already be bound on the pass.
	 *
	 * @param pass      the active render pass.
	 * @param instances number of instances to draw (default 1).
	 */
	void BindAndDraw(GPURenderPass& pass, Uint32 instances = 1) const {
		if (!m_indexCount) return;
		GPUBufferBinding vbind{.buffer = m_vbo, .offset = 0};
		GPUBufferBinding ibind{.buffer = m_ibo, .offset = 0};
		pass.BindVertexBuffers(0, std::span{&vbind, 1});
		pass.BindIndexBuffer(ibind, GPU_INDEXELEMENTSIZE_32BIT);
		pass.DrawIndexedPrimitives(m_indexCount, instances, 0, 0, 0);
	}

	[[nodiscard]] bool         IsValid()     const noexcept { return m_indexCount > 0; }
	explicit operator bool()                 const noexcept { return IsValid(); }
	[[nodiscard]] Uint32       VertexCount() const noexcept { return m_vertexCount; }
	[[nodiscard]] Uint32       IndexCount()  const noexcept { return m_indexCount; }
	[[nodiscard]] const FAABB& Bounds()      const noexcept { return m_bounds; }
	[[nodiscard]] GPUBuffer    VertexBuffer()const noexcept { return m_vbo; }
	[[nodiscard]] GPUBuffer    IndexBuffer() const noexcept { return m_ibo; }

private:
	GPUDeviceRef m_device{};
	GPUBuffer    m_vbo{};
	GPUBuffer    m_ibo{};
	Uint32       m_vertexCount = 0;
	Uint32       m_indexCount  = 0;
	FAABB        m_bounds{};

	void _Steal(GpuMesh& o) noexcept {
		m_device      = o.m_device;
		m_vbo         = o.m_vbo;
		m_ibo         = o.m_ibo;
		m_vertexCount = o.m_vertexCount;
		m_indexCount  = o.m_indexCount;
		m_bounds      = o.m_bounds;
		o.m_vbo = {};
		o.m_ibo = {};
		o.m_vertexCount = o.m_indexCount = 0;
		o.m_device = {};
	}
};

/** @} */ // CategoryEngineMesh

} // namespace SDL

#endif // SDL3PP_ENGINE_MESH_H_
