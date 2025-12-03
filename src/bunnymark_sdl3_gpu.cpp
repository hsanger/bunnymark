#include <chrono>
#include <ctime>
#include <iostream>
#include <ostream>
#include <random>

#include "SDL3/SDL_gpu.h"
#include "SDL3/SDL_init.h"
#include <vector>

#include "SDL3/SDL_log.h"

using namespace std::chrono;

constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;

constexpr int NUM_BUNNIES = 50000;

typedef struct SpriteInstance
{
    float x, y, z;
    float rotation;
    float w, h, padding_a, padding_b;
    float tex_u, tex_v, tex_w, tex_h;
    float r, g, b, a;
} SpriteInstance;

typedef struct Matrix4x4
{
    float m11, m12, m13, m14;
    float m21, m22, m23, m24;
    float m31, m32, m33, m34;
    float m41, m42, m43, m44;
} Matrix4x4;

Matrix4x4 Matrix4x4_CreateOrthographicOffCenter(
    const float left,
    const float right,
    const float bottom,
    const float top,
    const float zNearPlane,
    const float zFarPlane
) {
    return {
        2.0f / (right - left), 0, 0, 0,
        0, 2.0f / (top - bottom), 0, 0,
        0, 0, 1.0f / (zNearPlane - zFarPlane), 0,
        (left + right) / (left - right), (top + bottom) / (bottom - top), zNearPlane / (zNearPlane - zFarPlane), 1
    };
}

void logError(const char* errorText) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s: %s", errorText, SDL_GetError());
}

constexpr float NANOS_IN_MILLIS = 1000000.0;
float getMillisElapsed(const time_point<steady_clock>& a, const time_point<steady_clock>& b) {
    return static_cast<float>(duration_cast<nanoseconds>(a - b).count()) / NANOS_IN_MILLIS;
}

SDL_GPUShader* loadShader(
    SDL_GPUDevice* device,
    const char* shaderFilename,
    const SDL_GPUShaderStage stage,
    const Uint32 samplerCount,
    const Uint32 storageTextureCount,
    const Uint32 storageBufferCount,
    const Uint32 uniformBufferCount
) {
    char fullPath[256];
    const SDL_GPUShaderFormat supportedFormats = SDL_GetGPUShaderFormats(device);
    SDL_GPUShaderFormat format;
    const char* entrypoint;
    const auto basePath = "../shaders/sdl/compiled";

    if (supportedFormats & SDL_GPU_SHADERFORMAT_SPIRV) {
        SDL_snprintf(fullPath, sizeof(fullPath), "%s/%s.spv", basePath, shaderFilename);
        format = SDL_GPU_SHADERFORMAT_SPIRV;
        entrypoint = "main";
    } else if (supportedFormats & SDL_GPU_SHADERFORMAT_MSL) {
        SDL_snprintf(fullPath, sizeof(fullPath), "%s/%s.msl", basePath, shaderFilename);
        format = SDL_GPU_SHADERFORMAT_MSL;
        entrypoint = "main0";
    } else if (supportedFormats & SDL_GPU_SHADERFORMAT_DXIL) {
        SDL_snprintf(fullPath, sizeof(fullPath), "%s/%s.dxil", basePath, shaderFilename);
        format = SDL_GPU_SHADERFORMAT_DXIL;
        entrypoint = "main";
    } else {
        SDL_SetError("Unknown shader format");
        return nullptr;
    }

    size_t codeSize;
    void* code = SDL_LoadFile(fullPath, &codeSize);
    if (!code) {
        SDL_SetError("Shader file not found");
        return nullptr;
    }

    SDL_GPUShaderCreateInfo shaderInfo = {
        .code_size = codeSize,
        .code =  static_cast<Uint8*>(code),
        .entrypoint = entrypoint,
        .format = format,
        .stage = stage,
        .num_samplers = samplerCount,
        .num_storage_textures = storageTextureCount,
        .num_storage_buffers = storageBufferCount,
        .num_uniform_buffers = uniformBufferCount
    };
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &shaderInfo);
    SDL_free(code);
    return shader;
}

