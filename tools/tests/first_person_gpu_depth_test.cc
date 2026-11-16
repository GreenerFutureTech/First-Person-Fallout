#define SDL_MAIN_HANDLED
#include "first_person_gpu_depth.h"
#include "billboard_clip_bands.h"
#include <SDL_opengl.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

static void require(bool result, const char* message)
{
    if (!result) {
        std::fprintf(stderr, "%s: %s\n", message, SDL_GetError());
        std::exit(1);
    }
}

static void verify(SDL_Renderer* renderer, int width, int height, int scale)
{
    const double infinity = std::numeric_limits<double>::infinity();
    std::vector<double> pixels(width * height, infinity), walls(width, infinity), sills(width, infinity);
    for (int x = 0; x < width; ++x) {
        if (x >= width - 8) walls[x] = 6.0;
        if (x < 8) sills[x] = 3.0;
        for (int y = 0; y < height; ++y) {
            double depth = infinity;
            if (x % 8 < 3 && y >= 8 && y < height - 6) depth = 2.0;
            if (x >= 15 && y % 7 < 2) depth = std::min(depth, 4.0);
            if (x >= width - 8) depth = std::min(depth, 6.0);
            if (x >= 32 && x < 40 && y < 6) depth = 1.5; // Door cap.
            pixels[x * height + y] = depth;
        }
    }
    fallout::FirstPersonGpuDepthUploadStats stats;
    require(fallout::firstPersonGpuDepthUpload(renderer, width, height, height / 2,
        pixels.data(), walls.data(), sills.data(), &stats), "upload depths");
    require(stats.bytes == (static_cast<Uint64>(width) * height + width * 2) * sizeof(float), "initial transfer size");
    require(fallout::firstPersonGpuDepthUpload(renderer, width, height, height / 2,
        pixels.data(), walls.data(), sills.data(), &stats) && stats.bytes == 0, "unchanged depths skip upload");
    sills[0] = 2.5;
    require(fallout::firstPersonGpuDepthUpload(renderer, width, height, height / 2,
        pixels.data(), walls.data(), sills.data(), &stats) && stats.bytes == width * 2 * sizeof(float), "column-only update");
    sills[0] = 3.0;
    require(fallout::firstPersonGpuDepthUpload(renderer, width, height, height / 2,
        pixels.data(), walls.data(), sills.data()), "restore column");
    SDL_Texture* sprite = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, 2, 2);
    require(sprite != nullptr, "create sprite");
    const Uint32 art[] = { 0xFFFF0000, 0, 0xFF0000FF, 0xFFFFFFFF };
    require(SDL_UpdateTexture(sprite, nullptr, art, 2 * sizeof(Uint32)) == 0, "upload sprite");
    SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(sprite, SDL_ScaleModeNearest);
    SDL_Texture* base = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STATIC, 1, 1);
    Uint32 gray = 0xFF202020;
    require(SDL_UpdateTexture(base, nullptr, &gray, sizeof(gray)) == 0, "upload base");
    const SDL_Rect destination { -4, -2, width + 8, height + 4 };
    const SDL_Rect clip { 0, 0, width, height };
    const SDL_Rect ui { 1, 1, 5, 5 };
    std::vector<Uint32> actual(width * height * scale * scale);
    for (int policy : { 0, 1, 2 }) {
        for (double depth : { 1.0, 2.0, 3.0, 5.0, 7.0 }) {
            SDL_RenderSetClipRect(renderer, nullptr);
            SDL_RenderCopy(renderer, base, nullptr, nullptr);
            // Nondefault scissor state must be restored for the SDL UI pass.
            SDL_RenderSetClipRect(renderer, &ui);
            SDL_RenderCopy(renderer, base, nullptr, &ui);
            fallout::firstPersonGpuDepthBegin(renderer);
            const SDL_Rect leftClip { 0, 0, width / 2, height };
            const SDL_Rect rightClip { width / 2, 0, width - width / 2, height };
            fallout::firstPersonGpuDepthDraw(sprite, destination, leftClip, depth, policy);
            fallout::firstPersonGpuDepthDraw(sprite, destination, rightClip, depth, policy);
            require(fallout::firstPersonGpuDepthEnd() == 1, "adjacent clips batch together");
            // Reusing the same SDL texture after the custom pass exercises
            // both SDL's texture cache and restoration of its shader program.
            SDL_RenderCopy(renderer, sprite, nullptr, &clip);
            SDL_RenderSetClipRect(renderer, nullptr);
            require(SDL_RenderReadPixels(renderer, nullptr, SDL_PIXELFORMAT_ARGB8888,
                actual.data(), width * scale * sizeof(Uint32)) == 0, "read pixels");
            for (int y = 0; y < height * scale; ++y) {
                for (int x = 0; x < width * scale; ++x) {
                    const double sx = (x + 0.5) / scale;
                    const double sy = (y + 0.5) / scale;
                    const int lx = static_cast<int>(sx), ly = static_cast<int>(sy);
                    double wall = pixels[lx * height + ly];
                    if (policy == 2) wall = std::min(wall,
                        ly < height / 2 ? walls[lx] : std::min(walls[lx], sills[lx]));
                    const int tx = static_cast<int>((sx - destination.x) * 2 / destination.w);
                    const int ty = static_cast<int>((sy - destination.y) * 2 / destination.h);
                    Uint32 expected = gray;
                    if ((policy == 0 || depth < wall) && art[ty * 2 + tx] != 0) expected = art[ty * 2 + tx];
                    if (lx >= ui.x && lx < ui.x + ui.w && ly >= ui.y && ly < ui.y + ui.h) expected = art[0];
                    if ((actual[y * width * scale + x] & 0xFFFFFF) != (expected & 0xFFFFFF)) {
                        std::fprintf(stderr, "pixel mismatch policy=%d depth=%.0f scale=%d x=%d y=%d actual=%08x expected=%08x\n",
                            policy, depth, scale, x, y, actual[y * width * scale + x], expected);
                        std::exit(1);
                    }
                }
            }
        }
    }
    SDL_DestroyTexture(sprite);
    SDL_DestroyTexture(base);
}

