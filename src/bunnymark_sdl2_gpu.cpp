#include <chrono>
#include <ctime>
#include <iostream>
#include <ostream>
#include <random>

#include <SDL2/SDL.h>
#include <SDL_gpu.h>

#include <vector>

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
    // Initial SDL_gpu setup
    GPU_SetPreInitFlags(GPU_INIT_DISABLE_VSYNC);
    GPU_Target* screen = GPU_Init(WINDOW_WIDTH, WINDOW_HEIGHT, GPU_DEFAULT_INIT_FLAGS);
    if (!screen) {
        logError("Failed to initialize SDL_gpu");
        GPU_Quit();
        return 1;
    }

    // Set the window title
    SDL_Window* window = SDL_GetWindowFromID(screen->context->windowID);
    if (!window) {
        logError("Failed to get window");
        GPU_Quit();
        return 1;
    }
    SDL_SetWindowTitle(window, "SDL_gpu Bunnymark");

    // Load the bunny image
    GPU_Image* bunnyTexture = GPU_LoadImage("../bunny.png");

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
            if (event.type == SDL_QUIT) {
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

        GPU_ClearColor(screen, SDL_Color{128, 128, 255});

        // Update the bunnies
        for (auto&[x, y, vx, vy] : bunnies) {
            x += vx * dt;
            y += vy * dt;

            if (x < 0 || x > WINDOW_WIDTH - 32) vx *= -1;
            if (y < 0 || y > WINDOW_HEIGHT - 32) vy *= -1;

            GPU_Blit(bunnyTexture, nullptr, screen, x, y);
        }

        GPU_Flip(screen);
    }

    GPU_FreeImage(bunnyTexture);
    GPU_Quit();
    return 0;
}
