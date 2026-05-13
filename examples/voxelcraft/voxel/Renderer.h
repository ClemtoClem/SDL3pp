#pragma once
// ══════════════════════════════════════════════════════════════════════════
//  Renderer.h – Pipeline GPU 3D (off-screen) + intégration SDL::Renderer
// ══════════════════════════════════════════════════════════════════════════

#ifndef SDL3PP_ENABLE_IMAGE
#define SDL3PP_ENABLE_IMAGE 1
#endif

#include "ChunkMesher.h"
#include "World.h"
#include "Camera.h"
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_gpu.h>
#include <SDL3pp/SDL3pp_render.h>
#include <SDL3pp/SDL3pp_image.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

// ── Renderer ──────────────────────────────────────────────────────────────────
// Renders the voxel world to an off-screen GPU texture, which the SDL Renderer
// can then composite with 2D UI via RenderTexture().
class Renderer {
public:
    SDL::Renderer&   renderer2d;
    SDL::GPUDeviceRef device;

    SDL::GPUTexture          texArray;
    SDL::GPUSampler          sampler;
    SDL::GPUGraphicsPipeline pipeline;

    // Off-screen render targets
    SDL::GPUTexture colorTex;
    SDL::GPUTexture depthTex;
    SDL::Texture    colorSdlTex;   // SDL::Texture wrapping colorTex for renderer2d
    Uint32 rtW = 0, rtH = 0;

    explicit Renderer(SDL::Renderer& r2d)
        : renderer2d(r2d), device(r2d.GetGPUDevice())
    {
        LoadTextureArray();
        CreateSampler();
        BuildPipeline();
    }

    ~Renderer() {
        device.ReleaseTexture(texArray);
        device.ReleaseSampler(sampler);
        device.ReleaseGraphicsPipeline(pipeline);
        if ((SDL::GPUTextureRaw)colorTex) device.ReleaseTexture(colorTex);
        if ((SDL::GPUTextureRaw)depthTex)  device.ReleaseTexture(depthTex);
        // colorSdlTex is destroyed by its RAII wrapper
    }

    // Returns the SDL::Texture wrapping the off-screen color buffer.
    SDL::TextureRef GetColorSdlTex() { return colorSdlTex.Get(); }

    // Rebuild and upload chunks whose mesh is dirty.
    void UploadDirtyChunks(World& world) {
        world.ecs.Each<ChunkPos, ChunkBlocks, ChunkMesh>(
            [&](SDL::ECS::EntityId, ChunkPos& pos, ChunkBlocks& cb, ChunkMesh& mesh) {
                if (mesh.dirty)
                    BuildChunkMesh(pos, cb, world, mesh);
                if (mesh.gpuDirty && mesh.indexCount > 0)
                    UploadChunk(mesh);
            });
    }

