#include "first_person_sky.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "color.h"

namespace fallout {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kPanoramaWidth = 2048;
constexpr int kPanoramaHeight = 512;

struct SkyColor {
    double r;
    double g;
    double b;
};

double smoothStep(double low, double high, double value)
{
    const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

SkyColor mix(SkyColor a, SkyColor b, double t)
{
    return { a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t };
}

double noiseValue(int x, int y, unsigned int seed)
{
    unsigned int value = static_cast<unsigned int>(x) * 0x9E3779B9u
        ^ static_cast<unsigned int>(y) * 0x85EBCA6Bu ^ seed;
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    value ^= value >> 16;
    return (value & 65535u) / 65535.0;
}

// Wrap the lattice itself, so clouds and ridges join at the panorama seam.
double noise(double x, double y, int period, unsigned int seed)
{
    const int ix = static_cast<int>(std::floor(x));
    const int iy = static_cast<int>(std::floor(y));
    const double tx = smoothStep(0.0, 1.0, x - ix);
    const double ty = smoothStep(0.0, 1.0, y - iy);
    const int x0 = (ix % period + period) % period;
    const int x1 = (x0 + 1) % period;
    const double a = noiseValue(x0, iy, seed);
    const double b = noiseValue(x1, iy, seed);
    const double c = noiseValue(x0, iy + 1, seed);
    const double d = noiseValue(x1, iy + 1, seed);
    return (a + (b - a) * tx) * (1.0 - ty) + (c + (d - c) * tx) * ty;
}

uint16_t packColor(SkyColor color, int x, int y)
{
    // World-anchored ordered dithering softens RGB555/palette banding without
    // introducing random noise that flickers whenever the camera moves.
    constexpr int kDither[4][4] = {
        { 0, 8, 2, 10 }, { 12, 4, 14, 6 },
        { 3, 11, 1, 9 }, { 15, 7, 13, 5 },
    };
    const double bias = (kDither[y & 3][x & 3] - 7.5) * 0.8;
    const int r = std::clamp(static_cast<int>(color.r + bias), 0, 255) >> 3;
    const int g = std::clamp(static_cast<int>(color.g + bias), 0, 255) >> 3;
    const int b = std::clamp(static_cast<int>(color.b + bias), 0, 255) >> 3;
    return static_cast<uint16_t>((r << 10) | (g << 5) | b);
}

std::vector<uint16_t> buildPanorama()
{
    std::vector<uint16_t> pixels(kPanoramaWidth * kPanoramaHeight);
    for (int x = 0; x < kPanoramaWidth; x++) {
        const double u = static_cast<double>(x) / kPanoramaWidth;
        const double angle = u * 2.0 * kPi;
        const double sunAngle = std::remainder(angle - 2.1, 2.0 * kPi);
        const double sunFacing = 0.5 + 0.5 * std::cos(sunAngle);
        const double farRidge = 0.022 + 0.060 * noise(u * 11.0, 0.3, 11, 17)
            + 0.022 * noise(u * 29.0, 0.6, 29, 31)
            + 0.006 * noise(u * 83.0, 0.2, 83, 53);
        const double middleRidge = 0.011 + 0.033 * noise(u * 17.0, 0.5, 17, 73)
            + 0.012 * noise(u * 43.0, 0.8, 43, 91);
        const double nearRidge = 0.004 + 0.018 * noise(u * 13.0, 0.1, 13, 113)
            + 0.006 * noise(u * 37.0, 0.4, 37, 131);

        for (int y = 0; y < kPanoramaHeight; y++) {
            // Vertical texture coordinates store tan(elevation); the upper edge
            // fades to clear sky so steep portrait views can safely clamp to it.
            const double altitude = static_cast<double>(y) / (kPanoramaHeight - 1);
            const double haze = std::exp(-altitude * 7.0);
            const SkyColor horizon = mix({ 170.0, 164.0, 143.0 },
                { 199.0, 175.0, 137.0 }, sunFacing * sunFacing);
            SkyColor color = mix({ 61.0, 85.0, 103.0 }, horizon, haze);

            const double sunDistanceSquared = sunAngle * sunAngle
                + (altitude - 0.18) * (altitude - 0.18);
            const double glow = 0.32 * std::exp(-sunDistanceSquared / 0.025);
            color = mix(color, { 232.0, 201.0, 148.0 }, glow);
            const double disc = 1.0 - smoothStep(0.00012, 0.00020, sunDistanceSquared);
            color = mix(color, { 245.0, 226.0, 181.0 }, disc * 0.92);

            // Broad cloud banks with smaller wisps, stretched along the horizon.
            const double clouds = 0.52 * noise(u * 14.0, altitude * 8.0, 14, 151)
                + 0.27 * noise(u * 28.0, altitude * 16.0, 28, 173)
                + 0.14 * noise(u * 56.0, altitude * 32.0, 56, 197)
                + 0.07 * noise(u * 112.0, altitude * 64.0, 112, 211);
            const double cloudBand = smoothStep(0.04, 0.12, altitude)
                * (1.0 - smoothStep(0.75, 1.0, altitude));
            const double cloudOpacity = smoothStep(0.47, 0.72, clouds) * cloudBand * 0.78;
            const SkyColor cloudColor = mix({ 137.0, 145.0, 144.0 },
                { 219.0, 205.0, 176.0 }, smoothStep(0.45, 0.80, clouds) * (0.6 + 0.4 * sunFacing));
            color = mix(color, cloudColor, cloudOpacity);

            // Successively darker ridges provide depth, with dust gathering at
            // their bases and a final fade into the existing desert ground color.
            constexpr double kRidgeEdge = 1.0 / kPanoramaHeight;
            const SkyColor farColor = mix({ 132.0, 134.0, 128.0 },
                { 167.0, 153.0, 126.0 }, std::exp(-altitude * 24.0));
            color = mix(color, farColor, 1.0 - smoothStep(farRidge - kRidgeEdge, farRidge + kRidgeEdge, altitude));
            const SkyColor middleColor = mix({ 101.0, 108.0, 104.0 },
                { 139.0, 128.0, 105.0 }, std::exp(-altitude * 40.0));
            color = mix(color, middleColor, 1.0 - smoothStep(middleRidge - kRidgeEdge, middleRidge + kRidgeEdge, altitude));
            const SkyColor nearColor = mix({ 84.0, 87.0, 78.0 },
                { 107.0, 90.0, 74.0 }, std::exp(-altitude * 70.0));
            color = mix(color, nearColor, 1.0 - smoothStep(nearRidge - kRidgeEdge, nearRidge + kRidgeEdge, altitude));
            pixels[y * kPanoramaWidth + x] = packColor(color, x, y);
        }
    }
    return pixels;
}

std::vector<uint16_t> buildCavePanorama()
{
    std::vector<uint16_t> pixels(kPanoramaWidth * kPanoramaHeight);
    for (int x = 0; x < kPanoramaWidth; x++) {
        const double u = static_cast<double>(x) / kPanoramaWidth;
        const double ceiling = 0.13
            + 0.12 * noise(u * 8.0, 0.3, 8, 227)
            + 0.05 * noise(u * 27.0, 0.6, 27, 239)
            + 0.02 * noise(u * 91.0, 0.2, 91, 251);
        const double stalactiteNoise = noise(u * 23.0, 0.8, 23, 263);
        const double stalactiteTip = ceiling
            - std::max(0.0, stalactiteNoise - 0.58) * 0.65;

        for (int y = 0; y < kPanoramaHeight; y++) {
            const double altitude = static_cast<double>(y) / (kPanoramaHeight - 1);
            const double rockNoise = 0.65 * noise(u * 49.0, altitude * 37.0, 49, 277)
                + 0.35 * noise(u * 103.0, altitude * 81.0, 103, 283);
            const double rockEdge = stalactiteNoise > 0.58
                ? stalactiteTip
                : ceiling;
            const double rock = smoothStep(rockEdge - 0.008, rockEdge + 0.008, altitude);
            const double distantWall = (1.0 - smoothStep(0.01, 0.16, altitude))
                * (0.25 + 0.35 * noise(u * 13.0, altitude * 19.0, 13, 293));

            SkyColor color = mix({ 17.0, 16.0, 15.0 },
                { 49.0, 39.0, 29.0 }, std::exp(-altitude * 18.0));
            color = mix(color, { 61.0, 48.0, 34.0 }, distantWall);
            const SkyColor rockColor = mix({ 36.0, 31.0, 27.0 },
                { 77.0, 62.0, 43.0 }, rockNoise);
            color = mix(color, rockColor, rock);
            pixels[y * kPanoramaWidth + x] = packColor(color, x, y);
        }
    }
    return pixels;
}

} // namespace

void firstPersonSkyRender(unsigned char* buffer, int width, int height, int pitch,
    double yaw, double fieldOfViewScale, int ambientIntensityIndex,
    FirstPersonSkyType skyType)
{
    if (buffer == nullptr || width < 2 || height < 2 || pitch < width
        || !std::isfinite(yaw) || !std::isfinite(fieldOfViewScale)
        || fieldOfViewScale <= 0.0 || ambientIntensityIndex < 0
        || ambientIntensityIndex >= COLOR_COUNT) {
        return;
    }

    // Build the scenery only once. Retain logical RGB555 colors rather than
    // palette indices so loading another palette cannot leave stale colors.
    const std::vector<uint16_t>* panorama;
    if (skyType == FirstPersonSkyType::Cave) {
        static const std::vector<uint16_t> cavePanorama = buildCavePanorama();
        panorama = &cavePanorama;
    } else {
        static const std::vector<uint16_t> desertPanorama = buildPanorama();
        panorama = &desertPanorama;
    }
    static std::vector<double> columnAngles;
    static std::vector<double> columnScales;
    static std::vector<int> panoramaColumns;
    static int cachedHeight = 0;
    static double cachedFieldOfView = 0.0;
    const int horizon = height / 2;
    if (columnAngles.size() != static_cast<std::size_t>(width)
        || cachedHeight != height || cachedFieldOfView != fieldOfViewScale) {
        columnAngles.resize(width);
        columnScales.resize(width);
        panoramaColumns.resize(width);
        for (int x = 0; x < width; x++) {
            const double cameraX = (2.0 * x / width - 1.0) * fieldOfViewScale;
            columnAngles[x] = std::atan(cameraX) * kPanoramaWidth / (2.0 * kPi);
            // Use the horizontal focal length for both axes, preserving the
            // sun's circular shape across different viewport aspect ratios.
            columnScales[x] = (kPanoramaHeight - 1) * 2.0 * fieldOfViewScale
                / (width * std::sqrt(1.0 + cameraX * cameraX));
        }
        cachedHeight = height;
        cachedFieldOfView = fieldOfViewScale;
    }

    const double rotation = std::fmod(yaw, 360.0) * kPanoramaWidth / 360.0;
    for (int x = 0; x < width; x++) {
        const int column = static_cast<int>(std::floor(rotation + columnAngles[x]));
        panoramaColumns[x] = (column % kPanoramaWidth + kPanoramaWidth) % kPanoramaWidth;
    }
    for (int y = 0; y < horizon; y++) {
        unsigned char* row = buffer + y * pitch;
        for (int x = 0; x < width; x++) {
            const int panoramaY = static_cast<int>(std::min(
                (horizon - y) * columnScales[x], static_cast<double>(kPanoramaHeight - 1)));
            const unsigned char color =
                _colorTable[(*panorama)[panoramaY * kPanoramaWidth + panoramaColumns[x]]];
            row[x] = intensityColorTable[color][ambientIntensityIndex];
        }
    }
}

} // namespace fallout
