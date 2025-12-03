#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <ostream>
#include <random>

#include "SDL3/SDL_init.h"

#include "bgfx/bgfx.h"
#include "bgfx/platform.h"
#include "bimg/decode.h"
#include "bx/allocator.h"
#include "bx/math.h"
#include "SDL3/SDL_log.h"

using namespace std::chrono;

constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;
constexpr int NUM_BUNNIES = 70000;

struct Vertex {
    float x, y;
    float u, v;
    uint32_t color;

    static bgfx::VertexLayout layout;
    static void init() {
        layout
            .begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }
};

bgfx::VertexLayout Vertex::layout;

void logError(const char* errorText) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s: %s", errorText, SDL_GetError());
}

constexpr float NANOS_IN_MILLIS = 1000000.0;
float getMillisElapsed(const time_point<steady_clock>& a, const time_point<steady_clock>& b) {
    return static_cast<float>(duration_cast<nanoseconds>(a - b).count()) / NANOS_IN_MILLIS;
}

struct ReadFileResult {
    const char* data;
    const std::streamsize size;
};

bgfx::ShaderHandle loadShader(const char* filename) {
    std::string shaderFormat;
    switch (bgfx::getRendererType()) {
        case bgfx::RendererType::Direct3D11:
        case bgfx::RendererType::Direct3D12:
            shaderFormat = "dx11";
            break;
        case bgfx::RendererType::Agc:
        case bgfx::RendererType::Gnm:
            shaderFormat = "pssl";
            break;
        case bgfx::RendererType::Metal:
            shaderFormat = "metal";
            break;
        case bgfx::RendererType::Nvn:
            shaderFormat = "nvn";
            break;
        case bgfx::RendererType::OpenGL:
            shaderFormat = "glsl";
            break;
        case bgfx::RendererType::OpenGLES:
            shaderFormat = "essl";
            break;
        case bgfx::RendererType::Vulkan:
            shaderFormat = "spirv";
            break;
        default:
            // TODO Print error and exit
            break;
    }

    std::ifstream stream("shaders/bgfx_simple/" + shaderFormat + "/" + filename + ".bin");

    stream.seekg(0, std::ios::end);
    const std::streamsize size = stream.tellg();
    stream.seekg(0, std::ios::beg);

    char data[size];
    stream.read(data, size);

    const bgfx::Memory* mem = bgfx::copy(data, size);
    return bgfx::createShader(mem);
}

int main() {
    Vertex::init();

    // Initial SDL setup
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        logError("Failed to initialize SDL");
        SDL_Quit();
        return 1;
    }

    // Create the window
    SDL_Window* window = SDL_CreateWindow(
        "BGFX Bunnymark",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0
    );
    if (!window) {
        logError("Failed to initialize window");
        SDL_Quit();
        return 1;
    }

    // Initialize bgfx
    bgfx::Init init;
    // uncomment to change renderer
    // init.type = bgfx::RendererType::OpenGL;
    init.resolution.width = WINDOW_WIDTH;
    init.resolution.height = WINDOW_HEIGHT;
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
#if defined(SDL_PLATFORM_WIN32)
    init.platformData.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#elif defined(SDL_PLATFORM_MACOS)
    init.platformData.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
#elif defined(SDL_PLATFORM_LINUX)
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        init.platformData.nwh = reinterpret_cast<void*>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
        init.platformData.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    } else if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland") == 0) {
        init.platformData.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        init.platformData.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
    }
#elif defined(SDL_PLATFORM_IOS)
    init.platformData.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
#elif defined(SDL_PLATFORM_ANDROID)
    init.platformData.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_SURFACE_POINTER, nullptr);
    init.platformData.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_ANDROID_DISPLAY_POINTER, nullptr);
#elif defined(EMSCRIPTEN)
    init.platformData.nwh = reinterpret_cast<void*>("#canvas");