// A deliberately fragmented full-screen fence, with identical visible pixels
// for the old SDL geometry clips and the shared-depth shader. GPU completion is
// waited on only here, never in the game. Includes depth packing/upload in new.
static void benchmark(SDL_Renderer* renderer)
{
    constexpr int width = 1920, height = 1080, iterations = 30;
    std::vector<double> pixels(width * height, 1.0e30), walls(width, 1.0e30);
    for (int x = 0; x < width; ++x) {
        for (int y = 0; y < height; ++y) {
            if ((y + x / 7) % 27 < 13 && x % 19 < 15) pixels[x * height + y] = 2.0;
        }
    }
    // Build the same coalesced row clips outside the timing loop. This gives
    // the old path the benefit of free CPU clipping (a conservative comparison).
    std::vector<SDL_Rect> clips, active, next;
    std::vector<bool> retained;
    for (int y = 0; y < height; ++y) {
        next.clear();
        retained.assign(active.size(), false);
        size_t cursor = 0;
        for (int x = 0; x < width;) {
            if (pixels[x * height + y] < 5.0) { ++x; continue; }
            const int left = x++;
            while (x < width && pixels[x * height + y] > 5.0) ++x;
            SDL_Rect rect { left, y, x - left, 1 };
            const size_t previous = fallout::billboardFindPreviousRun(active, cursor, rect);
            if (previous < active.size()) {
                active[previous].h++;
                next.push_back(active[previous]);
                retained[previous] = true;
            } else next.push_back(rect);
        }
        for (size_t i = 0; i < active.size(); ++i) if (!retained[i]) clips.push_back(active[i]);
        active.swap(next);
    }
    clips.insert(clips.end(), active.begin(), active.end());
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    for (const auto& clip : clips) {
        const float x0 = static_cast<float>(clip.x), y0 = static_cast<float>(clip.y);
        const float x1 = x0 + clip.w, y1 = y0 + clip.h;
        const SDL_Color color { 255, 255, 255, 255 };
        int start = static_cast<int>(vertices.size());
        vertices.push_back({ { x0, y0 }, color, { 0, 0 } });
        vertices.push_back({ { x1, y0 }, color, { 1, 0 } });
        vertices.push_back({ { x1, y1 }, color, { 1, 1 } });
        vertices.push_back({ { x0, y1 }, color, { 0, 1 } });
        for (int index : { 0, 1, 2, 0, 2, 3 }) indices.push_back(start + index);
    }
    SDL_Texture* sprite = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
    Uint32 red = 0xFFFF0000;
    require(sprite != nullptr && SDL_UpdateTexture(sprite, nullptr, &red, sizeof(red)) == 0, "benchmark sprite");
    SDL_SetTextureBlendMode(sprite, SDL_BLENDMODE_BLEND);
    const SDL_Rect full { 0, 0, width, height };
    auto finish = reinterpret_cast<void (APIENTRY*)()>(SDL_GL_GetProcAddress("glFinish"));
    require(finish != nullptr, "benchmark completion wait");
    for (int mode : { 0, 1, 2 }) {
        double total = 0.0, pack = 0.0, upload = 0.0;
        Uint64 bytes = 0;
        for (int frame = -5; frame < iterations; ++frame) {
            Uint64 start = SDL_GetPerformanceCounter();
            fallout::FirstPersonGpuDepthUploadStats stats;
            if (mode != 0) {
                // Force the moving-camera case to transfer a changed raster.
                if (mode == 2) pixels[0] = frame % 2 == 0 ? 2.0 : 2.25;
                require(fallout::firstPersonGpuDepthUpload(renderer, width, height, height / 2,
                    pixels.data(), walls.data(), walls.data(), &stats), "benchmark depth upload");
            }
            SDL_SetRenderDrawColor(renderer, 32, 32, 32, 255);
            SDL_RenderClear(renderer);
            // Establish SDL's viewport/projection before handing off to GL.
            SDL_RenderCopy(renderer, sprite, nullptr, &full);
            if (mode != 0) {
                fallout::firstPersonGpuDepthBegin(renderer);
                fallout::firstPersonGpuDepthDraw(sprite, full, full, 5.0, 1);
                fallout::firstPersonGpuDepthEnd();
            } else {
                require(SDL_RenderGeometry(renderer, sprite, vertices.data(), static_cast<int>(vertices.size()),
                    indices.data(), static_cast<int>(indices.size())) == 0, "benchmark geometry");
            }
            SDL_RenderFlush(renderer);
            finish();
            if (frame >= 0) {
                total += 1000.0 * (SDL_GetPerformanceCounter() - start) / SDL_GetPerformanceFrequency();
                pack += stats.packMs;
                upload += stats.uploadMs;
                bytes += stats.bytes;
            }
        }
        std::printf("1920x1080 %s: %.3f ms/frame including GPU completion; depthPack %.3f ms; depthUpload %.3f ms; %.0f KB; %zu quads\n",
            mode == 0 ? "precomputed SDL clips" : mode == 1 ? "unchanged shared depth" : "changed shared depth",
            total / iterations, pack / iterations, upload / iterations,
            bytes / (1024.0 * iterations), mode ? size_t(1) : clips.size());
    }
    SDL_DestroyTexture(sprite);
}

