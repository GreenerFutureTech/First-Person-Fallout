#include "indexed_texture_palette.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using fallout::IndexedTexturePalette;
using Palette = std::array<unsigned char, 768>;

static void require(bool condition)
{
    if (!condition) {
        std::fputs("Palette dependency test failed\n", stderr);
        std::exit(1);
    }
}

// Independent reference: compare the actual expanded pixels, not cache state.
static std::vector<uint32_t> expand(const std::vector<unsigned char>& pixels, const Palette& palette)
{
    std::vector<uint32_t> result;
    for (unsigned char index : pixels) {
        result.push_back(index == 0 ? 0 : 0xFF000000u
            | (uint32_t(palette[index * 3]) << 18)
            | (uint32_t(palette[index * 3 + 1]) << 10)
            | (uint32_t(palette[index * 3 + 2]) << 2));
    }
    return result;
}

int main()
{
    Palette original {};
    for (size_t i = 0; i < original.size(); i++) {
        original[i] = static_cast<unsigned char>(i % 64);
    }

    // Empty/transparent sprites, ordinary art, cycling colors, and boundary 255.
    const std::vector<std::vector<unsigned char>> sprites = {
        {}, { 0, 0 }, { 0, 1, 42, 42, 128, 228 }, { 0, 229, 233, 248, 254, 255 }
    };
    for (const auto& pixels : sprites) {
        IndexedTexturePalette dependencies;
        for (unsigned char index : pixels) {
            dependencies.useColor(index);
        }
        dependencies.capture(original.data());
        const auto originalRgba = expand(pixels, original);
        require(dependencies.matches(original.data()));

        // Exhaust every entry and RGB channel, including transparent index zero.
        for (size_t component = 0; component < original.size(); component++) {
            Palette changed = original;
            changed[component] ^= 1;
            require(dependencies.matches(changed.data()) == (expand(pixels, changed) == originalRgba));
        }

        // Multiple unseen palette updates must be compared against the upload
        // snapshot, not just the immediately preceding rendered frame.
        Palette cycled = original;
        for (int index = 229; index < 256; index++) {
            cycled[index * 3] ^= 7;
        }
        require(dependencies.matches(cycled.data()) == (expand(pixels, cycled) == originalRgba));
        cycled[42 * 3 + 2] ^= 3;
        require(dependencies.matches(cycled.data()) == (expand(pixels, cycled) == originalRgba));

        // A successful re-upload adopts the new palette; later fades still
        // invalidate every sprite whose visible pixels actually change.
        dependencies.capture(cycled.data());
        require(dependencies.matches(cycled.data()));
        Palette black {};
        require(dependencies.matches(black.data()) == (expand(pixels, black) == expand(pixels, cycled)));
    }
    std::puts("Palette dependency tests passed");
}
