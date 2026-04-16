/**
 * @file 03_cube.cpp
 *
 * GPU example 03 – Rotating 3D cube
 *
 * Demonstrates:
 *   - Vertex + index buffers for indexed drawing
 *   - Depth buffer (D16_UNORM) with enable_depth_test / enable_depth_write
 *   - MVP matrix passed via PushVertexUniformData (uniform buffer slot 0)
 *   - Back-face culling
 *   - Lazy depth-texture recreation on window resize
 *
 * Prerequisites:
 *   Run `make shaders` once to compile the GLSL shaders to SPIR-V.
 *   Run this binary from the project root directory so the paths
 *   cube.{vert,frag}.spv are reachable.
 *
 * Controls:
 *   Escape / close window → quit
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <cmath>
#include <stdexcept>
#include <vector>

// ── Vertex layout ─────────────────────────────────────────────────────────────

struct Vertex {
	float x, y, z;    // 3-D position
	float r, g, b, a; // RGBA colour
};

// 8 corners of a unit cube [-1, 1]³
static constexpr Vertex kVerts[8] = {
	// back face  (z = -1)
	{ -1, -1, -1,  0.9f, 0.2f, 0.2f, 1.f }, // 0 – red
	{  1, -1, -1,  0.9f, 0.6f, 0.2f, 1.f }, // 1 – orange
	{  1,  1, -1,  0.9f, 0.9f, 0.2f, 1.f }, // 2 – yellow
	{ -1,  1, -1,  0.5f, 0.9f, 0.2f, 1.f }, // 3 – lime
	// front face (z = +1)
	{ -1, -1,  1,  0.2f, 0.9f, 0.9f, 1.f }, // 4 – cyan
	{  1, -1,  1,  0.2f, 0.4f, 0.9f, 1.f }, // 5 – blue
	{  1,  1,  1,  0.6f, 0.2f, 0.9f, 1.f }, // 6 – purple
	{ -1,  1,  1,  0.9f, 0.2f, 0.6f, 1.f }, // 7 – pink
};

// 12 triangles (36 indices), counter-clockwise winding when viewed from outside
static constexpr Uint16 kIndices[36] = {
	0,1,2, 0,2,3, // back   (-Z)
	5,4,7, 5,7,6, // front  (+Z)
	4,0,3, 4,3,7, // left   (-X)
	1,5,6, 1,6,2, // right  (+X)
	3,2,6, 3,6,7, // top    (+Y)
	4,5,1, 4,1,0, // bottom (-Y)
};

// ── Shader loader ─────────────────────────────────────────────────────────────

static SDL::GPUShader LoadShader(SDL::GPUDeviceRef   device,
																 const char*         path,
																 SDL::GPUShaderStage stage,
																 Uint32 numSamplers        = 0,
																 Uint32 numUniformBuffers  = 0,
																 Uint32 numStorageBuffers  = 0,
																 Uint32 numStorageTextures = 0) {
	std::string fullPath = std::string(SDL::GetBasePath()) + "../../../assets/shaders/bin/gpu/" + path;
	SDL::IOStream io = SDL::IOStream::FromFile(fullPath, "rb");
	Sint64 size = io.GetSize();
	if (size < 0)
		throw std::runtime_error(std::string{"Cannot open shader: "} + path);
	std::vector<Uint8> code(static_cast<size_t>(size));
	io.Read(code);
	io.Close();

	SDL::GPUShaderCreateInfo info{};
	info.code_size            = code.size();
	info.code                 = code.data();
	info.entrypoint           = "main";
	info.format               = SDL::GPU_SHADERFORMAT_SPIRV;
	info.stage                = stage;
	info.num_samplers         = numSamplers;
	info.num_uniform_buffers  = numUniformBuffers;
	info.num_storage_buffers  = numStorageBuffers;
	info.num_storage_textures = numStorageTextures;

	return device.CreateShader(info);
}

// ── Application ───────────────────────────────────────────────────────────────

struct Main {
	static constexpr SDL::Point windowSz = {800, 600};

	// ── Initialisation helpers ──────────────────────────────────────────
	static SDL::AppResult Init(Main** m, SDL::AppArgs args) {
		SDL::LogPriority priority = SDL::LOG_PRIORITY_WARN;
		for (auto arg : args) {
			if (arg == "--verbose") priority = SDL::LOG_PRIORITY_VERBOSE;
			else if (arg == "--debug") priority = SDL::LOG_PRIORITY_DEBUG;
			else if (arg == "--info") priority = SDL::LOG_PRIORITY_INFO;
			else if (arg == "--help") {
				SDL::Log("Usage: %s [options]", SDL::GetBasePath());
				SDL::Log("Options:");
				SDL::Log("  --verbose    Set log priority to VERBOSE");
				SDL::Log("  --debug      Set log priority to DEBUG");
				SDL::Log("  --info       Set log priority to INFO");
				SDL::Log("  --help       Show this help message");
				SDL::Log("Press Escape or close the window to quit.");
				SDL::Log("Press arrow keys to rotate the cube.");
				return SDL::APP_EXIT_SUCCESS;
			}
		}
		SDL::SetLogPriorities(priority);
		
		SDL::SetAppMetadata("GPU Cube", "1.0", "com.example.gpu.cube");
		SDL::Init(SDL::INIT_VIDEO);
		*m = new Main();
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
	}

	// ── Members ─────────────────────────────────────────────────────────
	SDL::Window              window{"examples/gpu/03_cube", windowSz};
	SDL::GPUDevice           device{SDL::GPU_SHADERFORMAT_SPIRV, false, nullptr};
	SDL::GPUBuffer           vertexBuffer;
	SDL::GPUBuffer           indexBuffer;
	SDL::GPUTexture          depthTexture;
	SDL::GPUGraphicsPipeline pipeline;
	Uint32                   depthW = 0, depthH = 0;
	float                    angleY = 0.f, angleX = 0.3f;

	// ── Constructor ──────────────────────────────────────────────────────
	Main() {
		device.ClaimWindow(window);
		UploadGeometry();
		BuildPipeline();
	}

	// ── Destructor ───────────────────────────────────────────────────────
	~Main() {
		if (static_cast<SDL::GPUTextureRaw>(depthTexture))
			device.ReleaseTexture(depthTexture);
		device.ReleaseGraphicsPipeline(pipeline);
		device.ReleaseBuffer(indexBuffer);
		device.ReleaseBuffer(vertexBuffer);
		device.ReleaseWindow(window);
	}

	// ── Setup helpers ────────────────────────────────────────────────────

	void UploadGeometry() {
		constexpr Uint32 vbSize = sizeof(kVerts);
		constexpr Uint32 ibSize = sizeof(kIndices);
		constexpr Uint32 totalSize = vbSize + ibSize;

		// Single transfer buffer for both vertex and index data.
		SDL::GPUTransferBufferCreateInfo tbInfo{};
		tbInfo.usage = SDL::GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		tbInfo.size  = totalSize;
		auto tb = device.CreateTransferBuffer(tbInfo);

		auto* base = static_cast<Uint8*>(device.MapTransferBuffer(tb, false));
		SDL_memcpy(base,          kVerts,   vbSize);
		SDL_memcpy(base + vbSize, kIndices, ibSize);
		device.UnmapTransferBuffer(tb);

		// Vertex buffer
		SDL::GPUBufferCreateInfo vbInfo{};
		vbInfo.usage = SDL::GPU_BUFFERUSAGE_VERTEX;
		vbInfo.size  = vbSize;
		vertexBuffer = device.CreateBuffer(vbInfo);

		// Index buffer
		SDL::GPUBufferCreateInfo ibInfo{};
		ibInfo.usage = SDL::GPU_BUFFERUSAGE_INDEX;
		ibInfo.size  = ibSize;
		indexBuffer  = device.CreateBuffer(ibInfo);

		// One copy pass to upload both buffers.
		auto cmdBuf = device.AcquireCommandBuffer();
		auto cp     = cmdBuf.BeginCopyPass();

		SDL::GPUTransferBufferLocation vSrc{tb, 0};
		SDL::GPUBufferRegion           vDst{vertexBuffer, 0, vbSize};
		cp.UploadToBuffer(vSrc, vDst, false);

		SDL::GPUTransferBufferLocation iSrc{tb, vbSize};
		SDL::GPUBufferRegion           iDst{indexBuffer, 0, ibSize};
		cp.UploadToBuffer(iSrc, iDst, false);

		cp.End();
		cmdBuf.Submit();

		device.ReleaseTransferBuffer(tb);
	}

	void BuildPipeline() {
		// Vertex shader uses one uniform buffer (slot 0 → MVP matrix).
		auto vertShader = LoadShader(
			device,
			"cube.vert.spv",
			SDL::GPU_SHADERSTAGE_VERTEX,
			0, 1); // numSamplers=0, numUniformBuffers=1

		auto fragShader = LoadShader(
			device,
			"cube.frag.spv",
			SDL::GPU_SHADERSTAGE_FRAGMENT);

		// Vertex buffer description
		SDL::GPUVertexBufferDescription vbDesc{};
		vbDesc.slot       = 0;
		vbDesc.pitch      = sizeof(Vertex);
		vbDesc.input_rate = SDL::GPU_VERTEXINPUTRATE_VERTEX;

		// Vertex attributes: position (float3) + colour (float4)
		SDL::GPUVertexAttribute attrs[2]{};
		attrs[0].location    = 0;
		attrs[0].buffer_slot = 0;
		attrs[0].format      = SDL::GPU_VERTEXELEMENTFORMAT_FLOAT3;
		attrs[0].offset      = offsetof(Vertex, x);

		attrs[1].location    = 1;
		attrs[1].buffer_slot = 0;
		attrs[1].format      = SDL::GPU_VERTEXELEMENTFORMAT_FLOAT4;
		attrs[1].offset      = offsetof(Vertex, r);

		// Colour target
		SDL::GPUColorTargetDescription ctd{};
		ctd.format = device.GetSwapchainTextureFormat(window);

		SDL::GPUGraphicsPipelineCreateInfo pipeInfo{};
		pipeInfo.vertex_shader   = vertShader;
		pipeInfo.fragment_shader = fragShader;

		pipeInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
		pipeInfo.vertex_input_state.num_vertex_buffers         = 1;
		pipeInfo.vertex_input_state.vertex_attributes          = attrs;
		pipeInfo.vertex_input_state.num_vertex_attributes      = 2;

		pipeInfo.primitive_type = SDL::GPU_PRIMITIVETYPE_TRIANGLELIST;

		// Back-face culling (CCW winding = front face)
		pipeInfo.rasterizer_state.cull_mode   = SDL::GPU_CULLMODE_BACK;
		pipeInfo.rasterizer_state.front_face  = SDL::GPU_FRONTFACE_COUNTER_CLOCKWISE;
		pipeInfo.rasterizer_state.fill_mode   = SDL::GPU_FILLMODE_FILL;

		// Depth test
		pipeInfo.depth_stencil_state.enable_depth_test  = true;
		pipeInfo.depth_stencil_state.enable_depth_write = true;
		pipeInfo.depth_stencil_state.compare_op         = SDL::GPU_COMPAREOP_LESS;

		// Colour + depth targets
		pipeInfo.target_info.color_target_descriptions = &ctd;
		pipeInfo.target_info.num_color_targets         = 1;
		pipeInfo.target_info.depth_stencil_format      = SDL::GPU_TEXTUREFORMAT_D16_UNORM;
		pipeInfo.target_info.has_depth_stencil_target  = true;

		pipeline = device.CreateGraphicsPipeline(pipeInfo);

		device.ReleaseShader(vertShader);
		device.ReleaseShader(fragShader);
	}

	// Creates (or recreates) the depth texture to match the current swapchain size.
	void EnsureDepthTexture(Uint32 w, Uint32 h) {
		if (depthW == w && depthH == h) return;

		if (static_cast<SDL::GPUTextureRaw>(depthTexture))
			device.ReleaseTexture(depthTexture);

		SDL::GPUTextureCreateInfo dtInfo{};
		dtInfo.type                 = SDL::GPU_TEXTURETYPE_2D;
		dtInfo.format               = SDL::GPU_TEXTUREFORMAT_D16_UNORM;
		dtInfo.usage                = SDL::GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
		dtInfo.width                = w;
		dtInfo.height               = h;
		dtInfo.layer_count_or_depth = 1;
		dtInfo.num_levels           = 1;
		dtInfo.sample_count         = SDL::GPU_SAMPLECOUNT_1;

		depthTexture = device.CreateTexture(dtInfo);
		depthW = w;
		depthH = h;
	}

	// ── Per-frame logic ───────────────────────────────────────────────────
	SDL::AppResult Iterate() {
		angleY += 0.01f;

		Uint32 swW = 0, swH = 0;
		auto cmdBuf  = device.AcquireCommandBuffer();
		auto swapTex = cmdBuf.WaitAndAcquireSwapchainTexture(window, &swW, &swH);

		if (!static_cast<SDL::GPUTextureRaw>(swapTex)) {
			cmdBuf.Submit();
			return SDL::APP_CONTINUE;
		}

		EnsureDepthTexture(swW, swH);

		// ── MVP matrix ───────────────────────────────────────────────────
		float aspect = static_cast<float>(swW) / static_cast<float>(swH);

		SDL::FMatrix4 model = SDL::FMatrix4::RotateY(angleY) * SDL::FMatrix4::RotateX(angleX);
		SDL::FMatrix4 view  = SDL::FMatrix4::Translate(0.f, 0.f, -5.f); // camera 5 units back
		SDL::FMatrix4 proj  = SDL::FMatrix4::Perspective(
			SDL_PI_F / 3.f, aspect, 0.1f, 100.f);       // 60° FOV

		SDL::FMatrix4 mvp   = proj * view * model;

		cmdBuf.PushVertexUniformData(0, SDL::SourceBytes{mvp.m, sizeof(mvp.m)});

		// ── Render pass ──────────────────────────────────────────────────
		SDL::GPUColorTargetInfo ct{};
		ct.texture     = swapTex;
		ct.clear_color = {0.05f, 0.05f, 0.10f, 1.0f};
		ct.load_op     = SDL::GPU_LOADOP_CLEAR;
		ct.store_op    = SDL::GPU_STOREOP_STORE;

		SDL::GPUDepthStencilTargetInfo dt{};
		dt.texture          = depthTexture;
		dt.clear_depth      = 1.0f;
		dt.load_op          = SDL::GPU_LOADOP_CLEAR;
		dt.store_op         = SDL::GPU_STOREOP_DONT_CARE;
		dt.stencil_load_op  = SDL::GPU_LOADOP_DONT_CARE;
		dt.stencil_store_op = SDL::GPU_STOREOP_DONT_CARE;

		auto pass = cmdBuf.BeginRenderPass(std::span{&ct, 1}, dt);
		pass.BindPipeline(pipeline);

		SDL::GPUBufferBinding vbBind{vertexBuffer, 0};
		pass.BindVertexBuffers(0, std::span{&vbBind, 1});

		SDL::GPUBufferBinding ibBind{indexBuffer, 0};
		pass.BindIndexBuffer(ibBind, SDL::GPU_INDEXELEMENTSIZE_16BIT);

		pass.DrawIndexedPrimitives(
			static_cast<Uint32>(SDL_arraysize(kIndices)),
			1, 0, 0, 0);
		pass.End();

		cmdBuf.Submit();
		return SDL::APP_CONTINUE;
	}

	// ── Event handling ────────────────────────────────────────────────────
	SDL::AppResult Event(const SDL::Event& event) {
		if (event.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
		if (event.type == SDL::EVENT_KEY_DOWN) {
				switch (event.key.key) {
					case SDL::KEYCODE_ESCAPE:
						return SDL::APP_EXIT_SUCCESS;
					case SDL::KEYCODE_UP:
						angleX -= 0.1f;
						break;
					case SDL::KEYCODE_DOWN:
						angleX += 0.1f;
						break;
					case SDL::KEYCODE_LEFT:
						angleY -= 0.1f;
						break;
					case SDL::KEYCODE_RIGHT:
						angleY += 0.1f;
						break;
					default:
						break;
				}
		}
		return SDL::APP_CONTINUE;
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