int main(int argc, char** argv)
{
    SDL_SetMainReady();
    require(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL init");
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
    // Hidden window, in-memory pixel comparisons; no screenshot artifacts.
    for (int scale : { 1, 2 }) {
        SDL_Window* window = SDL_CreateWindow("Fence depth regression test", 0, 0,
            64 * scale, 48 * scale, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        require(window != nullptr, "hidden OpenGL window");
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        require(renderer != nullptr, "OpenGL renderer");
        require(SDL_RenderSetLogicalSize(renderer, 64, 48) == 0, "logical size");
        require(fallout::firstPersonGpuDepthInit(renderer), "depth shader init");
        verify(renderer, 64, 48, scale);
        // Resize the depth textures without changing the logical projection.
        if (scale == 1) {
            SDL_SetWindowSize(window, 80, 60);
            SDL_PumpEvents();
            SDL_RenderSetLogicalSize(renderer, 80, 60);
            verify(renderer, 80, 60, 1);
        }
        fallout::firstPersonGpuDepthDestroy();
        SDL_setenv("F2_FENCE_GPU_DEPTH", "0", 1);
        require(!fallout::firstPersonGpuDepthInit(renderer), "explicit fallback switch");
        SDL_setenv("F2_FENCE_GPU_DEPTH", "1", 1);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 32, SDL_PIXELFORMAT_ARGB8888);
    require(surface != nullptr, "software surface");
    SDL_Renderer* software = SDL_CreateSoftwareRenderer(surface);
    require(software != nullptr && !fallout::firstPersonGpuDepthInit(software), "software renderer fallback");
    SDL_DestroyRenderer(software);
    SDL_FreeSurface(surface);
    if (argc > 1 && SDL_strcmp(argv[1], "--benchmark") == 0) {
        SDL_Window* window = SDL_CreateWindow("Fence depth benchmark", 0, 0,
            1920, 1080, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        require(window != nullptr, "benchmark window");
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
        require(renderer != nullptr, "benchmark renderer");
        SDL_RenderSetLogicalSize(renderer, 1920, 1080);
        require(fallout::firstPersonGpuDepthInit(renderer), "benchmark shader init");
        benchmark(renderer);
        fallout::firstPersonGpuDepthDestroy();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    std::puts("GPU depth tests passed: fence layers, wall/sill/cap policies, alpha, foreground, equality, SDL overlays, scaling, resize, context recreation");
}
