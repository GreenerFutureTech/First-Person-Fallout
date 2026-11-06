#ifndef FIRST_PERSON_SKY_H
#define FIRST_PERSON_SKY_H

namespace fallout {

enum class FirstPersonSkyType {
    Desert,
    Cave,
};

// Draws above the camera horizon in the indexed framebuffer. Yaw is in
// degrees; fieldOfViewScale is the camera plane length used by the world renderer.
void firstPersonSkyRender(unsigned char* buffer, int width, int height, int pitch,
    int horizonY, double yaw, double fieldOfViewScale, int ambientIntensityIndex,
    FirstPersonSkyType skyType);

} // namespace fallout

#endif /* FIRST_PERSON_SKY_H */