    // Render the world to the off-screen color texture.
    // Caller is responsible for blitting GetColorSdlTex() via renderer2d.
    void DrawFrame(World& world, const Camera& cam, float fovY = 1.047f) {
        SDL::Point sz = renderer2d.GetCurrentOutputSize();
        Uint32 w = (Uint32)sz.x, h = (Uint32)sz.y;
        if (w == 0 || h == 0) return;

        EnsureRT(w, h);

        float aspect = (float)w / (float)h;
        SDL::FMatrix4 proj = SDL::FMatrix4::Perspective(fovY, aspect, 0.05f, 1000.f);
        SDL::FMatrix4 view = cam.GetView();
        SDL::FMatrix4 vp   = proj * view;
        SDL::FFrustum frustum = SDL::FFrustum::FromViewProj(vp);

        auto cmdBuf = device.AcquireCommandBuffer();

        SDL::GPUColorTargetInfo ct{};
        ct.texture     = colorTex;
        ct.clear_color = {0.40f, 0.68f, 0.98f, 1.0f};
        ct.load_op     = SDL::GPU_LOADOP_CLEAR;
        ct.store_op    = SDL::GPU_STOREOP_STORE;

        SDL::GPUDepthStencilTargetInfo dt{};
        dt.texture           = depthTex;
        dt.clear_depth       = 1.0f;
        dt.load_op           = SDL::GPU_LOADOP_CLEAR;
        dt.store_op          = SDL::GPU_STOREOP_DONT_CARE;
        dt.stencil_load_op   = SDL::GPU_LOADOP_DONT_CARE;
        dt.stencil_store_op  = SDL::GPU_STOREOP_DONT_CARE;

        auto pass = cmdBuf.BeginRenderPass(std::span{&ct, 1}, dt);
        pass.BindPipeline(pipeline);

        SDL::GPUTextureSamplerBinding tsb{texArray, sampler};
        pass.BindFragmentSamplers(0, std::span{&tsb, 1});

        world.ecs.Each<ChunkPos, ChunkMesh>(
            [&](SDL::ECS::EntityId, ChunkPos& pos, ChunkMesh& mesh) {
                if (mesh.indexCount == 0 || !(SDL::GPUBufferRaw)mesh.vbuf) return;

                SDL::FVector3 orig{(float)(pos.cx * CHUNK_W),
                                   (float)(pos.cy * CHUNK_H),
                                   (float)(pos.cz * CHUNK_D)};
                SDL::FAABB box{orig, orig + SDL::FVector3{(float)CHUNK_W, (float)CHUNK_H, (float)CHUNK_D}};
                if (!frustum.Intersects(box)) return;

                SDL::FMatrix4 mvp = vp * SDL::FMatrix4::Translate(orig);
                cmdBuf.PushVertexUniformData(0, SDL::SourceBytes{mvp.m, sizeof(mvp.m)});

                SDL::GPUBufferBinding vbBind{mesh.vbuf, 0};
                pass.BindVertexBuffers(0, std::span{&vbBind, 1});

                SDL::GPUBufferBinding ibBind{mesh.ibuf, 0};
                pass.BindIndexBuffer(ibBind, SDL::GPU_INDEXELEMENTSIZE_32BIT);

                pass.DrawIndexedPrimitives(mesh.indexCount, 1, 0, 0, 0);
            });

        pass.End();
        cmdBuf.Submit();
    }

private:
    // ── Off-screen render targets ─────────────────────────────────────────────
    void EnsureRT(Uint32 w, Uint32 h) {
        if (w == rtW && h == rtH) return;

        if ((SDL::GPUTextureRaw)colorTex) device.ReleaseTexture(colorTex);
        if ((SDL::GPUTextureRaw)depthTex)  device.ReleaseTexture(depthTex);
        // colorSdlTex is destroyed by its own destructor
        colorSdlTex = SDL::Texture{};

        // Color target (RGBA8 UNORM, sampler + color target)
        SDL::GPUTextureCreateInfo cci{};
        cci.type                 = SDL::GPU_TEXTURETYPE_2D;
        cci.format               = SDL::GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        cci.usage                = SDL::GPU_TEXTUREUSAGE_COLOR_TARGET | SDL::GPU_TEXTUREUSAGE_SAMPLER;
        cci.width                = w;
        cci.height               = h;
        cci.layer_count_or_depth = 1;
        cci.num_levels           = 1;
        cci.sample_count         = SDL::GPU_SAMPLECOUNT_1;
        colorTex = device.CreateTexture(cci);

        // Wrap the GPU texture as SDL::Texture for use with renderer2d
        SDL::Properties props = SDL::Properties::Create();
        props.SetPointerProperty(SDL::prop::Texture::CREATE_GPU_TEXTURE_POINTER,
                                 (void*)(SDL::GPUTextureRaw)colorTex);
        props.SetNumberProperty(SDL::prop::Texture::CREATE_WIDTH_NUMBER,  (Sint64)w);
        props.SetNumberProperty(SDL::prop::Texture::CREATE_HEIGHT_NUMBER, (Sint64)h);
        props.SetNumberProperty(SDL::prop::Texture::CREATE_FORMAT_NUMBER,
                                (Sint64)SDL::PIXELFORMAT_RGBA32);
        colorSdlTex = renderer2d.CreateTextureWithProperties(props);

        // Depth target
        SDL::GPUTextureCreateInfo dci{};
        dci.type                 = SDL::GPU_TEXTURETYPE_2D;
        dci.format               = SDL::GPU_TEXTUREFORMAT_D16_UNORM;
        dci.usage                = SDL::GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        dci.width                = w;
        dci.height               = h;
        dci.layer_count_or_depth = 1;
        dci.num_levels           = 1;
        dci.sample_count         = SDL::GPU_SAMPLECOUNT_1;
        depthTex = device.CreateTexture(dci);

        rtW = w; rtH = h;
    }

