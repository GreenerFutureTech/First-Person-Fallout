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
    const auto backwallLeft = firstPersonSceneryBackwallSlab(profile, 480, 4, 0, 0);
    const auto backwallRight = firstPersonSceneryBackwallSlab(profile, 960, 4, 48, 1.5);
    require(close(backwallLeft.center.y, backwallRight.center.y)
            && close(backwallLeft.axis.x, 1.0)
            && close(backwallLeft.axis.y, 0.0)
            && close(backwallRight.axis.x, 1.0)
            && close(backwallRight.axis.y, 0.0),
        "backwall panels share one depth and horizontal axis");
    require(close(backwallRight.center.x - backwallLeft.center.x, 11.0)
            && close(backwallLeft.center.x * 48.0 + backwallLeft.center.y * 32.0, 480.0)
            && close(backwallLeft.length, backwallRight.length)
            && close(backwallLeft.verticalOffset, 0.0)
            && close(backwallRight.verticalOffset, 1.5),
        "backwall panels preserve projected X at shared depth, height, and scale");
    const auto nearEdge = firstPersonSceneryMapEdgeSlab(
        profile, FirstPersonSceneryMapEdge::MinY, 480, 120, 0.5, 0, 0.0);
    const auto sideEdge = firstPersonSceneryMapEdgeSlab(
        profile, FirstPersonSceneryMapEdge::MinX, 480, 120, 0.5, 0, 0.0);
    require(close(nearEdge.center.y, 0.5) && close(nearEdge.axis.x, 1.0)
            && close(nearEdge.axis.y, 0.0)
            && close(nearEdge.length, 200.0 / 48.0),
        "north and south map edges align along world X");
    require(close(sideEdge.center.x, 0.5) && close(sideEdge.axis.x, 0.0)
            && close(sideEdge.axis.y, 1.0)
            && close(sideEdge.center.y * 24.0 - sideEdge.center.x * 12.0, 120.0)
            && close(sideEdge.length, 200.0 / 32.0),
        "west and east map edges align along world Y");
    const double upperPanelY = firstPersonSceneryBackwallProjectedY(54.25, 32.75);
    const double lowerPanelY = firstPersonSceneryBackwallProjectedY(54.25, 41.25);
    require(upperPanelY < lowerPanelY
            && close(firstPersonSceneryBackwallVerticalOffset(lowerPanelY, upperPanelY),
                (lowerPanelY - upperPanelY) / 48.0)
            && firstPersonSceneryBackwallVerticalOffset(lowerPanelY, upperPanelY) > 0.0,
        "panel vertical spacing uses the horizontal scale and preserves ordering");
    const double xAxisRow = firstPersonSceneryBackwallRowCoordinate(480, 120, -0.25);
    require(close(firstPersonSceneryBackwallRowCoordinate(528, 108, -0.25), xAxisRow)
            && close(firstPersonSceneryBackwallRowCoordinate(512, 144, -0.25)
                    - xAxisRow,
                32.0),
        "backwall row coordinate removes along-wall isometric slope");
    const double yAxisRow = firstPersonSceneryBackwallRowCoordinate(480, 120, 0.75);
    require(close(firstPersonSceneryBackwallRowCoordinate(512, 144, 0.75), yAxisRow)
            && close(firstPersonSceneryBackwallRowCoordinate(528, 108, 0.75)
                    - yAxisRow,
                -48.0),
        "backwall row coordinate handles the other map axis");
    require(close(firstPersonSceneryBackwallDepth(46.25), 38.75),
        "Temple backwall moves one map row toward the facade");
    require(close(firstPersonSceneryTemplePanelVerticalOffset(1363, 1.25), 0.25)
            && close(firstPersonSceneryTemplePanelVerticalOffset(1364, 1.25), 0.25)
            && close(firstPersonSceneryTemplePanelVerticalOffset(1362, 1.25), 1.25),
        "Temple05 and the texture to its right share the downward tier adjustment");
    require(firstPersonSceneryFloorOcclusionTop(2.0, 100, 600) == 250
            && firstPersonSceneryFloorOcclusionTop(1000.0, 100, 600) == 101
            && firstPersonSceneryFloorOcclusionTop(0.5, 100, 600) == 600,
        "Temple backwall is clipped where the projected floor becomes nearer");
    auto unevenProfile = profile;
    unevenProfile.bottoms[50] = 160;
    require(firstPersonSceneryBackwallBaseline(unevenProfile, 50) == 128
            && firstPersonSceneryBackwallBaseline(unevenProfile, 115) == 112,
        "clean backwall profiles follow the fitted base instead of isolated low pixels");
    unevenProfile.fixed = true;
    unevenProfile.baseRmsError = 3.0;
    require(firstPersonSceneryBackwallBaseline(unevenProfile, 50) == 160,
        "large irregular backwall profiles retain their actual lower edge");
    require(!firstPersonSceneryShouldReplaceDepth(2.0, 2.0, false)
            && firstPersonSceneryShouldReplaceDepth(2.0, 2.0, true)
            && firstPersonSceneryShouldReplaceDepth(1.0, 2.0, false)
            && !firstPersonSceneryShouldReplaceDepth(3.0, 2.0, true),
        "equal-depth backwall panels composite in both renderers");
    for (int protoId : { 1359, 1360, 1361, 1362, 1363, 1364, 1368, 1369 }) {
        require(firstPersonSceneryIsTempleBackwallProto(protoId),
            "placed Temple facade object is grouped into the backwall");
    }
    for (int protoId : { 937, 938, 939 }) {
        require(firstPersonSceneryIsArroyoVillageMonumentProto(protoId),
            "all Arroyo Village monument pieces are enlarged");
    }
    require(!firstPersonSceneryIsArroyoVillageMonumentProto(936)
            && !firstPersonSceneryIsArroyoVillageMonumentProto(940),
        "unrelated neighboring scenery is not enlarged");
    for (int protoId : { 1365, 1366, 1367, 1370, 1371 }) {
        require(!firstPersonSceneryIsTempleBackwallProto(protoId),
            "fire pits and unplaced scenery are not backwall panels");
    }
    require(firstPersonSceneryIsTempleBackwallSupplementalProto(1359)
            && firstPersonSceneryIsTempleBackwallSupplementalProto(1368)
            && !firstPersonSceneryIsTempleBackwallSupplementalProto(1369),
        "newly recovered Temple panels keep separate map-derived placement");
    require(firstPersonSceneryIsTempleBackwallProto(1369)
            && firstPersonSceneryIsTempleBackwallSuppressedProto(1369)
            && !firstPersonSceneryIsTempleBackwallSuppressedProto(1368),
        "Temple08 stays as a layout anchor while its pixels are hidden");
    require(firstPersonSceneryTempleBackwallReplacementFrame(1369) == 1382
            && firstPersonSceneryTempleBackwallReplacementFrame(1370) == -1,
        "placed Temple08 uses Temple07 artwork only for its F11 replacement");

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
    require(firstPersonSceneryMapEdge(5.0, 0.5, 0.5, 9.5, 0.5, 9.5,
                1.0, -0.25, 0.3) == FirstPersonSceneryMapEdge::MinY,
        "classify aligned scenery at the near map edge");
    require(firstPersonSceneryMapEdge(5.0, 9.0, 0.5, 9.5, 0.5, 9.5,
                1.0, -0.25, 0.3) == FirstPersonSceneryMapEdge::MaxY,
        "classify aligned scenery at the far map edge");
    require(firstPersonSceneryMapEdge(0.75, 5.0, 0.5, 9.5, 0.5, 9.5,
                1.0, 0.75, 0.3) == FirstPersonSceneryMapEdge::MinX,
        "classify the other map-axis edge");
    require(firstPersonSceneryMapEdge(5.0, 5.0, 0.5, 9.5, 0.5, 9.5,
                1.0, -0.25, 0.3) == FirstPersonSceneryMapEdge::None,
        "leave scenery away from map edges unchanged");
    require(firstPersonSceneryMapEdge(5.0, 0.5, 0.5, 9.5, 0.5, 9.5,
                1.0, 0.0, 0.3) == FirstPersonSceneryMapEdge::None,
        "leave non-aligned scenery unchanged");
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
