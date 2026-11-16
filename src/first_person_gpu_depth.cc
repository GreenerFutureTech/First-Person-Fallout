#include "first_person_gpu_depth.h"

#include <SDL_opengl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

namespace fallout {
namespace {

// Load through SDL so non-OpenGL backends require no GL link dependency.
#define GPU_GL_FUNCTIONS(X) \
    X(void, GetIntegerv, (GLenum, GLint*)) \
    X(GLenum, GetError, ()) \
    X(void, PushAttrib, (GLbitfield)) \
    X(void, PopAttrib, ()) \
    X(void, Enable, (GLenum)) \
    X(void, Disable, (GLenum)) \
    X(void, BlendFuncSeparate, (GLenum, GLenum, GLenum, GLenum)) \
    X(void, BlendEquation, (GLenum)) \
    X(void, ActiveTexture, (GLenum)) \
    X(void, GenTextures, (GLsizei, GLuint*)) \
    X(void, DeleteTextures, (GLsizei, const GLuint*)) \
    X(void, BindTexture, (GLenum, GLuint)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint)) \
    X(void, PixelStorei, (GLenum, GLint)) \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(GLuint, CreateShader, (GLenum)) \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
    X(void, CompileShader, (GLuint)) \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*)) \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteShader, (GLuint)) \
    X(GLuint, CreateProgram, ()) \
    X(void, AttachShader, (GLuint, GLuint)) \
    X(void, LinkProgram, (GLuint)) \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*)) \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
    X(void, DeleteProgram, (GLuint)) \
    X(void, UseProgram, (GLuint)) \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*)) \
    X(void, Uniform1i, (GLint, GLint)) \
    X(void, Uniform1f, (GLint, GLfloat)) \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat)) \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat)) \
    X(void, Begin, (GLenum)) \
    X(void, End, ()) \
    X(void, TexCoord2f, (GLfloat, GLfloat)) \
    X(void, Vertex2f, (GLfloat, GLfloat))

struct GlFunctions {
#define DECLARE_GL(result, name, args) result (APIENTRY* name) args = nullptr;
    GPU_GL_FUNCTIONS(DECLARE_GL)
#undef DECLARE_GL
} gl;

GLuint program = 0;
GLuint depthTexture = 0;
GLuint columnTexture = 0;
int depthWidth = 0;
int depthHeight = 0;
int horizonY = 0;
GLint savedProgram = 0;
SDL_Texture* lastTexture = nullptr;
bool batchOpen = false;
double batchDepth = 0.0;
int batchPolicy = 0;
float batchTexWidth = 1.0f, batchTexHeight = 1.0f;
unsigned int batchCount = 0;
std::vector<float> depthPixels;
std::vector<float> columnPixels;
std::vector<float> nextDepthPixels;
std::vector<float> nextColumnPixels;
struct {
    GLint logicalSize, depthSize, horizon, sprite, pixels, columns, depth, policy, color;
} uniform;

GLuint compile(GLenum type, const char* source)
{
    GLuint shader = gl.CreateShader(type);
    gl.ShaderSource(shader, 1, &source, nullptr);
    gl.CompileShader(shader);
    GLint success = GL_FALSE;
    gl.GetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (success != GL_TRUE) {
        char message[1024] = {};
        gl.GetShaderInfoLog(shader, sizeof(message), nullptr, message);
        SDL_Log("F11 depth shader compile failed: %s", message);
        gl.DeleteShader(shader);
        return 0;
    }
    return shader;
}

void configureTexture(GLuint texture)
{
    gl.BindTexture(GL_TEXTURE_2D, texture);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void flushRenderer(SDL_Renderer* renderer)
{
#if SDL_VERSION_ATLEAST(2, 0, 10)
    SDL_RenderFlush(renderer);
#else
    // Older SDL versions submit immediately and have no command queue.
    (void)renderer;
#endif
}

} // namespace