    // ── Texture array (block atlas) ───────────────────────────────────────────
    void LoadTextureArray() {
		const std::string basePath = std::string(SDL::GetBasePath()) + "../../../assets/textures/game/";

        int texW = 16, texH = 16;
        std::vector<std::vector<Uint8>> layers;
        layers.reserve(NUM_BLOCK_TEXTURES);

        for (int i = 0; i < NUM_BLOCK_TEXTURES; ++i) {
            const char* fname = ATLAS_TEXTURE_FILES[i];

            SDL::Surface surf;
            try {
                surf = SDL::Surface{(basePath + fname).c_str()};
            } catch (...) {
                SDL::Log("Texture manquante : %s", fname);
            }

            if (i == 0 && surf.Get()) {
                texW = surf.GetWidth();
                texH = surf.GetHeight();
            }

            SDL::Surface rgba;
            try {
                if (surf.Get() && (surf.GetWidth() != texW || surf.GetHeight() != texH))
                    surf = surf.Scale({texW, texH}, SDL::SCALEMODE_NEAREST);
                if (surf.Get())
                    rgba = surf.Convert(SDL::PIXELFORMAT_RGBA32);
            } catch (...) {}

            if (!rgba.Get()) {
                rgba = SDL::Surface{SDL::Point{texW, texH}, SDL::PIXELFORMAT_RGBA32};
                Uint32* px = (Uint32*)rgba.GetPixels();
                for (int k = 0; k < texW * texH; ++k) px[k] = 0xFFFF00FFu; // magenta
            }

            std::vector<Uint8> px((size_t)(texW * texH * 4));
            const Uint8* src = (const Uint8*)rgba.GetPixels();
            int pitch = rgba.GetPitch();
            for (int row = 0; row < texH; ++row)
                SDL::Memcpy(px.data() + row * texW * 4, src + row * pitch, texW * 4);
            layers.push_back(std::move(px));
        }

        SDL::GPUTextureCreateInfo tci{};
        tci.type                 = SDL::GPU_TEXTURETYPE_2D_ARRAY;
        tci.format               = SDL::GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        tci.usage                = SDL::GPU_TEXTUREUSAGE_SAMPLER;
        tci.width                = (Uint32)texW;
        tci.height               = (Uint32)texH;
        tci.layer_count_or_depth = NUM_BLOCK_TEXTURES;
        tci.num_levels           = 1;
        tci.sample_count         = SDL::GPU_SAMPLECOUNT_1;
        texArray = device.CreateTexture(tci);

        Uint32 layerBytes = (Uint32)(texW * texH * 4);

        SDL::GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL::GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = layerBytes * NUM_BLOCK_TEXTURES;
        auto tb = device.CreateTransferBuffer(tbci);

        auto* dst = static_cast<Uint8*>(device.MapTransferBuffer(tb, false));
        for (int i = 0; i < NUM_BLOCK_TEXTURES; ++i)
            SDL::Memcpy(dst + i * layerBytes, layers[i].data(), layerBytes);
        device.UnmapTransferBuffer(tb);

        auto cmd = device.AcquireCommandBuffer();
        auto cp  = cmd.BeginCopyPass();
        for (int i = 0; i < NUM_BLOCK_TEXTURES; ++i) {
            SDL::GPUTextureTransferInfo si{};
            si.transfer_buffer = tb;
            si.offset          = (Uint32)(i * layerBytes);

            SDL::GPUTextureRegion di{};
            di.texture = texArray;
            di.layer   = (Uint32)i;
            di.w       = (Uint32)texW;
            di.h       = (Uint32)texH;
            di.d       = 1;

            cp.UploadToTexture(si, di, false);
        }
        cp.End();
        cmd.Submit();
        device.ReleaseTransferBuffer(tb);
    }

    // ── Nearest-neighbor sampler ──────────────────────────────────────────────
    void CreateSampler() {
        SDL::GPUSamplerCreateInfo sci{};
        sci.min_filter     = SDL::GPU_FILTER_NEAREST;
        sci.mag_filter     = SDL::GPU_FILTER_NEAREST;
        sci.mipmap_mode    = SDL::GPU_SAMPLERMIPMAPMODE_NEAREST;
        sci.address_mode_u = SDL::GPU_SAMPLERADDRESSMODE_REPEAT;
        sci.address_mode_v = SDL::GPU_SAMPLERADDRESSMODE_REPEAT;
        sci.address_mode_w = SDL::GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler = device.CreateSampler(sci);
    }

    // ── Voxel GPU pipeline ────────────────────────────────────────────────────
    static SDL::GPUShader LoadShader(SDL::GPUDeviceRef dev, const char* file,
                                      SDL::GPUShaderStage stage,
                                      Uint32 numSamplers = 0,
                                      Uint32 numUniform  = 0) {
        std::string path = std::string(SDL::GetBasePath()) + "../../../assets/shaders/bin/gpu/" + file;
        SDL::IOStream io = SDL::IOStream::FromFile(path, "rb");
        Sint64 sz = io.GetSize();
        if (sz < 0) throw std::runtime_error(std::string("Shader introuvable : ") + file);
        std::vector<Uint8> code((size_t)sz);
        io.Read(code);
        io.Close();

        SDL::GPUShaderCreateInfo info{};
        info.code_size           = code.size();
        info.code                = code.data();
        info.entrypoint          = "main";
        info.format              = SDL::GPU_SHADERFORMAT_SPIRV;
        info.stage               = stage;
        info.num_samplers        = numSamplers;
        info.num_uniform_buffers = numUniform;
        return dev.CreateShader(info);
    }

