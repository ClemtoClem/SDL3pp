/**
 * @file 02_triangle.cpp
 *
 * GPU example 02 – Coloured triangle
 *
 * Demonstrates:
 *   - Uploading geometry to a GPU vertex buffer via a transfer buffer
 *   - Loading SPIR-V shaders from disk
 *   - Building a graphics pipeline (vertex layout, colour target)
 *   - Drawing with DrawPrimitives
 *
 * Prerequisites:
 *   Run `make shaders` once to compile the GLSL shaders to SPIR-V.
 *   Run this binary from the project root directory so the paths
 *   triangle.{vert,frag}.spv are reachable.
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <stdexcept>
#include <vector>

// ── Vertex layout ─────────────────────────────────────────────────────────────

struct Vertex {
  float x, y;       // 2-D position (NDC)
  float r, g, b, a; // RGBA colour
};

static constexpr Vertex kVerts[] = {
  {  0.0f,  0.5f,  0.95f, 0.26f, 0.21f, 1.0f }, // top      – red
  { -0.5f, -0.5f,  0.30f, 0.69f, 0.31f, 1.0f }, // bot-left – green
  {  0.5f, -0.5f,  0.13f, 0.59f, 0.95f, 1.0f }, // bot-right – blue
};

// ── Shader loader ─────────────────────────────────────────────────────────────

static SDL::GPUShader LoadShader(SDL::GPUDeviceRef  device,
                                 const char*        path,
                                 SDL::GPUShaderStage stage,
                                 Uint32 numSamplers       = 0,
                                 Uint32 numUniformBuffers = 0,
                                 Uint32 numStorageBuffers = 0,
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
        SDL::Log("  --verbose    Set log priority to verbose");
        SDL::Log("  --debug      Set log priority to debug");
        SDL::Log("  --info       Set log priority to info");
        SDL::Log("  --help       Show this help message");
        return SDL::APP_EXIT_SUCCESS;
      }
    }
    SDL::SetLogPriorities(priority);
    
    SDL::SetAppMetadata(
      "GPU Triangle", "1.0", "com.example.gpu.triangle");
    SDL::Init(SDL::INIT_VIDEO);
    *m = new Main();
    return SDL::APP_CONTINUE;
  }

  static void Quit(Main* m, SDL::AppResult) {
    delete m;
  }

  // ── Members ─────────────────────────────────────────────────────────
  SDL::Window            window{"examples/gpu/02_triangle", windowSz};
  SDL::GPUDevice         device{SDL::GPU_SHADERFORMAT_SPIRV, false, nullptr};
  SDL::GPUBuffer         vertexBuffer;
  SDL::GPUGraphicsPipeline pipeline;

  // ── Constructor ──────────────────────────────────────────────────────
  Main() {
    device.ClaimWindow(window);
    UploadVertices();
    BuildPipeline();
  }

  // ── Destructor ───────────────────────────────────────────────────────
  ~Main() {
    device.ReleaseGraphicsPipeline(pipeline);
    device.ReleaseBuffer(vertexBuffer);
    device.ReleaseWindow(window);
  }

  // ── Setup helpers ────────────────────────────────────────────────────

  void UploadVertices() {
    // 1. Create a CPU-side transfer buffer and fill it.
    SDL::GPUTransferBufferCreateInfo tbInfo{};
    tbInfo.usage = SDL::GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    tbInfo.size  = sizeof(kVerts);
    auto tb = device.CreateTransferBuffer(tbInfo);

    auto* dst = static_cast<Vertex*>(device.MapTransferBuffer(tb, false));
    SDL_memcpy(dst, kVerts, sizeof(kVerts));
    device.UnmapTransferBuffer(tb);

    // 2. Create the GPU-side vertex buffer.
    SDL::GPUBufferCreateInfo bufInfo{};
    bufInfo.usage = SDL::GPU_BUFFERUSAGE_VERTEX;
    bufInfo.size  = sizeof(kVerts);
    vertexBuffer  = device.CreateBuffer(bufInfo);

    // 3. Upload via a one-shot copy pass.
    auto cmdBuf = device.AcquireCommandBuffer();
    auto cp     = cmdBuf.BeginCopyPass();

    SDL::GPUTransferBufferLocation src{tb, 0};
    SDL::GPUBufferRegion           dst2{vertexBuffer, 0, sizeof(kVerts)};
    cp.UploadToBuffer(src, dst2, false);
    cp.End();
    cmdBuf.Submit();

    device.ReleaseTransferBuffer(tb);
  }

  void BuildPipeline() {
    auto vertShader = LoadShader(
      device,
      "triangle.vert.spv",
      SDL::GPU_SHADERSTAGE_VERTEX);

    auto fragShader = LoadShader(
      device,
      "triangle.frag.spv",
      SDL::GPU_SHADERSTAGE_FRAGMENT);

    // Vertex buffer: one buffer, per-vertex rate.
    SDL::GPUVertexBufferDescription vbDesc{};
    vbDesc.slot       = 0;
    vbDesc.pitch      = sizeof(Vertex);
    vbDesc.input_rate = SDL::GPU_VERTEXINPUTRATE_VERTEX;

    // Two attributes: position (float2) and colour (float4).
    SDL::GPUVertexAttribute attrs[2]{};
    attrs[0].location    = 0;
    attrs[0].buffer_slot = 0;
    attrs[0].format      = SDL::GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attrs[0].offset      = offsetof(Vertex, x);

    attrs[1].location    = 1;
    attrs[1].buffer_slot = 0;
    attrs[1].format      = SDL::GPU_VERTEXELEMENTFORMAT_FLOAT4;
    attrs[1].offset      = offsetof(Vertex, r);

    // Single colour target matching the swapchain format.
    SDL::GPUColorTargetDescription ctd{};
    ctd.format = device.GetSwapchainTextureFormat(window);

    SDL::GPUGraphicsPipelineCreateInfo pipeInfo{};
    pipeInfo.vertex_shader                             = vertShader;
    pipeInfo.fragment_shader                           = fragShader;
    pipeInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
    pipeInfo.vertex_input_state.num_vertex_buffers         = 1;
    pipeInfo.vertex_input_state.vertex_attributes          = attrs;
    pipeInfo.vertex_input_state.num_vertex_attributes      = 2;
    pipeInfo.primitive_type                            = SDL::GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipeInfo.target_info.color_target_descriptions     = &ctd;
    pipeInfo.target_info.num_color_targets             = 1;

    pipeline = device.CreateGraphicsPipeline(pipeInfo);

    // Shaders are compiled into the pipeline – release the handles.
    device.ReleaseShader(vertShader);
    device.ReleaseShader(fragShader);
  }

  // ── Per-frame logic ───────────────────────────────────────────────────
  SDL::AppResult Iterate() {
    auto cmdBuf  = device.AcquireCommandBuffer();
    auto swapTex = cmdBuf.WaitAndAcquireSwapchainTexture(window);

    if (!static_cast<SDL::GPUTextureRaw>(swapTex)) {
      cmdBuf.Submit();
      return SDL::APP_CONTINUE;
    }

    SDL::GPUColorTargetInfo ct{};
    ct.texture     = swapTex;
    ct.clear_color = {0.05f, 0.05f, 0.08f, 1.0f};
    ct.load_op     = SDL::GPU_LOADOP_CLEAR;
    ct.store_op    = SDL::GPU_STOREOP_STORE;

    auto pass = cmdBuf.BeginRenderPass(std::span{&ct, 1}, std::nullopt);
    pass.BindPipeline(pipeline);

    SDL::GPUBufferBinding binding{vertexBuffer, 0};
    pass.BindVertexBuffers(0, std::span{&binding, 1});

    pass.DrawPrimitives(3, 1, 0, 0);
    pass.End();

    cmdBuf.Submit();
    return SDL::APP_CONTINUE;
  }

  // ── Event handling ────────────────────────────────────────────────────
  SDL::AppResult Event(const SDL::Event& event) {
    if (event.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
    if (event.type == SDL::EVENT_KEY_DOWN &&
        event.key.scancode == SDL::SCANCODE_ESCAPE)
      return SDL::APP_SUCCESS;
    return SDL::APP_CONTINUE;
  }
};

SDL3PP_DEFINE_CALLBACKS(Main)
