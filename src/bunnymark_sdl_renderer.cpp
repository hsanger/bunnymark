#include <chrono>
#include <ctime>
#include <iostream>
#include <ostream>
#include <random>

#include "SDL3/SDL_init.h"
#include <vector>

#include "SDL3/SDL_log.h"
#include "SDL3/SDL_render.h"

using namespace std::chrono;

constexpr int WINDOW_WIDTH = 800;
constexpr int WINDOW_HEIGHT = 600;
constexpr int NUM_BUNNIES = 50000;

void logError(const char* errorText) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "%s: %s", errorText, SDL_GetError());
}

constexpr float NANOS_IN_MILLIS = 1000000.0;
float getMillisElapsed(const time_point<steady_clock>& a, const time_point<steady_clock>& b) {
    return static_cast<float>(duration_cast<nanoseconds>(a - b).count()) / NANOS_IN_MILLIS;
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
        "SDL3 Renderer Bunnymark",
        WINDOW_WIDTH,
        WINDOW_HEIGHT,
        0
    );
    if (!window) {
        logError("Failed to initialize window");
        SDL_Quit();
        return 1;
    }

    // Create the renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, "vulkan");
    if (!renderer) {
        logError("Failed to initialize renderer");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Load the bunny image as an SDL_Surface
    SDL_Surface* bunnySurface = SDL_LoadPNG("../bunny.png");
    if (!bunnySurface) {
        logError("Failed to load image bunny.png");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Convert the bunny surface to a texture
    SDL_Texture* bunnyTexture = SDL_CreateTextureFromSurface(renderer, bunnySurface);
    SDL_DestroySurface(bunnySurface);
    if (!bunnyTexture) {
        logError("Failed to create texture from surface");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Get the dimensions of the bunny for later
    const int w = bunnyTexture->w;
    const int h = bunnyTexture->h;
    const int hw = w / 2;
    const int hh = h / 2;

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

    struct Vertex {
        float x, y;
        float u, v;
    };

    Vertex vertices[NUM_BUNNIES * 6];
    constexpr SDL_FColor vertexColor{1, 1, 1, 1};

    //
    // Start the game loop
    //

    auto lastTick = steady_clock::now();
    auto lastFpsMeasurement = steady_clock::now();
    float dt = 0;
    uint32_t framesInLastSecond = 0;

    bool running = true;
    SDL_Event event;

    SDL_SetRenderDrawColor(renderer, 0, 128, 255, 255);

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

        SDL_RenderClear(renderer);

        int vIdx = -1;

        // Update the bunnies
        for (auto&[x, y, vx, vy] : bunnies) {
            x += vx * dt;
            y += vy * dt;

            if (x < 0 || x > WINDOW_WIDTH - 32) vx *= -1;
            if (y < 0 || y > WINDOW_HEIGHT - 32) vy *= -1;

            // Uncomment to use SDL's built-in RenderTexture function (slower)
            // SDL_FRect rect{x - hw, y - hh, static_cast<float>(w), static_cast<float>(h)};
            // SDL_RenderTexture(renderer, bunnyTexture, nullptr, &rect);

            vertices[++vIdx] = {x - hw, y - hh, 0, 0};
            vertices[++vIdx] = {x - hw, y + hh, 0, 1};
            vertices[++vIdx] = {x + hw, y - hh, 1, 0};
            vertices[++vIdx] = {x + hw, y - hh, 1, 0};
            vertices[++vIdx] = {x - hw, y + hh, 0, 1};
            vertices[++vIdx] = {x + hw, y + hh, 1, 1};
        }

        SDL_RenderGeometryRaw(
            renderer,
            bunnyTexture,
            &vertices[0].x,
            sizeof(float) * 4,
            &vertexColor,
            0,
            &vertices[0].u,
            sizeof(float) * 4,
            NUM_BUNNIES * 6,
            nullptr,
            0,
            4
        );

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyTexture(bunnyTexture);
    SDL_Quit();
    return 0;
}
