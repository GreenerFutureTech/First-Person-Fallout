cmake_minimum_required(VERSION 3.18)
project(fallout_render_tests LANGUAGES CXX)

enable_testing()
add_executable(indexed_texture_palette_test indexed_texture_palette_test.cc)
target_compile_features(indexed_texture_palette_test PRIVATE cxx_std_17)
target_include_directories(indexed_texture_palette_test PRIVATE ../../src)
add_test(NAME indexed_texture_palette COMMAND indexed_texture_palette_test)

add_executable(billboard_clip_bands_test billboard_clip_bands_test.cc)
target_compile_features(billboard_clip_bands_test PRIVATE cxx_std_17)
target_include_directories(billboard_clip_bands_test PRIVATE ../../src)
add_test(NAME billboard_clip_bands COMMAND billboard_clip_bands_test)