    void BuildPipeline() {
        auto vert = LoadShader(device, "voxel.vert.spv", SDL::GPU_SHADERSTAGE_VERTEX,  0, 1);
        auto frag = LoadShader(device, "voxel.frag.spv", SDL::GPU_SHADERSTAGE_FRAGMENT, 1, 0);

        SDL::GPUVertexBufferDescription vbd{};
        vbd.slot       = 0;
        vbd.pitch      = sizeof(VoxelVertex);
        vbd.input_rate = SDL::GPU_VERTEXINPUTRATE_VERTEX;

        SDL::GPUVertexAttribute attrs[4]{};
        attrs[0] = {0, 0, SDL::GPU_VERTEXELEMENTFORMAT_FLOAT3, (Uint32)offsetof(VoxelVertex, x)};
        attrs[1] = {1, 0, SDL::GPU_VERTEXELEMENTFORMAT_FLOAT2, (Uint32)offsetof(VoxelVertex, u)};
        attrs[2] = {2, 0, SDL::GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(VoxelVertex, layer)};
        attrs[3] = {3, 0, SDL::GPU_VERTEXELEMENTFORMAT_FLOAT,  (Uint32)offsetof(VoxelVertex, light)};

        SDL::GPUColorTargetDescription ctd{};
        ctd.format = SDL::GPU_TEXTUREFORMAT_R8G8B8A8_UNORM; // matches off-screen target

        SDL::GPUGraphicsPipelineCreateInfo pci{};
        pci.vertex_shader   = vert;
        pci.fragment_shader = frag;

        pci.vertex_input_state.vertex_buffer_descriptions = &vbd;
        pci.vertex_input_state.num_vertex_buffers         = 1;
        pci.vertex_input_state.vertex_attributes          = attrs;
        pci.vertex_input_state.num_vertex_attributes      = 4;

        pci.primitive_type = SDL::GPU_PRIMITIVETYPE_TRIANGLELIST;

        pci.rasterizer_state.cull_mode  = SDL::GPU_CULLMODE_NONE;
        pci.rasterizer_state.front_face = SDL::GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pci.rasterizer_state.fill_mode  = SDL::GPU_FILLMODE_FILL;

        pci.depth_stencil_state.enable_depth_test  = true;
        pci.depth_stencil_state.enable_depth_write = true;
        pci.depth_stencil_state.compare_op         = SDL::GPU_COMPAREOP_LESS;

        pci.target_info.color_target_descriptions  = &ctd;
        pci.target_info.num_color_targets          = 1;
        pci.target_info.depth_stencil_format       = SDL::GPU_TEXTUREFORMAT_D16_UNORM;
        pci.target_info.has_depth_stencil_target   = true;

        pipeline = device.CreateGraphicsPipeline(pci);
        device.ReleaseShader(vert);
        device.ReleaseShader(frag);
    }

    // ── Upload one chunk mesh to GPU ──────────────────────────────────────────
    void UploadChunk(ChunkMesh& mesh) {
        Uint32 vbSz = (Uint32)(mesh.vertices.size() * sizeof(VoxelVertex));
        Uint32 ibSz = (Uint32)(mesh.indices.size()  * sizeof(uint32_t));

        if ((SDL::GPUBufferRaw)mesh.vbuf) device.ReleaseBuffer(mesh.vbuf);
        if ((SDL::GPUBufferRaw)mesh.ibuf) device.ReleaseBuffer(mesh.ibuf);

        SDL::GPUBufferCreateInfo vbci{};
        vbci.usage = SDL::GPU_BUFFERUSAGE_VERTEX;
        vbci.size  = vbSz;
        mesh.vbuf = device.CreateBuffer(vbci);

        SDL::GPUBufferCreateInfo ibci{};
        ibci.usage = SDL::GPU_BUFFERUSAGE_INDEX;
        ibci.size  = ibSz;
        mesh.ibuf = device.CreateBuffer(ibci);

        SDL::GPUTransferBufferCreateInfo tbci{};
        tbci.usage = SDL::GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tbci.size  = vbSz + ibSz;
        auto tb = device.CreateTransferBuffer(tbci);

        auto* base = static_cast<Uint8*>(device.MapTransferBuffer(tb, false));
        SDL::Memcpy(base,        mesh.vertices.data(), vbSz);
        SDL::Memcpy(base + vbSz, mesh.indices.data(),  ibSz);
        device.UnmapTransferBuffer(tb);

        auto cmd = device.AcquireCommandBuffer();
        auto cp  = cmd.BeginCopyPass();
        cp.UploadToBuffer({tb, 0},    {mesh.vbuf, 0, vbSz}, false);
        cp.UploadToBuffer({tb, vbSz}, {mesh.ibuf, 0, ibSz}, false);
        cp.End();
        cmd.Submit();
        device.ReleaseTransferBuffer(tb);

        mesh.gpuDirty = false;
    }
};