bool firstPersonGpuDepthInit(SDL_Renderer* renderer)
{
    SDL_RendererInfo info {};
    if (SDL_GetRendererInfo(renderer, &info) != 0 || SDL_strcmp(info.name, "opengl") != 0
        || SDL_GL_GetCurrentContext() == nullptr
        || !SDL_GL_ExtensionSupported("GL_ARB_texture_float")
        || !SDL_GL_ExtensionSupported("GL_ARB_texture_non_power_of_two")) return false;
    const char* disabled = SDL_getenv("F2_FENCE_GPU_DEPTH");
    if (disabled != nullptr && SDL_strcmp(disabled, "0") == 0) return false;
#define LOAD_GL(result, name, args) \
    gl.name = reinterpret_cast<decltype(gl.name)>(SDL_GL_GetProcAddress("gl" #name)); \
    if (gl.name == nullptr) return false;
    GPU_GL_FUNCTIONS(LOAD_GL)
#undef LOAD_GL
    GLint textureUnits = 0;
    gl.GetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &textureUnits);
    if (textureUnits < 3) return false;

    const char* vertexSource = R"GLSL(#version 120
uniform vec2 logicalSize;
varying vec2 screenPosition;
varying vec2 spriteUv;
void main() {
    screenPosition = gl_Vertex.xy;
    spriteUv = gl_MultiTexCoord0.xy;
    gl_Position = vec4(2.0 * screenPosition.x / logicalSize.x - 1.0,
        1.0 - 2.0 * screenPosition.y / logicalSize.y, 0.0, 1.0);
}
)GLSL";
    const char* fragmentSource = R"GLSL(#version 120
uniform sampler2D spriteTexture;
uniform sampler2D pixelDepths;
uniform sampler2D columnDepths;
uniform vec2 depthSize;
uniform float horizon;
uniform float actorDepth;
uniform int depthPolicy;
uniform vec4 modulation;
varying vec2 screenPosition;
varying vec2 spriteUv;
void main() {
    vec4 color = texture2D(spriteTexture, spriteUv) * modulation;
    if (color.a == 0.0) discard;
    if (depthPolicy != 0) {
        vec2 pixel = floor(screenPosition) + vec2(0.5);
        // CPU depth storage is column-major: upload without transposing it.
        float wall = texture2D(pixelDepths, pixel.yx / depthSize.yx).r;
        if (depthPolicy == 2) {
            wall = min(wall, texture2D(columnDepths,
                vec2(pixel.x / depthSize.x, pixel.y < horizon ? 0.25 : 0.75)).r);
        }
        if (actorDepth >= wall) discard;
    }
    gl_FragColor = color;
}
)GLSL";
    GLuint vertex = compile(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = compile(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) gl.DeleteShader(vertex);
        if (fragment != 0) gl.DeleteShader(fragment);
        return false;
    }
    program = gl.CreateProgram();
    gl.AttachShader(program, vertex);
    gl.AttachShader(program, fragment);
    gl.LinkProgram(program);
    gl.DeleteShader(vertex);
    gl.DeleteShader(fragment);
    GLint success = GL_FALSE;
    gl.GetProgramiv(program, GL_LINK_STATUS, &success);
    if (success != GL_TRUE) {
        char message[1024] = {};
        gl.GetProgramInfoLog(program, sizeof(message), nullptr, message);
        SDL_Log("F11 depth shader link failed: %s", message);
        firstPersonGpuDepthDestroy();
        return false;
    }
    uniform.logicalSize = gl.GetUniformLocation(program, "logicalSize");
    uniform.depthSize = gl.GetUniformLocation(program, "depthSize");
    uniform.horizon = gl.GetUniformLocation(program, "horizon");
    uniform.sprite = gl.GetUniformLocation(program, "spriteTexture");
    uniform.pixels = gl.GetUniformLocation(program, "pixelDepths");
    uniform.columns = gl.GetUniformLocation(program, "columnDepths");
    uniform.depth = gl.GetUniformLocation(program, "actorDepth");
    uniform.policy = gl.GetUniformLocation(program, "depthPolicy");
    uniform.color = gl.GetUniformLocation(program, "modulation");
    gl.GenTextures(1, &depthTexture);
    gl.GenTextures(1, &columnTexture);
    return true;
}

