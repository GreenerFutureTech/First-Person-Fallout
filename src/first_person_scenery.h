#ifndef FALLOUT_FIRST_PERSON_SCENERY_H_
#define FALLOUT_FIRST_PERSON_SCENERY_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace fallout {

// A large isometric backdrop represents a span of the map, not an actor at
// one point. Fit its ground edge once, then undo the isometric projection.
// Keep actual transparent pixels: fences and broken structures have openings.
struct FirstPersonSceneryProfile {
    int width = 0;
    int height = 0;
    int left = 0;
    int right = 0; // exclusive
    double slope = 0.0;
    double intercept = 0.0;
    double baseRmsError = std::numeric_limits<double>::infinity();
    bool fixed = false;
    std::vector<int> bottoms;
    std::array<int, 2> endColumns { -1, -1 };
};

inline bool firstPersonSceneryIsLarge(int width, int columnHeight)
{
    return width >= 160 && columnHeight >= 64 && width >= columnHeight * 1.5;
}

inline bool firstPersonSceneryHasMapAlignedBase(double slope, double rmsError)
{
    constexpr double kSlopeTolerance = 0.08;
    constexpr double kMaxRmsError = 2.0;
    return rmsError <= kMaxRmsError
        && (std::abs(slope + 0.25) <= kSlopeTolerance
            || std::abs(slope - 0.75) <= kSlopeTolerance);
}

inline FirstPersonSceneryProfile firstPersonSceneryProfile(
    const unsigned char* pixels, int width, int height)
{
    FirstPersonSceneryProfile result;
    result.width = width;
    result.height = height;
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return result;
    }
    result.left = width;
    double count = 0.0;
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    int endHeights[2] = { 0, 0 };
    result.bottoms.assign(width, 0);
    for (int x = 0; x < width; x++) {
        int columnTop = height;
        int columnBottom = 0;
        for (int y = 0; y < height; y++) {
            if (pixels[y * width + x] != 0) {
                columnTop = std::min(columnTop, y);
                columnBottom = y + 1;
            }
        }
        result.bottoms[x] = columnBottom;
        if (columnBottom == 0) {
            continue;
        }
        result.left = std::min(result.left, x);
        result.right = x + 1;
        const int end = x < width / 2 ? 0 : 1;
        if (columnBottom - columnTop > endHeights[end]) {
            endHeights[end] = columnBottom - columnTop;
            result.endColumns[end] = x;
        }
        count += 1.0;
        sumX += x;
        sumY += columnBottom;
        sumXX += static_cast<double>(x) * x;
        sumXY += static_cast<double>(x) * columnBottom;
    }
    if (count == 0.0) {
        return result;
    }
    const double variance = count * sumXX - sumX * sumX;
    result.slope = variance > 0.0 ? (count * sumXY - sumX * sumY) / variance : 0.0;
    result.intercept = (sumY - result.slope * sumX) / count;
    double squaredError = 0.0;
    for (int x = result.left; x < result.right; x++) {
        if (result.bottoms[x] == 0) {
            continue;
        }
        const double error = result.bottoms[x] - (result.slope * x + result.intercept);
        squaredError += error * error;
    }
    const double rmsError = std::sqrt(squaredError / count);
    result.baseRmsError = rmsError;
    const int columnHeight = std::max(endHeights[0], endHeights[1]);
    // Measure vertical columns, not the bounding-box height: an isometric
    // wall running down-screen adds its ground slope to the latter. Some tall
    // facades fail the aspect-ratio test, so accept only clean map-axis bases.
    const bool largeMapAlignedFacade = result.right - result.left >= 160
        && columnHeight >= 64
        && firstPersonSceneryHasMapAlignedBase(result.slope, rmsError);
    result.fixed = firstPersonSceneryIsLarge(result.right - result.left, columnHeight)
        || largeMapAlignedFacade;
    if (!result.fixed) {
        return result;
    }

    for (int end = 0; end < 2; end++) {
        if (result.endColumns[end] < 0) {
            result.endColumns[end] = result.endColumns[1 - end];
        }
    }
    return result;
}

enum class FirstPersonSceneryMapEdge {
    None,
    MinX,
    MaxX,
    MinY,
    MaxY,
};

