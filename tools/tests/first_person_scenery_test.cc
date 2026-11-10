#include "first_person_scenery.h"

#include <cstdio>
#include <cstdlib>
#include <random>

using namespace fallout;

static void require(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "Scenery regression: %s\n", message);
        std::exit(1);
    }
}

static bool close(double a, double b)
{
    return std::abs(a - b) < 1.0e-7;
}

// Independent reference: intersect each of the four perimeter segments.
static double referenceDepth(const FirstPersonScenerySlab& slab,
    double cameraX, double cameraY, double rayX, double rayY)
{
    FirstPersonSceneryPoint corners[4];
    const int signs[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
    for (int i = 0; i < 4; i++) {
        corners[i] = { slab.center.x + signs[i][0] * slab.axis.x * slab.length / 2
                - signs[i][1] * slab.axis.y * slab.thickness / 2,
            slab.center.y + signs[i][0] * slab.axis.y * slab.length / 2
                + signs[i][1] * slab.axis.x * slab.thickness / 2 };
    }
    double nearest = std::numeric_limits<double>::infinity();
    for (int i = 0; i < 4; i++) {
        const auto& a = corners[i];
        const auto& b = corners[(i + 1) % 4];
        const double edgeX = b.x - a.x;
        const double edgeY = b.y - a.y;
        const double determinant = rayX * edgeY - rayY * edgeX;
        if (std::abs(determinant) < 1.0e-10) {
            continue;
        }
        const double dx = a.x - cameraX;
        const double dy = a.y - cameraY;
        const double depth = (dx * edgeY - dy * edgeX) / determinant;
        const double fraction = (dx * rayY - dy * rayX) / determinant;
        if (depth >= 0.1 && fraction >= 0.0 && fraction <= 1.0) {
            nearest = std::min(nearest, depth);
        }
    }
    return nearest;
}

int main()
{
    require(firstPersonSceneryIsLarge(320, 128), "large backdrop classification");
    require(!firstPersonSceneryIsLarge(64, 64), "small props remain billboards");
    require(!firstPersonSceneryIsLarge(160, 320), "tall trees remain billboards");
    require(!firstPersonSceneryIsLarge(320, 32), "ground debris remains a billboard");
    require(!firstPersonSceneryProfile(nullptr, 0, 0).fixed, "empty art");

    // Synthetic isometric wall with the (48,-12) ground slope. Padding must
    // not affect its length; a genuine opening must not be filled in.
    constexpr int width = 240;
    constexpr int height = 160;
    std::vector<unsigned char> pixels(width * height, 0);
    for (int x = 20; x < 220; x++) {
        if (x >= 110 && x < 120) {
            continue;
        }
        const int bottom = 140 - x / 4;
        for (int y = bottom - 70; y < bottom; y++) {
            pixels[y * width + x] = 42;
        }
    }
    const auto profile = firstPersonSceneryProfile(pixels.data(), width, height);
    require(profile.fixed && profile.left == 20 && profile.right == 220, "opaque bounds");
    require(profile.bottoms[115] == 0, "preserve actual transparency");
    require(std::abs(profile.slope + 0.25) < 0.001, "ground slope fit");
    const auto derived = firstPersonScenerySlab(profile, 10, 20, 0, 0);
    require(std::abs(derived.axis.y) < 0.001, "undo isometric orientation");
    require(std::abs(derived.length - 200.0 / 48.0) < 0.01, "world span from art");
    const auto shifted = firstPersonScenerySlab(profile, 10, 20, 48, -12);
    require(close(shifted.center.x - derived.center.x, 1.0)
            && close(shifted.center.y, derived.center.y),
        "honor art and object offsets");
    const auto backwallLeft = firstPersonSceneryBackwallSlab(profile, 10, 4, 0);
    const auto backwallRight = firstPersonSceneryBackwallSlab(profile, 20, 4, 48);
    require(close(backwallLeft.center.y, backwallRight.center.y)
            && close(backwallLeft.axis.x, 1.0)
            && close(backwallLeft.axis.y, 0.0)
            && close(backwallRight.axis.x, 1.0)
            && close(backwallRight.axis.y, 0.0),
        "backwall panels share one depth and horizontal axis");
    require(close(backwallRight.center.x - backwallLeft.center.x, 11.0)
            && close(backwallLeft.length, backwallRight.length),
        "backwall panels preserve horizontal placement and scale");

    // Temple facade art can be taller than the original aspect-ratio cutoff
    // despite having a clean base aligned to a map axis.
    constexpr int facadeWidth = 241;
    constexpr int facadeHeight = 300;
    std::vector<unsigned char> facadePixels(facadeWidth * facadeHeight, 0);
    for (int x = 0; x < facadeWidth; x++) {
        const int bottom = 270 - x / 4;
        for (int y = bottom - 198; y < bottom; y++) {
            facadePixels[y * facadeWidth + x] = 42;
        }
    }
    const auto facade = firstPersonSceneryProfile(
        facadePixels.data(), facadeWidth, facadeHeight);
    require(facade.fixed && !firstPersonSceneryIsLarge(facadeWidth, 198),
        "clean map-aligned facade bypasses the aspect-ratio cutoff");
    require(firstPersonSceneryHasMapAlignedBase(facade.slope, 0.3),
        "accept Fallout's first map-axis slope");
    require(firstPersonSceneryHasMapAlignedBase(0.75, 0.3),
        "accept Fallout's other map-axis slope");
    require(!firstPersonSceneryHasMapAlignedBase(0.0, 0.3),
        "reject non-map-aligned base");
    require(!firstPersonSceneryHasMapAlignedBase(-0.25, 2.1),
        "reject noisy composite base");
    std::vector<unsigned char> noisyFacadePixels(facadeWidth * facadeHeight, 0);
    for (int x = 0; x < facadeWidth; x++) {
        const int offset = (x / 8) % 2 == 0 ? -10 : 10;
        const int bottom = 270 - x / 4 + offset;
        for (int y = bottom - 198; y < bottom; y++) {
            noisyFacadePixels[y * facadeWidth + x] = 42;
        }
    }
    require(!firstPersonSceneryProfile(
                noisyFacadePixels.data(), facadeWidth, facadeHeight)
                 .fixed,
        "irregular wide art must not be forced into the slab path");

    std::fill(pixels.begin(), pixels.end(), 0);
    require(!firstPersonSceneryProfile(pixels.data(), width, height).fixed, "transparent art");
    std::vector<unsigned char> slopingPixels(width * 260, 0);
    for (int x = 20; x < 220; x++) {
        const int bottom = 80 + x * 3 / 4;
        for (int y = bottom - 70; y < bottom; y++) {
            slopingPixels[y * width + x] = 42;
        }
    }
    const auto sloping = firstPersonSceneryProfile(slopingPixels.data(), width, 260);
    require(sloping.fixed, "down-screen wall must not be mistaken for tall foliage");
    const auto downScreen = firstPersonScenerySlab(sloping, 10, 20, 0, 0);
    require(std::abs(downScreen.axis.x) < 0.001, "opposite isometric ground direction");
    require(std::abs(downScreen.length - 200.0 / 32.0) < 0.01, "down-screen wall length");

    const FirstPersonScenerySlab slab { { 0, 0 }, { 1, 0 }, 10, 0.2 };
    FirstPersonSceneryHit hit;
    require(firstPersonSceneryIntersect(slab, 0, -3, 0, 1, hit)
            && close(hit.depth, 2.9) && close(hit.u, 0.5),
        "front surface depth");
    require(firstPersonSceneryIntersect(slab, 0, 3, 0, -1, hit)
            && close(hit.depth, 2.9),
        "visible from the reverse side");
    require(firstPersonSceneryIntersect(slab, -8, 0, 1, 0, hit)
            && close(hit.depth, 3) && hit.endFace,
        "visible end-on with parallel rays");
    require(!firstPersonSceneryIntersect(slab, -8, 0.3, 1, 0, hit), "parallel ray outside slab");
    require(firstPersonSceneryIntersect(slab, 0, 0, 1, 0, hit)
            && close(hit.depth, 5),
        "camera inside uses exit face");
    require(firstPersonSceneryIntersect(slab, 0, -0.15, 0, 1, hit)
            && close(hit.depth, 0.25),
        "near-plane crossing retains far face");
    // Center is behind the camera, but the right end is visible ahead.
    require(firstPersonSceneryIntersect(slab, 1, -1, 1, 0.5, hit), "anchor behind camera");

    // Compare depth and conservative screen culling to the independent edge
    // reference over many camera positions, rotations and viewport columns.
    std::mt19937 random(711);
    std::uniform_real_distribution<double> position(-8, 8);
    std::uniform_real_distribution<double> angle(-3.141592653589793, 3.141592653589793);
    constexpr int screenWidth = 160;
    for (int trial = 0; trial < 1000; trial++) {
        const double orientation = angle(random);
        const FirstPersonScenerySlab rotated { { 0, 0 },
            { std::cos(orientation), std::sin(orientation) }, 10, 0.2 };
        const double cameraX = position(random);
        const double cameraY = position(random);
        const double yaw = angle(random);
        const double directionX = std::cos(yaw);
        const double directionY = std::sin(yaw);
        const double planeX = -directionY * 0.66;
        const double planeY = directionX * 0.66;
        const auto columns = firstPersonSceneryColumns(rotated, cameraX, cameraY,
            directionX, directionY, planeX, planeY, screenWidth);
        for (int x = 0; x < screenWidth; x++) {
            const double cameraColumn = 2.0 * x / screenWidth - 1.0;
            const double rayX = directionX + cameraColumn * planeX;
            const double rayY = directionY + cameraColumn * planeY;
            const double expected = referenceDepth(rotated, cameraX, cameraY, rayX, rayY);
            const bool intersects = firstPersonSceneryIntersect(rotated, cameraX, cameraY, rayX, rayY, hit);
            require(intersects == std::isfinite(expected), "ray/edge visibility comparison");
            if (intersects) {
                require(close(hit.depth, expected), "per-column depth comparison");
                require(hit.u >= 0 && hit.u <= 1, "valid texture coordinate");
                require(x >= columns.first && x < columns.end, "visible end must survive culling");
            }
        }
    }
    std::puts("Large scenery regression tests passed (160,000 reference rays).");
}