#endif
    bgfx::init(init);

    bgfx::setViewClear(0, BGFX_CLEAR_COLOR, 0x8080ffff);

    // Load shaders
    const bgfx::ShaderHandle vertShader = loadShader("vs_bunny.sc");
    const bgfx::ShaderHandle fragShader = loadShader("fs_bunny.sc");
    const bgfx::ProgramHandle program = bgfx::createProgram(vertShader, fragShader, true);

    //
    // Load bunny texture
    //

    std::ifstream stream("../bunny.png", std::ios::binary);

    stream.seekg(0, std::ios::end);
    const std::streamsize size = stream.tellg();
    stream.seekg(0, std::ios::beg);

    char data[size];
    stream.read(data, size);

    bx::DefaultAllocator allocator;
    bimg::ImageContainer* image = bimg::imageParse(&allocator, data, size);
    const uint16_t w = image->m_width;
    const uint16_t h = image->m_height;
    const uint16_t hw = w / 2;
    const uint16_t hh = h / 2;
    bgfx::TextureHandle bunnyTexture = bgfx::createTexture2D(
        w,
        h,
        image->m_numMips > 1,
        bx::max(image->m_numLayers, 1u),
        static_cast<bgfx::TextureFormat::Enum>(image->m_format),
        // BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT,
    BGFX_TEXTURE_NONE,
        bgfx::copy(image->m_data, image->m_size)
    );
    bimg::imageFree(image);

    if (!bgfx::isValid(bunnyTexture)) {
        SDL_SetError("Texture invalid");
        logError("Failed to load texture");
        bgfx::destroy(program);
        bgfx::shutdown();
        SDL_Quit();
        return 1;
    }

    // Create index buffer
    uint32_t indices[NUM_BUNNIES * 6];
    uint32_t idx = -1;
    for (uint32_t i = 0; i < NUM_BUNNIES; i++) {
        uint32_t base = i * 4;
        indices[++idx] = base + 0;
        indices[++idx] = base + 1;
        indices[++idx] = base + 2;
        indices[++idx] = base + 0;
        indices[++idx] = base + 2;
        indices[++idx] = base + 3;
    }

    // create index buffer (note: using 32-bit indices)
    bgfx::IndexBufferHandle indexBuffer = bgfx::createIndexBuffer(
        bgfx::makeRef(indices, sizeof(indices)),
        BGFX_BUFFER_INDEX32
    );

    // Create the sampler
    const bgfx::UniformHandle sampler = bgfx::createUniform("s_texColor",  bgfx::UniformType::Sampler);

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

    //
    // Position the camera
    //

    float view[16];
    constexpr bx::Vec3 eye = { 0.0f, 0.0f, -1.0f };
    constexpr bx::Vec3 at  = { 0.0f, 0.0f, 0.0f };
    bx::mtxLookAt(view, eye, at);

    float proj[16];
    bx::mtxOrtho(
        proj,
        0,
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0,
        0,
        1,
        0,
        false
    );

    bgfx::setViewTransform(0, view, proj);

    //
    // Start the game loop
    //

    auto lastTick = steady_clock::now();
    auto lastFpsMeasurement = steady_clock::now();
    float dt = 0;
    uint32_t framesInLastSecond = 0;

    bool running = true;
    SDL_Event event;

    bgfx::setViewRect(0, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);

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

        // Measure FPS and report every second
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

        bgfx::TransientVertexBuffer vertexBuffer;
        bgfx::allocTransientVertexBuffer(&vertexBuffer, NUM_BUNNIES * 4, Vertex::layout);
        auto data = reinterpret_cast<Vertex*>(vertexBuffer.data);
        int idx = -1;
        for (int i = 0; i < NUM_BUNNIES; i++) {
            Bunny& b = bunnies[i];
            data[++idx] = {b.x - hw, b.y + hh, 0, 1, 0xffffffff}; // top-left
            data[++idx] = {b.x + hw, b.y + hh, 1, 1, 0xffffffff}; // top-right
            data[++idx] = {b.x + hw, b.y - hh, 1, 0, 0xffffffff}; // bottom-right
            data[++idx] = {b.x - hw, b.y - hh, 0, 0, 0xffffffff}; // bottom-left
        }
        bgfx::setVertexBuffer(0, &vertexBuffer);

        bgfx::setTexture(0, sampler, bunnyTexture);

        bgfx::setIndexBuffer(indexBuffer);

        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_BLEND_ALPHA);

        bgfx::submit(0, program);

        bgfx::frame();
    }

    bgfx::destroy(bunnyTexture);
    bgfx::destroy(sampler);
    bgfx::destroy(indexBuffer);
    bgfx::destroy(program);
    bgfx::shutdown();
    SDL_Quit();
    return 0;
}