inline FirstPersonSceneryMapEdge firstPersonSceneryMapEdge(
    double worldX, double worldY, double mapMinX, double mapMaxX,
    double mapMinY, double mapMaxY, double maximumDistance,
    double baseSlope, double baseRmsError)
{
    if (!firstPersonSceneryHasMapAlignedBase(baseSlope, baseRmsError)) {
        return FirstPersonSceneryMapEdge::None;
    }

    if (std::abs(baseSlope + 0.25) <= 0.08) {
        const double minDistance = worldY - mapMinY;
        const double maxDistance = mapMaxY - worldY;
        if (minDistance <= maximumDistance && minDistance <= maxDistance) {
            return FirstPersonSceneryMapEdge::MinY;
        }
        if (maxDistance <= maximumDistance) {
            return FirstPersonSceneryMapEdge::MaxY;
        }
    } else if (std::abs(baseSlope - 0.75) <= 0.08) {
        const double minDistance = worldX - mapMinX;
        const double maxDistance = mapMaxX - worldX;
        if (minDistance <= maximumDistance && minDistance <= maxDistance) {
            return FirstPersonSceneryMapEdge::MinX;
        }
        if (maxDistance <= maximumDistance) {
            return FirstPersonSceneryMapEdge::MaxX;
        }
    }

    return FirstPersonSceneryMapEdge::None;
}

struct FirstPersonSceneryPoint {
    double x;
    double y;
};

inline FirstPersonSceneryPoint firstPersonSceneryGroundOffset(double x, double y)
{
    // tileToScreenXY: one world unit projects to (48,-12) or (32,24).
    return { x / 64.0 - y / 48.0, x / 128.0 + y / 32.0 };
}

struct FirstPersonScenerySlab {
    FirstPersonSceneryPoint center;
    FirstPersonSceneryPoint axis;
    double length;
    double thickness = 0.2;
    double verticalOffset = 0.0;
};

inline double firstPersonSceneryBackwallProjectedX(double worldX, double worldY)
{
    return worldX * 48.0 + worldY * 32.0;
}

inline double firstPersonSceneryBackwallProjectedY(double worldX, double worldY)
{
    return -worldX * 12.0 + worldY * 24.0;
}

inline double firstPersonSceneryBackwallVerticalOffset(
    double backwallBaseY, double projectedY)
{
    return (backwallBaseY - projectedY) / 48.0;
}

inline double firstPersonSceneryBackwallDepth(double entranceY)
{
    constexpr double kDepthBeforeEntrance = 7.5;
    return entranceY - kDepthBeforeEntrance;
}

inline bool firstPersonSceneryIsTempleBackwallProto(int protoId)
{
    switch (protoId) {
    case 1360: // TEMPLE01.FRM
    case 1361: // TEMPLE02.FRM
    case 1362: // TEMPLE03.FRM
    case 1363: // TEMPLE04.FRM
    case 1364: // TEMPLE05.FRM
    case 1365: // TEMPLE06.FRM
    case 1369: // temple07.frm
    case 1370: // temple08.frm
        return true;
    default:
        return false;
    }
}

inline int firstPersonSceneryBackwallBaseline(
    const FirstPersonSceneryProfile& profile, int sourceX)
{
    if (!profile.fixed) {
        return profile.bottoms[sourceX];
    }
    return std::clamp(static_cast<int>(std::lround(
                          profile.slope * sourceX + profile.intercept)),
        0, profile.height);
}

inline bool firstPersonSceneryShouldReplaceDepth(
    double incomingDepth, double existingDepth, bool allowEqualDepth)
{
    return incomingDepth < existingDepth
        || (allowEqualDepth && incomingDepth == existingDepth);
}

inline FirstPersonScenerySlab firstPersonScenerySlab(
    const FirstPersonSceneryProfile& profile, double worldX, double worldY,
    int offsetX, int offsetY)
{
    const double middle = (profile.left + profile.right) * 0.5;
    const auto center = firstPersonSceneryGroundOffset(
        middle - profile.width / 2 + offsetX,
        profile.intercept + profile.slope * middle - profile.height + offsetY);
    const auto span = firstPersonSceneryGroundOffset(
        profile.right - profile.left, profile.slope * (profile.right - profile.left));
    const double length = std::hypot(span.x, span.y);
    return { { worldX + center.x, worldY + center.y },
        { span.x / length, span.y / length }, length };
}

inline FirstPersonScenerySlab firstPersonSceneryBackwallSlab(
    const FirstPersonSceneryProfile& profile, double projectedScreenX, double backwallY,
    int offsetX, double verticalOffset)
{
    const double middle = (profile.left + profile.right) * 0.5;
    return {
        { (projectedScreenX - backwallY * 32.0) / 48.0
                + (middle - profile.width * 0.5 + offsetX) / 48.0,
            backwallY },
        { 1.0, 0.0 },
        (profile.right - profile.left) / 48.0,
        0.2,
        verticalOffset,
    };
}