int main() {
    // Initial SDL setup
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logError("Failed to initialize SDL");
        SDL_Quit();
        return 1;
    }

    // Create the window
    SDL_Window* window = SDL_CreateWindow(
        "SDL3 GPU Bunnymark",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0
    );
    if (!window) {
        logError("Failed to initialize window");
        SDL_Quit();
        return 1;
    }

    // Create the GPU device
    SDL_GPUDevice* gpuDevice = SDL_CreateGPUDevice(
    SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
    false,
        nullptr
    );
    if (!gpuDevice) {
        logError("Failed to create GPU device");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Assign the window to the GPU device
    if (!SDL_ClaimWindowForGPUDevice(gpuDevice, window)) {
        logError("Failed to claim GPU device");
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Set swapchain parameters
    if (!SDL_SetGPUSwapchainParameters(
        gpuDevice,
        window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_PRESENTMODE_IMMEDIATE
    )) {
        logError("Failed to set GPU swapchain parameters");
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Load shaders
    SDL_GPUShader* vertShader = loadShader(
        gpuDevice,
        "PullSpriteBatch.vert",
        SDL_GPU_SHADERSTAGE_VERTEX,
        0,
        0,
        1,
        1
    );
    SDL_GPUShader* fragShader = loadShader(
        gpuDevice,
        "TexturedQuadColor.frag",
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        1,
        0,
        0,
        0
    );
    if (!vertShader || !fragShader) {
        logError("Failed to load shaders");
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create graphics pipeline

    SDL_GPUColorTargetDescription colorTargetDescription[] {{
        .format = SDL_GetGPUSwapchainTextureFormat(gpuDevice, window),
        .blend_state = {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .enable_blend = true
        }
    }};

    SDL_GPUGraphicsPipelineTargetInfo targetInfo{
        .color_target_descriptions = colorTargetDescription,
        .num_color_targets = 1
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo{
        .vertex_shader = vertShader,
        .fragment_shader = fragShader,
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .target_info = targetInfo
    };

    auto graphicsPipeline = SDL_CreateGPUGraphicsPipeline(gpuDevice, &pipelineCreateInfo);

    SDL_ReleaseGPUShader(gpuDevice, vertShader);
    SDL_ReleaseGPUShader(gpuDevice, fragShader);

    if (!graphicsPipeline) {
        logError("Failed to create graphics pipeline");
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
    }

    // Load bunny texture from disk into an SDL_Surface
    SDL_Surface* bunnySurface = SDL_LoadPNG("../bunny.png");
    if (!bunnySurface) {
        logError("Failed to load image bunny.png");
        SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    auto bunnyWidth = static_cast<Uint32>(bunnySurface->w);
    auto bunnyHeight = static_cast<Uint32>(bunnySurface->h);

    // Upload the texture to the GPU
    SDL_GPUTransferBufferCreateInfo textureBufferCreateInfo{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(bunnySurface->w * bunnySurface->h * 4)
    };
    SDL_GPUTransferBuffer* textureTransferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &textureBufferCreateInfo);
    if (!textureTransferBuffer) {
        logError("Failed to create GPU transfer buffer for texture");
        SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    auto textureTransferPtr = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(
        gpuDevice,
        textureTransferBuffer,
        false
    ));
    SDL_memcpy(textureTransferPtr, bunnySurface->pixels, bunnySurface->w * bunnySurface->h * 4);
    SDL_UnmapGPUTransferBuffer(gpuDevice, textureTransferBuffer);

    // Create the actual GPU texture
    SDL_GPUTextureCreateInfo textureCreateInfo{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = bunnyWidth,
        .height = bunnyHeight,
        .layer_count_or_depth = 1,
        .num_levels = 1
    };
    SDL_GPUTexture* bunnyTexture = SDL_CreateGPUTexture(gpuDevice, &textureCreateInfo);
    if (!bunnyTexture) {
        logError("Failed to create GPU texture");
        SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create a sampler, used to bind textures
    SDL_GPUSamplerCreateInfo sampler_info {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
        .address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE
    };
    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(gpuDevice, &sampler_info);
    if (!sampler) {
        logError("Failed to create sampler");
        SDL_ReleaseGPUTexture(gpuDevice, bunnyTexture);
        SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create sprite data transfer buffer
    SDL_GPUTransferBufferCreateInfo spriteDataTransferBufferCreateInfo {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = NUM_BUNNIES * sizeof(SpriteInstance)
    };
    SDL_GPUTransferBuffer* spriteDataTransferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &spriteDataTransferBufferCreateInfo);
    if (!spriteDataTransferBuffer) {
        logError("Failed to create sprite data transfer buffer");
        SDL_ReleaseGPUSampler(gpuDevice, sampler);
        SDL_ReleaseGPUTexture(gpuDevice, bunnyTexture);
        SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Create sprite data buffer
    SDL_GPUBufferCreateInfo spriteDataBufferCreateInfo {
        .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
        .size = NUM_BUNNIES * sizeof(SpriteInstance)
    };
    SDL_GPUBuffer* spriteDataBuffer = SDL_CreateGPUBuffer(gpuDevice, &spriteDataBufferCreateInfo);
    if (!spriteDataBuffer) {
        logError("Failed to create sprite data buffer");
        SDL_ReleaseGPUTransferBuffer(gpuDevice, spriteDataTransferBuffer);
        SDL_ReleaseGPUSampler(gpuDevice, sampler);
        SDL_ReleaseGPUTexture(gpuDevice, bunnyTexture);
        SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
        SDL_DestroyGPUDevice(gpuDevice);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Upload data to the GPU texture

    SDL_GPUCommandBuffer* uploadCommandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);
    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCommandBuffer);

    SDL_GPUTextureTransferInfo textureTransferInfo {
        .transfer_buffer = textureTransferBuffer,
        .offset = 0, /* Zeroes out the rest */
    };
    SDL_GPUTextureRegion textureRegion {
        .texture = bunnyTexture,
        .w = bunnyWidth,
        .h = bunnyHeight,
        .d = 1
    };
    SDL_UploadToGPUTexture(
        copyPass,
        &textureTransferInfo,
        &textureRegion,
        false
    );

    SDL_EndGPUCopyPass(copyPass);
    SDL_SubmitGPUCommandBuffer(uploadCommandBuffer);

    SDL_DestroySurface(bunnySurface);
    SDL_ReleaseGPUTransferBuffer(gpuDevice, textureTransferBuffer);


    //
    // Set up the bunnies
    //

    struct Bunny {
        float x, y;
        float vx, vy;
    };

    std::vector<Bunny> bunnies;
    std::mt19937 rng; // NOLINT deterministic but that's fine here
    std::uniform_real_distribution dis{-1.0f, 1.0f};

    for (int i = 0; i < NUM_BUNNIES; i++) {
        bunnies.push_back({
            .x = static_cast<float>(WINDOW_WIDTH) / 2,
            .y = static_cast<float>(WINDOW_HEIGHT) / 2,
            .vx = dis(rng),
            .vy = dis(rng)
        });
    }

    SDL_GPUTextureSamplerBinding samplerBinding{
        .texture = bunnyTexture,
        .sampler = sampler
    };

    Matrix4x4 cameraMatrix = Matrix4x4_CreateOrthographicOffCenter(
        0,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0,
        0,
        -1
    );


    //
    // Start the game loop
    //

    auto lastTick = steady_clock::now();
    auto lastFpsMeasurement = steady_clock::now();
    float dt = 0;
    uint32_t framesInLastSecond = 0;

    bool running = true;
    SDL_Event event;

    while (running) {
        // Listen for quit event
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        // Get delta time
        auto now = steady_clock::now();
        dt = getMillisElapsed(now, lastTick);
        lastTick = now;

        // Report FPS every second
        framesInLastSecond++;
        if (getMillisElapsed(now, lastFpsMeasurement) > 1000) {
            std::cout << "FPS: " << framesInLastSecond << std::endl;
            framesInLastSecond = 0;
            lastFpsMeasurement = now;
        }

        // Update the bunnies
        for (auto&[x, y, vx, vy] : bunnies) {
            x += vx * dt;
            y += vy * dt;

            if (x < 0 || x > WINDOW_WIDTH - 32) vx *= -1;
            if (y < 0 || y > WINDOW_HEIGHT - 32) vy *= -1;
        }

        //
        // Render the bunnies to the screen
        //

        SDL_GPUCommandBuffer* commandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);

        SDL_GPUTexture* swapchainTexture;
        SDL_WaitAndAcquireGPUSwapchainTexture(
            commandBuffer,
            window,
            &swapchainTexture,
            nullptr,
            nullptr
        );

        // Transfer sprite data to the GPU

        auto dataPtr = static_cast<SpriteInstance*>(SDL_MapGPUTransferBuffer(
            gpuDevice,
            spriteDataTransferBuffer,
            true
        ));
        for (Uint32 i = 0; i < NUM_BUNNIES; i++) {
            Bunny bunny = bunnies[i];
            dataPtr[i].x = bunny.x;
            dataPtr[i].y = bunny.y;
            dataPtr[i].z = 0;
            dataPtr[i].rotation = 0;
            dataPtr[i].w = bunnyWidth;
            dataPtr[i].h = bunnyHeight;
            dataPtr[i].tex_u = 0;
            dataPtr[i].tex_v = 0;
            dataPtr[i].tex_w = 1.0f;
            dataPtr[i].tex_h = 1.0f;
            dataPtr[i].r = 1.0f;
            dataPtr[i].g = 1.0f;
            dataPtr[i].b = 1.0f;
            dataPtr[i].a = 1.0f;
        }
        SDL_UnmapGPUTransferBuffer(gpuDevice, spriteDataTransferBuffer);

        SDL_GPUCopyPass* spriteDataCopyPass = SDL_BeginGPUCopyPass(commandBuffer);
        SDL_GPUTransferBufferLocation bufferLocation{
            .transfer_buffer = spriteDataTransferBuffer,
            .offset = 0
        };
        SDL_GPUBufferRegion bufferRegion{
            .buffer = spriteDataBuffer,
            .offset = 0,
            .size = NUM_BUNNIES * sizeof(SpriteInstance)
        };
        SDL_UploadToGPUBuffer(
            spriteDataCopyPass,
            &bufferLocation,
            &bufferRegion,
            true
        );
        SDL_EndGPUCopyPass(spriteDataCopyPass);

        // Start a render pass
        SDL_GPUColorTargetInfo colorTargetInfo{
            .texture = swapchainTexture,
            .clear_color = { 0.5, 0.5, 1, 1 },
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .store_op = SDL_GPU_STOREOP_STORE,
            .cycle = false
        };
        SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
            commandBuffer,
            &colorTargetInfo,
            1,
            nullptr
        );

        SDL_BindGPUGraphicsPipeline(renderPass, graphicsPipeline);
        SDL_BindGPUVertexStorageBuffers(
            renderPass,
            0,
            &spriteDataBuffer,
            1
        );
        SDL_BindGPUFragmentSamplers(
            renderPass,
            0,
            &samplerBinding,
            1
        );
        SDL_PushGPUVertexUniformData(
            commandBuffer,
            0,
            &cameraMatrix,
            sizeof(Matrix4x4)
        );
        SDL_DrawGPUPrimitives(
            renderPass,
            NUM_BUNNIES * 6,
            1,
            0,
            0
        );

        SDL_EndGPURenderPass(renderPass);

        SDL_SubmitGPUCommandBuffer(commandBuffer);
    }

    SDL_ReleaseGPUGraphicsPipeline(gpuDevice, graphicsPipeline);
    SDL_ReleaseGPUSampler(gpuDevice, sampler);
    SDL_ReleaseGPUTexture(gpuDevice, bunnyTexture);
    SDL_ReleaseGPUTransferBuffer(gpuDevice, spriteDataTransferBuffer);
    SDL_ReleaseGPUBuffer(gpuDevice, spriteDataBuffer);
    SDL_DestroyGPUDevice(gpuDevice);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