void firstPersonGpuDepthDestroy()
{
    if (depthTexture != 0) gl.DeleteTextures(1, &depthTexture);
    if (columnTexture != 0) gl.DeleteTextures(1, &columnTexture);
    if (program != 0) gl.DeleteProgram(program);
    program = depthTexture = columnTexture = 0;
    depthWidth = depthHeight = 0;
    depthPixels.clear();
    columnPixels.clear();
    nextDepthPixels.clear();
    nextColumnPixels.clear();
}

bool firstPersonGpuDepthUpload(SDL_Renderer* renderer, int width, int height,
    int horizon, const double* pixels, const double* walls, const double* sills,
    FirstPersonGpuDepthUploadStats* stats)
{
    if (stats != nullptr) *stats = {};
    if (program == 0 || width <= 0 || height <= 0) return false;
    GLint maximum = 0;
    gl.GetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
    if (width > maximum || height > maximum) return false;
    const auto asFloat = [](double depth) {
        return std::isfinite(depth) ? static_cast<float>(depth) : std::numeric_limits<float>::max();
    };
    const Uint64 packStart = stats != nullptr ? SDL_GetPerformanceCounter() : 0;
    nextDepthPixels.resize(static_cast<size_t>(width) * height);
    std::transform(pixels, pixels + nextDepthPixels.size(), nextDepthPixels.begin(), asFloat);
    nextColumnPixels.resize(static_cast<size_t>(width) * 2);
    for (int x = 0; x < width; ++x) {
        nextColumnPixels[x] = asFloat(walls[x]);
        nextColumnPixels[width + x] = asFloat(std::min(walls[x], sills[x]));
    }
    const bool pixelsChanged = width != depthWidth || height != depthHeight
        || std::memcmp(depthPixels.data(), nextDepthPixels.data(), nextDepthPixels.size() * sizeof(float)) != 0;
    const bool columnsChanged = width != depthWidth
        || std::memcmp(columnPixels.data(), nextColumnPixels.data(), nextColumnPixels.size() * sizeof(float)) != 0;
    const Uint64 uploadStart = stats != nullptr ? SDL_GetPerformanceCounter() : 0;
    if (stats != nullptr) stats->packMs = 1000.0 * (uploadStart - packStart) / SDL_GetPerformanceFrequency();
    // Actor animation does not move walls. Retain the depth texture when the
    // raster is unchanged even if the rest of the scene was refreshed.
    horizonY = horizon;
    if (!pixelsChanged && !columnsChanged) return true;
    flushRenderer(renderer);
    // Preserve SDL's texture, active unit and unpack state around the upload.
    gl.PushAttrib(GL_TEXTURE_BIT | GL_PIXEL_MODE_BIT);
    GLint alignment = 0, rowLength = 0;
    gl.GetIntegerv(GL_UNPACK_ALIGNMENT, &alignment);
    gl.GetIntegerv(GL_UNPACK_ROW_LENGTH, &rowLength);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl.ActiveTexture(GL_TEXTURE0);
    while (gl.GetError() != GL_NO_ERROR) { }
    if (pixelsChanged) {
        configureTexture(depthTexture);
        if (width != depthWidth || height != depthHeight) {
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE32F_ARB, height, width,
                0, GL_LUMINANCE, GL_FLOAT, nullptr);
        }
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, height, width, GL_LUMINANCE, GL_FLOAT, nextDepthPixels.data());
    }
    if (columnsChanged) {
        configureTexture(columnTexture);
        if (width != depthWidth) {
            gl.TexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE32F_ARB, width, 2,
                0, GL_LUMINANCE, GL_FLOAT, nullptr);
        }
        gl.TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, 2, GL_LUMINANCE, GL_FLOAT, nextColumnPixels.data());
    }
    const bool success = gl.GetError() == GL_NO_ERROR;
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, alignment);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, rowLength);
    gl.PopAttrib();
    if (success) {
        depthWidth = width;
        depthHeight = height;
        depthPixels.swap(nextDepthPixels);
        columnPixels.swap(nextColumnPixels);
        if (stats != nullptr) stats->bytes =
            (pixelsChanged ? depthPixels.size() * sizeof(float) : 0)
            + (columnsChanged ? columnPixels.size() * sizeof(float) : 0);
    } else {
        depthWidth = depthHeight = 0;
    }
    if (stats != nullptr) stats->uploadMs = 1000.0 * (SDL_GetPerformanceCounter() - uploadStart) / SDL_GetPerformanceFrequency();
    return success;
}

