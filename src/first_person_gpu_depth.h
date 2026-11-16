#ifndef FALLOUT_FIRST_PERSON_GPU_DEPTH_H_
#define FALLOUT_FIRST_PERSON_GPU_DEPTH_H_

#include <SDL.h>

namespace fallout {

// An isolated OpenGL 2.1 pass sharing SDL's context and cached sprite textures.
// Call destroy before SDL destroys/recreates its renderer/context.
bool firstPersonGpuDepthInit(SDL_Renderer* renderer);
void firstPersonGpuDepthDestroy();
struct FirstPersonGpuDepthUploadStats {
    double packMs = 0.0;
    double uploadMs = 0.0;
    Uint64 bytes = 0;
};
bool firstPersonGpuDepthUpload(SDL_Renderer* renderer, int width, int height,
    int horizon, const double* pixels, const double* walls, const double* sills,
    FirstPersonGpuDepthUploadStats* stats = nullptr);
void firstPersonGpuDepthBegin(SDL_Renderer* renderer);
// policy: 0 = existing clipped sprite; 1 = pixel depths; 2 = pixels + wall/sill.
void firstPersonGpuDepthDraw(SDL_Texture* texture, const SDL_Rect& destination,
    const SDL_Rect& clip, double depth, int policy);
unsigned int firstPersonGpuDepthEnd();

} // namespace fallout

#endif