inline FirstPersonScenerySlab firstPersonSceneryMapEdgeSlab(
    const FirstPersonSceneryProfile& profile, FirstPersonSceneryMapEdge edge,
    double projectedIsoX, double projectedIsoY, double edgeCoordinate,
    int offsetX, double verticalOffset)
{
    if (edge == FirstPersonSceneryMapEdge::MinY
        || edge == FirstPersonSceneryMapEdge::MaxY) {
        return firstPersonSceneryBackwallSlab(
            profile, projectedIsoX, edgeCoordinate, offsetX, verticalOffset);
    }

    const double middle = (profile.left + profile.right) * 0.5;
    return {
        { edgeCoordinate,
            (projectedIsoY + edgeCoordinate * 12.0) / 24.0
                + (middle - profile.width * 0.5 + offsetX) / 32.0 },
        { 0.0, 1.0 },
        (profile.right - profile.left) / 32.0,
        0.2,
        verticalOffset,
    };
}

struct FirstPersonSceneryHit {
    double depth;
    double u;
    bool endFace;
};

inline bool firstPersonSceneryIntersect(const FirstPersonScenerySlab& slab,
    double cameraX, double cameraY, double rayX, double rayY, FirstPersonSceneryHit& hit)
{
    const double dx = cameraX - slab.center.x;
    const double dy = cameraY - slab.center.y;
    const double origin[] = { dx * slab.axis.x + dy * slab.axis.y,
        -dx * slab.axis.y + dy * slab.axis.x };
    const double direction[] = { rayX * slab.axis.x + rayY * slab.axis.y,
        -rayX * slab.axis.y + rayY * slab.axis.x };
    const double halfExtent[] = { slab.length * 0.5, slab.thickness * 0.5 };
    double entry = -std::numeric_limits<double>::infinity();
    double exit = std::numeric_limits<double>::infinity();
    for (int axis = 0; axis < 2; axis++) {
        if (std::abs(direction[axis]) < 1.0e-10) {
            if (std::abs(origin[axis]) > halfExtent[axis]) {
                return false;
            }
            continue;
        }
        double nearDistance = (-halfExtent[axis] - origin[axis]) / direction[axis];
        double farDistance = (halfExtent[axis] - origin[axis]) / direction[axis];
        if (nearDistance > farDistance) {
            std::swap(nearDistance, farDistance);
        }
        entry = std::max(entry, nearDistance);
        exit = std::min(exit, farDistance);
    }
    if (entry > exit || exit < 0.1) {
        return false;
    }
    // If the camera/near plane enters the slab, retain its exit surface.
    hit.depth = entry >= 0.1 ? entry : exit;
    const double along = origin[0] + hit.depth * direction[0];
    hit.u = std::clamp(along / slab.length + 0.5, 0.0, 1.0);
    hit.endFace = std::abs(std::abs(along) - halfExtent[0]) < 1.0e-8;
    return std::isfinite(hit.depth);
}

struct FirstPersonSceneryColumns {
    int first;
    int end;
};

inline FirstPersonSceneryColumns firstPersonSceneryColumns(const FirstPersonScenerySlab& slab,
    double cameraX, double cameraY, double directionX, double directionY,
    double planeX, double planeY, int screenWidth)
{
    const double inverseDeterminant = 1.0 / (planeX * directionY - directionX * planeY);
    double minDepth = std::numeric_limits<double>::infinity();
    double maxDepth = -std::numeric_limits<double>::infinity();
    double left = screenWidth;
    double right = 0.0;
    for (int along : { -1, 1 }) {
        for (int across : { -1, 1 }) {
            const double x = slab.center.x - cameraX + along * slab.axis.x * slab.length * 0.5
                - across * slab.axis.y * slab.thickness * 0.5;
            const double y = slab.center.y - cameraY + along * slab.axis.y * slab.length * 0.5
                + across * slab.axis.x * slab.thickness * 0.5;
            const double depth = inverseDeterminant * (-planeY * x + planeX * y);
            minDepth = std::min(minDepth, depth);
            maxDepth = std::max(maxDepth, depth);
            if (depth >= 0.1) {
                const double lateral = inverseDeterminant * (directionY * x - directionX * y);
                const double screenX = screenWidth * 0.5 * (1.0 + lateral / depth);
                left = std::min(left, screenX);
                right = std::max(right, screenX);
            }
        }
    }
    if (maxDepth < 0.1) {
        return { 0, 0 };
    }
    // A near-plane crossing can project beyond either edge. Let ray tests
    // clip it, rather than rejecting it based on its center or one endpoint.
    if (minDepth < 0.1) {
        return { 0, screenWidth };
    }
    return { static_cast<int>(std::floor(std::clamp(left, 0.0, static_cast<double>(screenWidth)))),
        static_cast<int>(std::min(static_cast<double>(screenWidth), std::ceil(std::max(0.0, right)) + 1.0)) };
}

} // namespace fallout

#endif
