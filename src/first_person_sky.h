#ifndef FIRST_PERSON_SKY_H
#define FIRST_PERSON_SKY_H

namespace fallout {

// Draws above the midpoint of the indexed framebuffer. Yaw is in degrees;
// fieldOfViewScale is the camera plane length used by the world renderer.
void firstPersonSkyRender(unsigned char* buffer, int width, int height, int pitch,
    double yaw, double fieldOfViewScale);

} // namespace fallout

#endif /* FIRST_PERSON_SKY_H */