void firstPersonGpuDepthBegin(SDL_Renderer* renderer)
{
    flushRenderer(renderer);
    gl.GetIntegerv(GL_CURRENT_PROGRAM, &savedProgram);
    gl.PushAttrib(GL_ALL_ATTRIB_BITS);
    gl.Disable(GL_DEPTH_TEST);
    gl.Disable(GL_SCISSOR_TEST);
    gl.Disable(GL_CULL_FACE);
    gl.Enable(GL_BLEND);
    gl.BlendEquation(GL_FUNC_ADD);
    gl.BlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl.UseProgram(program);
    int width = 0, height = 0;
    SDL_RenderGetLogicalSize(renderer, &width, &height);
    gl.Uniform2f(uniform.logicalSize, static_cast<float>(width), static_cast<float>(height));
    gl.Uniform2f(uniform.depthSize, static_cast<float>(depthWidth), static_cast<float>(depthHeight));
    gl.Uniform1f(uniform.horizon, static_cast<float>(horizonY));
    gl.Uniform1i(uniform.sprite, 0);
    gl.Uniform1i(uniform.pixels, 1);
    gl.Uniform1i(uniform.columns, 2);
    gl.ActiveTexture(GL_TEXTURE1);
    gl.BindTexture(GL_TEXTURE_2D, depthTexture);
    gl.ActiveTexture(GL_TEXTURE2);
    gl.BindTexture(GL_TEXTURE_2D, columnTexture);
    gl.ActiveTexture(GL_TEXTURE0);
    lastTexture = nullptr;
    batchOpen = false;
    batchCount = 0;
}

void firstPersonGpuDepthDraw(SDL_Texture* texture, const SDL_Rect& destination,
    const SDL_Rect& clip, double depth, int policy)
{
    SDL_Rect visible;
    if (!SDL_IntersectRect(&destination, &clip, &visible)) return;
    if (!batchOpen || texture != lastTexture || policy != batchPolicy
        || (policy != 0 && depth != batchDepth)) {
        if (batchOpen) gl.End();
        SDL_GL_BindTexture(texture, &batchTexWidth, &batchTexHeight);
        lastTexture = texture;
        SDL_Color color { 255, 255, 255, 255 };
        SDL_GetTextureColorMod(texture, &color.r, &color.g, &color.b);
        SDL_GetTextureAlphaMod(texture, &color.a);
        gl.Uniform4f(uniform.color, color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f);
        gl.Uniform1f(uniform.depth, static_cast<float>(depth));
        gl.Uniform1i(uniform.policy, policy);
        batchDepth = depth;
        batchPolicy = policy;
        gl.Begin(GL_QUADS);
        batchOpen = true;
        ++batchCount;
    }
    const float left = static_cast<float>(visible.x);
    const float top = static_cast<float>(visible.y);
    const float right = static_cast<float>(visible.x + visible.w);
    const float bottom = static_cast<float>(visible.y + visible.h);
    const float u0 = (left - destination.x) / destination.w * batchTexWidth;
    const float v0 = (top - destination.y) / destination.h * batchTexHeight;
    const float u1 = (right - destination.x) / destination.w * batchTexWidth;
    const float v1 = (bottom - destination.y) / destination.h * batchTexHeight;
    gl.TexCoord2f(u0, v0); gl.Vertex2f(left, top);
    gl.TexCoord2f(u1, v0); gl.Vertex2f(right, top);
    gl.TexCoord2f(u1, v1); gl.Vertex2f(right, bottom);
    gl.TexCoord2f(u0, v1); gl.Vertex2f(left, bottom);
}

unsigned int firstPersonGpuDepthEnd()
{
    if (batchOpen) gl.End();
    batchOpen = false;
    gl.UseProgram(savedProgram);
    gl.PopAttrib();
    // BindTexture updates SDL's internal texture cache. Leave GL and that
    // cache in the same known state before SDL resumes drawing UI/overlays.
    gl.ActiveTexture(GL_TEXTURE0);
    if (lastTexture != nullptr) SDL_GL_UnbindTexture(lastTexture);
    return batchCount;
}

} // namespace fallout
