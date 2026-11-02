#include "svga.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include <SDL.h>
#include <SDL_opengl.h>

#include "color.h"
#include "config.h"
#include "debug.h"
#include "dinput.h"
#include "draw.h"
#include "game.h"
#include "interface.h"
#include "memory.h"
#include "mouse.h"
#include "movie.h"
#include "scan_unimplemented.h"
#include "settings.h"
#include "text_font.h"
#include "tile.h"
#include "win32.h"
#include "window_manager.h"
#include "window_manager_private.h"

#pragma comment(lib, "opengl32.lib")

namespace fallout {

static bool createRenderer(int width, int height);
static void destroyRenderer();
static bool initializeGpuFirstPersonShaders();
static bool initializeGpuFirstPersonScene(int width, int height);
static void destroyGpuFirstPersonShaders();
static bool gpuFirstPersonDrawStagedWalls();
static void gpuFirstPersonDrawStagedBillboards();

// screen rect
Rect _scr_size;

// 0x6ACA18 scr_blit
void (*_scr_blit)(unsigned char* src, int src_pitch, int unused, int src_x, int src_y, int src_width, int src_height, int dest_x, int dest_y) = _GNW95_ShowRect;

// 0x6ACA1C zero_mem
void (*_zero_mem)() = nullptr;

SDL_Window* gSdlWindow = nullptr;
SDL_Surface* gSdlSurface = nullptr;
SDL_Renderer* gSdlRenderer = nullptr;
SDL_Texture* gSdlTexture = nullptr;
SDL_Surface* gSdlTextureSurface = nullptr;
static bool gSdlRendererHardwareAccelerated = false;
static bool gGpuFirstPersonShadersReady = false;
static bool gGpuFirstPersonSceneReady = false;
static bool gGpuFirstPersonSceneBackendEnabled = false;
static bool gGpuFirstPersonExperimentalMode = false;
static bool gGpuFirstPersonFrameActive = false;
static SDL_GLContext gGpuFirstPersonContext = nullptr;
static GLuint gGpuFirstPersonShaderProgram = 0;

using GlCreateShaderProc = GLuint(APIENTRY*)(GLenum);
using GlShaderSourceProc = void(APIENTRY*)(GLuint, GLsizei, const GLchar* const*, const GLint*);
using GlCompileShaderProc = void(APIENTRY*)(GLuint);
using GlGetShaderivProc = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetShaderInfoLogProc = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlCreateProgramProc = GLuint(APIENTRY*)();
using GlAttachShaderProc = void(APIENTRY*)(GLuint, GLuint);
using GlLinkProgramProc = void(APIENTRY*)(GLuint);
using GlGetProgramivProc = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetProgramInfoLogProc = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, GLchar*);
using GlDeleteShaderProc = void(APIENTRY*)(GLuint);
using GlDeleteProgramProc = void(APIENTRY*)(GLuint);
using GlGetStringProc = const GLubyte*(APIENTRY*)(GLenum);
using GlGenTexturesProc = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteTexturesProc = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindTextureProc = void(APIENTRY*)(GLenum, GLuint);
using GlTexParameteriProc = void(APIENTRY*)(GLenum, GLenum, GLint);
using GlTexImage2DProc = void(APIENTRY*)(GLenum, GLint, GLint, GLsizei, GLsizei,
    GLint, GLenum, GLenum, const void*);
using GlGenFramebuffersProc = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteFramebuffersProc = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindFramebufferProc = void(APIENTRY*)(GLenum, GLuint);
using GlFramebufferTexture2DProc = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlCheckFramebufferStatusProc = GLenum(APIENTRY*)(GLenum);
using GlViewportProc = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GlGenVertexArraysProc = void(APIENTRY*)(GLsizei, GLuint*);
using GlDeleteVertexArraysProc = void(APIENTRY*)(GLsizei, const GLuint*);
using GlBindVertexArrayProc = void(APIENTRY*)(GLuint);
using GlUseProgramProc = void(APIENTRY*)(GLuint);
using GlClearColorProc = void(APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat);
using GlClearProc = void(APIENTRY*)(GLbitfield);
using GlDrawArraysProc = void(APIENTRY*)(GLenum, GLint, GLsizei);
using GlGetIntegervProc = void(APIENTRY*)(GLenum, GLint*);
using GlGetFloatvProc = void(APIENTRY*)(GLenum, GLfloat*);
using GlGetUniformLocationProc = GLint(APIENTRY*)(GLuint, const GLchar*);
using GlActiveTextureProc = void(APIENTRY*)(GLenum);
using GlUniform1iProc = void(APIENTRY*)(GLint, GLint);
using GlUniform1fProc = void(APIENTRY*)(GLint, GLfloat);
using GlUniform2fProc = void(APIENTRY*)(GLint, GLfloat, GLfloat);
using GlUniform4fProc = void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
using GlEnableProc = void(APIENTRY*)(GLenum);
using GlBlendFuncProc = void(APIENTRY*)(GLenum, GLenum);

static GlCreateShaderProc gGlCreateShader = nullptr;
static GlShaderSourceProc gGlShaderSource = nullptr;
static GlCompileShaderProc gGlCompileShader = nullptr;
static GlGetShaderivProc gGlGetShaderiv = nullptr;
static GlGetShaderInfoLogProc gGlGetShaderInfoLog = nullptr;
static GlCreateProgramProc gGlCreateProgram = nullptr;
static GlAttachShaderProc gGlAttachShader = nullptr;
static GlLinkProgramProc gGlLinkProgram = nullptr;
static GlGetProgramivProc gGlGetProgramiv = nullptr;
static GlGetProgramInfoLogProc gGlGetProgramInfoLog = nullptr;
static GlDeleteShaderProc gGlDeleteShader = nullptr;
static GlDeleteProgramProc gGlDeleteProgram = nullptr;
static GlGetStringProc gGlGetString = nullptr;
static GlGenTexturesProc gGlGenTextures = nullptr;
static GlDeleteTexturesProc gGlDeleteTextures = nullptr;
static GlBindTextureProc gGlBindTexture = nullptr;
static GlTexParameteriProc gGlTexParameteri = nullptr;
static GlTexImage2DProc gGlTexImage2D = nullptr;
static GlGenFramebuffersProc gGlGenFramebuffers = nullptr;
static GlDeleteFramebuffersProc gGlDeleteFramebuffers = nullptr;
static GlBindFramebufferProc gGlBindFramebuffer = nullptr;
static GlFramebufferTexture2DProc gGlFramebufferTexture2D = nullptr;
static GlCheckFramebufferStatusProc gGlCheckFramebufferStatus = nullptr;
static GlViewportProc gGlViewport = nullptr;
static GlGenVertexArraysProc gGlGenVertexArrays = nullptr;
static GlDeleteVertexArraysProc gGlDeleteVertexArrays = nullptr;
static GlBindVertexArrayProc gGlBindVertexArray = nullptr;
static GlUseProgramProc gGlUseProgram = nullptr;
static GlClearColorProc gGlClearColor = nullptr;
static GlClearProc gGlClear = nullptr;
static GlDrawArraysProc gGlDrawArrays = nullptr;
static GlGetIntegervProc gGlGetIntegerv = nullptr;
static GlGetFloatvProc gGlGetFloatv = nullptr;
static GlGetUniformLocationProc gGlGetUniformLocation = nullptr;
static GlActiveTextureProc gGlActiveTexture = nullptr;
static GlUniform1iProc gGlUniform1i = nullptr;
static GlUniform1fProc gGlUniform1f = nullptr;
static GlUniform2fProc gGlUniform2f = nullptr;
static GlUniform4fProc gGlUniform4f = nullptr;
static GlEnableProc gGlEnable = nullptr;
static GlBlendFuncProc gGlBlendFunc = nullptr;
static GLuint gGpuFirstPersonSceneFramebuffer = 0;
static GLuint gGpuFirstPersonSceneTexture = 0;
static GLuint gGpuFirstPersonSceneVertexArray = 0;
static GLuint gGpuFirstPersonFloorTestTexture = 0;
static GLint gGpuFirstPersonFloorCacheLocation = -1;
static GLint gGpuFirstPersonCameraLocation = -1;
static GLint gGpuFirstPersonDirectionLocation = -1;
static GLint gGpuFirstPersonCameraPlaneLocation = -1;
static GLint gGpuFirstPersonViewportLocation = -1;
static GLint gGpuFirstPersonHorizonLocation = -1;
static GLint gGpuFirstPersonMapSizeLocation = -1;
static GLint gGpuFirstPersonWallColumnsLocation = -1;
static GLint gGpuFirstPersonScenePassLocation = -1;
static GLint gGpuFirstPersonBillboardTextureLocation = -1;
static GLint gGpuFirstPersonBillboardRectLocation = -1;
static GLint gGpuFirstPersonBillboardClipLocation = -1;
static GLuint gGpuFirstPersonWallColumnTexture = 0;
static std::vector<float> gGpuFirstPersonStagedWallColumns;
static int gGpuFirstPersonStagedWallColumnCount = 0;
static bool gGpuFirstPersonWallColumnsDirty = false;
static int gGpuFirstPersonSceneWidth = 0;
static int gGpuFirstPersonSceneHeight = 0;
static bool gGpuFirstPersonSceneBound = false;
static GLint gGpuFirstPersonPreviousFramebuffer = 0;
static GLint gGpuFirstPersonPreviousViewport[4] {};
static GLint gGpuFirstPersonPreviousActiveTexture = GL_TEXTURE0;
static GLint gGpuFirstPersonPreviousTexture = 0;
static GLint gGpuFirstPersonPreviousVertexArray = 0;
static GLint gGpuFirstPersonPreviousProgram = 0;
static GLfloat gGpuFirstPersonPreviousClearColor[4] {};
static std::vector<unsigned char> gGpuFirstPersonStagedFloorCache;
static int gGpuFirstPersonStagedFloorCacheWidth = 0;
static int gGpuFirstPersonStagedFloorCacheHeight = 0;
static GLuint gGpuFirstPersonFloorCacheTexture = 0;
static bool gGpuFirstPersonFloorCacheTextureDirty = false;
struct GpuFirstPersonCamera {
    double cameraX = 0.0;
    double cameraY = 0.0;
    double directionX = 0.0;
    double directionY = -1.0;
    double cameraPlaneX = 0.66;
    double cameraPlaneY = 0.0;
    double mapWidth = 0.0;
    double mapHeight = 0.0;
    bool valid = false;
};
static GpuFirstPersonCamera gGpuFirstPersonStagedCamera;

struct GpuBillboard {
    SDL_Texture* texture;
    GLuint glTexture;
    SDL_Rect destination;
    SDL_Rect clip;
    double depth;
};

struct GpuBillboardTexture {
    SDL_Texture* texture;
    int width;
    int height;
    unsigned int paletteHash;
};

static std::vector<GpuBillboard> gGpuBillboards;
static SDL_Rect gGpuOverlayRect {};
static bool gGpuHasOverlayRect = false;
static std::unordered_map<const unsigned char*, GpuBillboardTexture> gGpuBillboardTextures;
static std::unordered_map<const unsigned char*, GLuint> gGpuFirstPersonBillboardTextures;
static unsigned char gGpuPalette[256 * 3];
static unsigned int gGpuPaletteHash = 0;

// TODO: Remove once migration to update-render cycle is completed.
FpsLimiter sharedFpsLimiter;

// 0x4CAD08 init_mode_320_200
int _init_mode_320_200()
{
    return _GNW95_init_mode_ex(320, 200, 8);
}

bool screenIsHardwareAccelerated()
{
    return gSdlRendererHardwareAccelerated;
}

const char* screenGetRendererName()
{
    if (gSdlRenderer == nullptr) {
        return nullptr;
    }

    SDL_RendererInfo rendererInfo;
    if (SDL_GetRendererInfo(gSdlRenderer, &rendererInfo) != 0) {
        return nullptr;
    }

    return rendererInfo.name;
}

bool screenSupportsGpuFirstPersonPass()
{
    if (!gSdlRendererHardwareAccelerated || gSdlRenderer == nullptr) {
        return false;
    }

    SDL_RendererInfo rendererInfo;
    if (SDL_GetRendererInfo(gSdlRenderer, &rendererInfo) != 0) {
        return false;
    }

    return (rendererInfo.flags & SDL_RENDERER_TARGETTEXTURE) != 0
        && (rendererInfo.flags & SDL_RENDERER_ACCELERATED) != 0;
}

bool screenHasOpenGLContext()
{
    return gSdlWindow != nullptr
        && (SDL_GetWindowFlags(gSdlWindow) & SDL_WINDOW_OPENGL) != 0
        && SDL_GL_GetCurrentContext() != nullptr;
}

bool screenSupportsGpuFirstPersonShaders()
{
    return gGpuFirstPersonShadersReady;
}

bool screenUsesGpuFirstPersonScene()
{
    return gGpuFirstPersonSceneBackendEnabled && gGpuFirstPersonExperimentalMode;
}

bool screenToggleGpuFirstPersonExperimentalMode()
{
    gGpuFirstPersonExperimentalMode = !gGpuFirstPersonExperimentalMode;
    if (gGpuFirstPersonExperimentalMode) {
        if (!screenGpuFirstPersonPresentationEligible()) {
            debugPrint("GPU first-person OpenGL mode: waiting for active F11 frame\n");
            gGpuFirstPersonExperimentalMode = false;
        } else {
            destroyRenderer();
            gGpuFirstPersonContext = SDL_GL_CreateContext(gSdlWindow);
            if (gGpuFirstPersonContext == nullptr
                || SDL_GL_MakeCurrent(gSdlWindow, gGpuFirstPersonContext) != 0) {
                debugPrint("GPU first-person OpenGL mode: context creation failed: %s\n",
                    SDL_GetError());
                if (gGpuFirstPersonContext != nullptr) {
                    SDL_GL_DeleteContext(gGpuFirstPersonContext);
                    gGpuFirstPersonContext = nullptr;
                }
                gGpuFirstPersonExperimentalMode = false;
                createRenderer(screenGetWidth(), screenGetHeight());
                return gGpuFirstPersonExperimentalMode;
            }
            const bool shadersReady = initializeGpuFirstPersonShaders();
            if (!shadersReady) {
                debugPrint("GPU first-person OpenGL mode: shader initialization failed\n");
            }
            const bool targetReady = shadersReady
                && initializeGpuFirstPersonScene(screenGetWidth(), screenGetHeight());
            if (shadersReady && !targetReady) {
                debugPrint("GPU first-person OpenGL mode: scene target initialization failed\n");
            }
            if (!targetReady) {
                debugPrint("GPU first-person OpenGL mode: initialization failed; restoring SDL\n");
                destroyGpuFirstPersonShaders();
                SDL_GL_DeleteContext(gGpuFirstPersonContext);
                gGpuFirstPersonContext = nullptr;
                gGpuFirstPersonExperimentalMode = false;
                createRenderer(screenGetWidth(), screenGetHeight());
            } else {
                gGpuFirstPersonSceneBackendEnabled = true;
                debugPrint("GPU first-person OpenGL mode: active floor presentation\n");
            }
        }
    } else if (gGpuFirstPersonSceneBackendEnabled) {
        destroyGpuFirstPersonShaders();
        SDL_GL_DeleteContext(gGpuFirstPersonContext);
        gGpuFirstPersonContext = nullptr;
        if (createRenderer(screenGetWidth(), screenGetHeight())) {
            windowRefreshAll(&_scr_size);
            gGpuFirstPersonSceneBackendEnabled = false;
        } else {
            debugPrint("GPU first-person OpenGL mode: failed to restore SDL renderer\n");
        }
    }
    return gGpuFirstPersonExperimentalMode;
}

bool screenGpuFirstPersonExperimentalModeEnabled()
{
    return gGpuFirstPersonExperimentalMode;
}

bool screenGpuFirstPersonStagedInputsReady()
{
    return !gGpuFirstPersonStagedFloorCache.empty()
        && gGpuFirstPersonStagedFloorCacheWidth > 0
        && gGpuFirstPersonStagedFloorCacheHeight > 0
        && gGpuFirstPersonStagedCamera.valid;
}

void screenGpuFirstPersonMarkFrameActive(bool active)
{
    gGpuFirstPersonFrameActive = active;
}

bool screenGpuFirstPersonPresentationEligible()
{
    return gGpuFirstPersonExperimentalMode
        && gGpuFirstPersonShadersReady
        && gGpuFirstPersonSceneReady
        && gGpuFirstPersonFrameActive
        && screenGpuFirstPersonStagedInputsReady();
}

bool gpuFirstPersonPresentStagedFloor()
{
    if (!screenGpuFirstPersonPresentationEligible()
        || gGpuFirstPersonShaderProgram == 0
        || gGpuFirstPersonSceneVertexArray == 0) {
        return false;
    }

    gGlBindFramebuffer(GL_FRAMEBUFFER, 0);
    gGlViewport(0, 0, screenGetWidth(), screenGetHeight());
    gGlClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gGlClear(GL_COLOR_BUFFER_BIT);
    gGlBindVertexArray(gGpuFirstPersonSceneVertexArray);
    gGlUseProgram(gGpuFirstPersonShaderProgram);
    const bool drawn = gpuFirstPersonDrawStagedFloor(
        gGpuFirstPersonStagedCamera.cameraX,
        gGpuFirstPersonStagedCamera.cameraY,
        gGpuFirstPersonStagedCamera.directionX,
        gGpuFirstPersonStagedCamera.directionY,
        gGpuFirstPersonStagedCamera.cameraPlaneX,
        gGpuFirstPersonStagedCamera.cameraPlaneY,
        gGpuFirstPersonStagedCamera.mapWidth,
        gGpuFirstPersonStagedCamera.mapHeight);
    const bool wallsDrawn = drawn && gpuFirstPersonDrawStagedWalls();
    if (drawn && wallsDrawn) {
        gpuFirstPersonDrawStagedBillboards();
        SDL_GL_SwapWindow(gSdlWindow);
    }
    return drawn && wallsDrawn;
}

static void logGpuFirstPersonGlInfo()
{
    if (gGlGetString == nullptr) {
        return;
    }

    const GLubyte* version = gGlGetString(GL_VERSION);
    const GLubyte* shadingLanguageVersion = gGlGetString(GL_SHADING_LANGUAGE_VERSION);
    const GLubyte* vendor = gGlGetString(GL_VENDOR);
    debugPrint("GPU first-person OpenGL: version=%s GLSL=%s vendor=%s\n",
        version != nullptr ? reinterpret_cast<const char*>(version) : "unknown",
        shadingLanguageVersion != nullptr
            ? reinterpret_cast<const char*>(shadingLanguageVersion)
            : "unknown",
        vendor != nullptr ? reinterpret_cast<const char*>(vendor) : "unknown");
}

static bool initializeGpuFirstPersonShaders()
{
    if (!screenHasOpenGLContext()) {
        return false;
    }

    gGlCreateShader = reinterpret_cast<GlCreateShaderProc>(
        SDL_GL_GetProcAddress("glCreateShader"));
    gGlShaderSource = reinterpret_cast<GlShaderSourceProc>(
        SDL_GL_GetProcAddress("glShaderSource"));
    gGlCompileShader = reinterpret_cast<GlCompileShaderProc>(
        SDL_GL_GetProcAddress("glCompileShader"));
    gGlGetShaderiv = reinterpret_cast<GlGetShaderivProc>(
        SDL_GL_GetProcAddress("glGetShaderiv"));
    gGlGetShaderInfoLog = reinterpret_cast<GlGetShaderInfoLogProc>(
        SDL_GL_GetProcAddress("glGetShaderInfoLog"));
    gGlCreateProgram = reinterpret_cast<GlCreateProgramProc>(
        SDL_GL_GetProcAddress("glCreateProgram"));
    gGlAttachShader = reinterpret_cast<GlAttachShaderProc>(
        SDL_GL_GetProcAddress("glAttachShader"));
    gGlLinkProgram = reinterpret_cast<GlLinkProgramProc>(
        SDL_GL_GetProcAddress("glLinkProgram"));
    gGlGetProgramiv = reinterpret_cast<GlGetProgramivProc>(
        SDL_GL_GetProcAddress("glGetProgramiv"));
    gGlGetProgramInfoLog = reinterpret_cast<GlGetProgramInfoLogProc>(
        SDL_GL_GetProcAddress("glGetProgramInfoLog"));
    gGlDeleteShader = reinterpret_cast<GlDeleteShaderProc>(
        SDL_GL_GetProcAddress("glDeleteShader"));
    gGlDeleteProgram = reinterpret_cast<GlDeleteProgramProc>(
        SDL_GL_GetProcAddress("glDeleteProgram"));
    gGlGetString = reinterpret_cast<GlGetStringProc>(
        SDL_GL_GetProcAddress("glGetString"));
    gGlGenTextures = reinterpret_cast<GlGenTexturesProc>(
        SDL_GL_GetProcAddress("glGenTextures"));
    gGlDeleteTextures = reinterpret_cast<GlDeleteTexturesProc>(
        SDL_GL_GetProcAddress("glDeleteTextures"));
    gGlBindTexture = reinterpret_cast<GlBindTextureProc>(
        SDL_GL_GetProcAddress("glBindTexture"));
    gGlTexParameteri = reinterpret_cast<GlTexParameteriProc>(
        SDL_GL_GetProcAddress("glTexParameteri"));
    gGlTexImage2D = reinterpret_cast<GlTexImage2DProc>(
        SDL_GL_GetProcAddress("glTexImage2D"));
    gGlGenFramebuffers = reinterpret_cast<GlGenFramebuffersProc>(
        SDL_GL_GetProcAddress("glGenFramebuffers"));
    gGlDeleteFramebuffers = reinterpret_cast<GlDeleteFramebuffersProc>(
        SDL_GL_GetProcAddress("glDeleteFramebuffers"));
    gGlBindFramebuffer = reinterpret_cast<GlBindFramebufferProc>(
        SDL_GL_GetProcAddress("glBindFramebuffer"));
    gGlFramebufferTexture2D = reinterpret_cast<GlFramebufferTexture2DProc>(
        SDL_GL_GetProcAddress("glFramebufferTexture2D"));
    gGlCheckFramebufferStatus = reinterpret_cast<GlCheckFramebufferStatusProc>(
        SDL_GL_GetProcAddress("glCheckFramebufferStatus"));
    gGlViewport = reinterpret_cast<GlViewportProc>(
        SDL_GL_GetProcAddress("glViewport"));
    gGlGenVertexArrays = reinterpret_cast<GlGenVertexArraysProc>(
        SDL_GL_GetProcAddress("glGenVertexArrays"));
    gGlDeleteVertexArrays = reinterpret_cast<GlDeleteVertexArraysProc>(
        SDL_GL_GetProcAddress("glDeleteVertexArrays"));
    gGlBindVertexArray = reinterpret_cast<GlBindVertexArrayProc>(
        SDL_GL_GetProcAddress("glBindVertexArray"));
    gGlUseProgram = reinterpret_cast<GlUseProgramProc>(
        SDL_GL_GetProcAddress("glUseProgram"));
    gGlClearColor = reinterpret_cast<GlClearColorProc>(
        SDL_GL_GetProcAddress("glClearColor"));
    gGlClear = reinterpret_cast<GlClearProc>(
        SDL_GL_GetProcAddress("glClear"));
    gGlDrawArrays = reinterpret_cast<GlDrawArraysProc>(
        SDL_GL_GetProcAddress("glDrawArrays"));
    gGlGetIntegerv = reinterpret_cast<GlGetIntegervProc>(
        SDL_GL_GetProcAddress("glGetIntegerv"));
    gGlGetFloatv = reinterpret_cast<GlGetFloatvProc>(
        SDL_GL_GetProcAddress("glGetFloatv"));
    gGlGetUniformLocation = reinterpret_cast<GlGetUniformLocationProc>(
        SDL_GL_GetProcAddress("glGetUniformLocation"));
    gGlActiveTexture = reinterpret_cast<GlActiveTextureProc>(
        SDL_GL_GetProcAddress("glActiveTexture"));
    gGlUniform1i = reinterpret_cast<GlUniform1iProc>(
        SDL_GL_GetProcAddress("glUniform1i"));
    gGlUniform1f = reinterpret_cast<GlUniform1fProc>(
        SDL_GL_GetProcAddress("glUniform1f"));
    gGlUniform2f = reinterpret_cast<GlUniform2fProc>(
        SDL_GL_GetProcAddress("glUniform2f"));
    gGlUniform4f = reinterpret_cast<GlUniform4fProc>(
        SDL_GL_GetProcAddress("glUniform4f"));
    gGlEnable = reinterpret_cast<GlEnableProc>(
        SDL_GL_GetProcAddress("glEnable"));
    gGlBlendFunc = reinterpret_cast<GlBlendFuncProc>(
        SDL_GL_GetProcAddress("glBlendFunc"));
    if (gGlCreateShader == nullptr || gGlShaderSource == nullptr
        || gGlCompileShader == nullptr || gGlGetShaderiv == nullptr
        || gGlGetShaderInfoLog == nullptr || gGlCreateProgram == nullptr
        || gGlAttachShader == nullptr || gGlLinkProgram == nullptr
        || gGlGetProgramiv == nullptr || gGlGetProgramInfoLog == nullptr
        || gGlDeleteShader == nullptr || gGlDeleteProgram == nullptr
        || gGlGetString == nullptr || gGlGenTextures == nullptr
        || gGlDeleteTextures == nullptr || gGlBindTexture == nullptr
        || gGlTexParameteri == nullptr || gGlTexImage2D == nullptr
        || gGlGenFramebuffers == nullptr || gGlDeleteFramebuffers == nullptr
        || gGlBindFramebuffer == nullptr || gGlFramebufferTexture2D == nullptr
        || gGlCheckFramebufferStatus == nullptr || gGlViewport == nullptr
        || gGlGenVertexArrays == nullptr || gGlDeleteVertexArrays == nullptr
        || gGlBindVertexArray == nullptr || gGlUseProgram == nullptr
        || gGlClearColor == nullptr || gGlClear == nullptr
        || gGlDrawArrays == nullptr || gGlGetIntegerv == nullptr
        || gGlGetFloatv == nullptr || gGlGetUniformLocation == nullptr
        || gGlActiveTexture == nullptr || gGlUniform1i == nullptr
        || gGlUniform1f == nullptr || gGlUniform2f == nullptr
        || gGlUniform4f == nullptr || gGlEnable == nullptr
        || gGlBlendFunc == nullptr) {
        return false;
    }

    constexpr const char* vertexSource =
        "#version 330 core\n"
        "uniform int uScenePass;\n"
        "uniform vec4 uBillboardRect;\n"
        "uniform vec2 uViewport;\n"
        "out vec2 textureCoordinate;\n"
        "void main() {\n"
        "    if (uScenePass == 2) {\n"
        "        float left = uBillboardRect.x / uViewport.x * 2.0 - 1.0;\n"
        "        float right = (uBillboardRect.x + uBillboardRect.z) / uViewport.x * 2.0 - 1.0;\n"
        "        float top = 1.0 - uBillboardRect.y / uViewport.y * 2.0;\n"
        "        float bottom = 1.0 - (uBillboardRect.y + uBillboardRect.w) / uViewport.y * 2.0;\n"
        "        if (gl_VertexID == 0) { gl_Position = vec4(left, bottom, 0.0, 1.0); textureCoordinate = vec2(0.0, 1.0); }\n"
        "        else if (gl_VertexID == 1) { gl_Position = vec4(right, bottom, 0.0, 1.0); textureCoordinate = vec2(1.0, 1.0); }\n"
        "        else if (gl_VertexID == 2) { gl_Position = vec4(right, top, 0.0, 1.0); textureCoordinate = vec2(1.0, 0.0); }\n"
        "        else if (gl_VertexID == 3) { gl_Position = vec4(left, bottom, 0.0, 1.0); textureCoordinate = vec2(0.0, 1.0); }\n"
        "        else if (gl_VertexID == 4) { gl_Position = vec4(right, top, 0.0, 1.0); textureCoordinate = vec2(1.0, 0.0); }\n"
        "        else { gl_Position = vec4(left, top, 0.0, 1.0); textureCoordinate = vec2(0.0, 0.0); }\n"
        "        return;\n"
        "    }\n"
        "    float top = uScenePass == 1 ? 1.0 : 0.0;\n"
        "    vec2 position;\n"
        "    if (gl_VertexID == 0) position = vec2(-1.0, -1.0);\n"
        "    else if (gl_VertexID == 1) position = vec2(1.0, -1.0);\n"
        "    else if (gl_VertexID == 2) position = vec2(1.0, top);\n"
        "    else if (gl_VertexID == 3) position = vec2(-1.0, -1.0);\n"
        "    else if (gl_VertexID == 4) position = vec2(1.0, top);\n"
        "    else position = vec2(-1.0, top);\n"
        "    gl_Position = vec4(position, 0.0, 1.0);\n"
        "}\n";
    constexpr const char* fragmentSource =
        "#version 330 core\n"
        "uniform sampler2D uFloorCache;\n"
        "uniform vec2 uCamera;\n"
        "uniform vec2 uDirection;\n"
        "uniform vec2 uCameraPlane;\n"
        "uniform vec2 uViewport;\n"
        "uniform float uHorizon;\n"
        "uniform vec2 uMapSize;\n"
        "uniform sampler2D uWallColumns;\n"
        "uniform sampler2D uBillboardTexture;\n"
        "uniform vec4 uBillboardRect;\n"
        "uniform vec4 uBillboardClip;\n"
        "uniform int uScenePass;\n"
        "in vec2 textureCoordinate;\n"
        "out vec4 fragmentColor;\n"
        "void main() {\n"
        "    float screenY = uViewport.y - gl_FragCoord.y;\n"
        "    if (uScenePass == 2) {\n"
        "        if (gl_VertexID == 0 || gl_VertexID == 3) textureCoordinate = vec2(0.0, 1.0);\n"
        "        else if (gl_VertexID == 1 || gl_VertexID == 4) textureCoordinate = vec2(1.0, 1.0);\n"
        "        else textureCoordinate = vec2(1.0, 0.0);\n"
        "        if (gl_FragCoord.x < uBillboardClip.x\n"
        "            || gl_FragCoord.x >= uBillboardClip.x + uBillboardClip.z\n"
        "            || screenY < uBillboardClip.y\n"
        "            || screenY >= uBillboardClip.y + uBillboardClip.w) discard;\n"
        "        fragmentColor = texture(uBillboardTexture, textureCoordinate);\n"
        "        if (fragmentColor.a < 0.01) discard;\n"
        "        return;\n"
        "    }\n"
        "    if (uScenePass == 1) {\n"
        "        int column = int(clamp(gl_FragCoord.x, 0.0, uViewport.x - 1.0));\n"
        "        vec4 wall = texelFetch(uWallColumns, ivec2(column, 0), 0);\n"
        "        if (wall.a < 0.5 || screenY < wall.r || screenY > wall.g) discard;\n"
        "        float shade = clamp(wall.b, 0.0, 1.0);\n"
        "        fragmentColor = vec4(vec3(0.42, 0.34, 0.24) * shade, 1.0);\n"
        "        return;\n"
        "    }\n"
        "    float distanceFromHorizon = screenY - uHorizon;\n"
        "    if (distanceFromHorizon <= 0.0) discard;\n"
        "    float rowDistance = (uViewport.y * 0.5) / distanceFromHorizon;\n"
        "    float cameraOffset = 2.0 * gl_FragCoord.x / uViewport.x - 1.0;\n"
        "    vec2 ray = uDirection + uCameraPlane * cameraOffset;\n"
        "    vec2 worldPosition = uCamera + ray * rowDistance;\n"
        "    vec2 cacheCoordinate = fract(worldPosition / uMapSize);\n"
        "    cacheCoordinate.y = 1.0 - cacheCoordinate.y;\n"
        "    fragmentColor = texture(uFloorCache, cacheCoordinate);\n"
        "}\n";
    const GLuint vertexShader = gGlCreateShader(GL_VERTEX_SHADER);
    const GLuint fragmentShader = gGlCreateShader(GL_FRAGMENT_SHADER);
    if (vertexShader == 0 || fragmentShader == 0) {
        return false;
    }

    gGlShaderSource(vertexShader, 1, &vertexSource, nullptr);
    gGlShaderSource(fragmentShader, 1, &fragmentSource, nullptr);
    gGlCompileShader(vertexShader);
    gGlCompileShader(fragmentShader);
    GLint vertexCompiled = GL_FALSE;
    GLint fragmentCompiled = GL_FALSE;
    gGlGetShaderiv(vertexShader, GL_COMPILE_STATUS, &vertexCompiled);
    gGlGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &fragmentCompiled);
    if (vertexCompiled != GL_TRUE || fragmentCompiled != GL_TRUE) {
        gGlDeleteShader(vertexShader);
        gGlDeleteShader(fragmentShader);
        return false;
    }

    gGpuFirstPersonShaderProgram = gGlCreateProgram();
    if (gGpuFirstPersonShaderProgram == 0) {
        gGlDeleteShader(vertexShader);
        gGlDeleteShader(fragmentShader);
        return false;
    }
    gGlAttachShader(gGpuFirstPersonShaderProgram, vertexShader);
    gGlAttachShader(gGpuFirstPersonShaderProgram, fragmentShader);
    gGlLinkProgram(gGpuFirstPersonShaderProgram);
    GLint linked = GL_FALSE;
    gGlGetProgramiv(gGpuFirstPersonShaderProgram, GL_LINK_STATUS, &linked);
    gGlDeleteShader(vertexShader);
    gGlDeleteShader(fragmentShader);
    if (linked != GL_TRUE) {
        gGlDeleteProgram(gGpuFirstPersonShaderProgram);
        gGpuFirstPersonShaderProgram = 0;
        return false;
    }

    gGpuFirstPersonFloorCacheLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uFloorCache");
    gGpuFirstPersonCameraLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uCamera");
    gGpuFirstPersonDirectionLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uDirection");
    gGpuFirstPersonCameraPlaneLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uCameraPlane");
    gGpuFirstPersonViewportLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uViewport");
    gGpuFirstPersonHorizonLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uHorizon");
    gGpuFirstPersonMapSizeLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uMapSize");
    gGpuFirstPersonWallColumnsLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uWallColumns");
    gGpuFirstPersonBillboardTextureLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uBillboardTexture");
    gGpuFirstPersonBillboardRectLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uBillboardRect");
    gGpuFirstPersonBillboardClipLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uBillboardClip");
    gGpuFirstPersonScenePassLocation =
        gGlGetUniformLocation(gGpuFirstPersonShaderProgram, "uScenePass");
    if (gGpuFirstPersonFloorCacheLocation < 0
        || gGpuFirstPersonCameraLocation < 0
        || gGpuFirstPersonDirectionLocation < 0
        || gGpuFirstPersonCameraPlaneLocation < 0
        || gGpuFirstPersonViewportLocation < 0
        || gGpuFirstPersonHorizonLocation < 0
        || gGpuFirstPersonMapSizeLocation < 0
        || gGpuFirstPersonWallColumnsLocation < 0
        || gGpuFirstPersonBillboardTextureLocation < 0
        || gGpuFirstPersonBillboardRectLocation < 0
        || gGpuFirstPersonBillboardClipLocation < 0
        || gGpuFirstPersonScenePassLocation < 0) {
        gGlDeleteProgram(gGpuFirstPersonShaderProgram);
        gGpuFirstPersonShaderProgram = 0;
        return false;
    }

    logGpuFirstPersonGlInfo();
    gGpuFirstPersonShadersReady = true;
    return true;
}

static bool initializeGpuFirstPersonScene(int width, int height)
{
    if (!gGpuFirstPersonShadersReady || width <= 0 || height <= 0) {
        return false;
    }

    gGlGenTextures(1, &gGpuFirstPersonSceneTexture);
    gGlGenFramebuffers(1, &gGpuFirstPersonSceneFramebuffer);
    if (gGpuFirstPersonSceneTexture == 0 || gGpuFirstPersonSceneFramebuffer == 0) {
        return false;
    }

    gGlBindTexture(GL_TEXTURE_2D, gGpuFirstPersonSceneTexture);
    gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gGlTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    gGlBindFramebuffer(GL_FRAMEBUFFER, gGpuFirstPersonSceneFramebuffer);
    gGlFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, gGpuFirstPersonSceneTexture, 0);
    const bool complete =
        gGlCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    gGlBindFramebuffer(GL_FRAMEBUFFER, 0);
    gGlBindTexture(GL_TEXTURE_2D, 0);
    if (!complete) {
        return false;
    }

    gGlGenVertexArrays(1, &gGpuFirstPersonSceneVertexArray);
    if (gGpuFirstPersonSceneVertexArray == 0) {
        return false;
    }

    gGpuFirstPersonSceneWidth = width;
    gGpuFirstPersonSceneHeight = height;
    gGpuFirstPersonSceneReady = true;
    return true;
}

static void destroyGpuFirstPersonShaders()
{
    if (gGpuFirstPersonFloorTestTexture != 0 && gGlDeleteTextures != nullptr) {
        gGlDeleteTextures(1, &gGpuFirstPersonFloorTestTexture);
    }
    gGpuFirstPersonFloorTestTexture = 0;
    if (gGpuFirstPersonSceneVertexArray != 0 && gGlDeleteVertexArrays != nullptr) {
        gGlDeleteVertexArrays(1, &gGpuFirstPersonSceneVertexArray);
    }
    gGpuFirstPersonSceneVertexArray = 0;
    if (gGpuFirstPersonSceneFramebuffer != 0 && gGlDeleteFramebuffers != nullptr) {
        gGlDeleteFramebuffers(1, &gGpuFirstPersonSceneFramebuffer);
    }
    if (gGpuFirstPersonSceneTexture != 0 && gGlDeleteTextures != nullptr) {
        gGlDeleteTextures(1, &gGpuFirstPersonSceneTexture);
    }
    gGpuFirstPersonSceneFramebuffer = 0;
    gGpuFirstPersonSceneTexture = 0;
    gGpuFirstPersonSceneWidth = 0;
    gGpuFirstPersonSceneHeight = 0;
    gGpuFirstPersonSceneBound = false;
    if (gGpuFirstPersonFloorCacheTexture != 0 && gGlDeleteTextures != nullptr) {
        gGlDeleteTextures(1, &gGpuFirstPersonFloorCacheTexture);
    }
    gGpuFirstPersonFloorCacheTexture = 0;
    gGpuFirstPersonFloorCacheTextureDirty = false;
    if (gGpuFirstPersonWallColumnTexture != 0 && gGlDeleteTextures != nullptr) {
        gGlDeleteTextures(1, &gGpuFirstPersonWallColumnTexture);
    }
    gGpuFirstPersonWallColumnTexture = 0;
    gGpuFirstPersonStagedWallColumns.clear();
    gGpuFirstPersonStagedWallColumnCount = 0;
    gGpuFirstPersonWallColumnsDirty = false;
    for (const auto& entry : gGpuFirstPersonBillboardTextures) {
        if (entry.second != 0 && gGlDeleteTextures != nullptr) {
            gGlDeleteTextures(1, &entry.second);
        }
    }
    gGpuFirstPersonBillboardTextures.clear();
    if (gGpuFirstPersonShaderProgram != 0 && gGlDeleteProgram != nullptr) {
        gGlDeleteProgram(gGpuFirstPersonShaderProgram);
    }
    gGpuFirstPersonShaderProgram = 0;
    gGpuFirstPersonFloorCacheLocation = -1;
    gGpuFirstPersonCameraLocation = -1;
    gGpuFirstPersonDirectionLocation = -1;
    gGpuFirstPersonCameraPlaneLocation = -1;
    gGpuFirstPersonViewportLocation = -1;
    gGpuFirstPersonHorizonLocation = -1;
    gGpuFirstPersonMapSizeLocation = -1;
    gGpuFirstPersonWallColumnsLocation = -1;
    gGpuFirstPersonBillboardTextureLocation = -1;
    gGpuFirstPersonBillboardRectLocation = -1;
    gGpuFirstPersonBillboardClipLocation = -1;
    gGpuFirstPersonScenePassLocation = -1;
    gGpuFirstPersonShadersReady = false;
    gGpuFirstPersonSceneReady = false;
}

static bool testGpuFirstPersonSceneDraw()
{
    if (!gGpuFirstPersonSceneReady || gGpuFirstPersonSceneVertexArray == 0) {
        return false;
    }

    GLint previousFramebuffer = 0;
    GLint previousActiveTexture = GL_TEXTURE0;
    GLint previousTexture = 0;
    GLint previousVertexArray = 0;
    GLint previousProgram = 0;
    GLint previousViewport[4] {};
    GLfloat previousClearColor[4] {};
    gGlGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
    gGlGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    gGlGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);
    gGlGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    gGlGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    gGlGetIntegerv(GL_VIEWPORT, previousViewport);
    gGlGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    gGlBindFramebuffer(GL_FRAMEBUFFER, gGpuFirstPersonSceneFramebuffer);
    gGlViewport(0, 0, gGpuFirstPersonSceneWidth, gGpuFirstPersonSceneHeight);
    gGlBindVertexArray(gGpuFirstPersonSceneVertexArray);
    gGlUseProgram(gGpuFirstPersonShaderProgram);
    if (gGpuFirstPersonFloorTestTexture == 0) {
        gGlGenTextures(1, &gGpuFirstPersonFloorTestTexture);
        if (gGpuFirstPersonFloorTestTexture == 0) {
            gGlUseProgram(static_cast<GLuint>(previousProgram));
            gGlBindVertexArray(static_cast<GLuint>(previousVertexArray));
            gGlBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
            gGlViewport(previousViewport[0], previousViewport[1],
                previousViewport[2], previousViewport[3]);
            gGlClearColor(previousClearColor[0], previousClearColor[1],
                previousClearColor[2], previousClearColor[3]);
            return false;
        }
        const unsigned char testPixel[4] = { 128, 128, 128, 255 };
        gGlBindTexture(GL_TEXTURE_2D, gGpuFirstPersonFloorTestTexture);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        gGlTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, testPixel);
    }
    gGlActiveTexture(GL_TEXTURE0);
    gGlBindTexture(GL_TEXTURE_2D, gGpuFirstPersonFloorTestTexture);
    gGlUniform1i(gGpuFirstPersonFloorCacheLocation, 0);
    gGlUniform2f(gGpuFirstPersonCameraLocation, 0.0f, 0.0f);
    gGlUniform2f(gGpuFirstPersonDirectionLocation, 0.0f, -1.0f);
    gGlUniform2f(gGpuFirstPersonCameraPlaneLocation, 0.66f, 0.0f);
    gGlUniform2f(gGpuFirstPersonViewportLocation,
        static_cast<GLfloat>(gGpuFirstPersonSceneWidth),
        static_cast<GLfloat>(gGpuFirstPersonSceneHeight));
    gGlUniform1f(gGpuFirstPersonHorizonLocation,
        static_cast<GLfloat>(gGpuFirstPersonSceneHeight / 2));
    gGlUniform2f(gGpuFirstPersonMapSizeLocation, 1.0f, 1.0f);
    gGlClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    gGlClear(GL_COLOR_BUFFER_BIT);
    gGlDrawArrays(GL_TRIANGLES, 0, 6);
    gGlUseProgram(static_cast<GLuint>(previousProgram));
    gGlBindVertexArray(static_cast<GLuint>(previousVertexArray));
    gGlBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
    gGlActiveTexture(static_cast<GLenum>(previousActiveTexture));
    gGlBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFramebuffer));
    gGlViewport(previousViewport[0], previousViewport[1],
        previousViewport[2], previousViewport[3]);
    gGlClearColor(previousClearColor[0], previousClearColor[1],
        previousClearColor[2], previousClearColor[3]);
    return true;
}

bool gpuFirstPersonBeginScene()
{
    if (!gGpuFirstPersonSceneReady || gGpuFirstPersonSceneBound
        || gGpuFirstPersonSceneFramebuffer == 0) {
        return false;
    }

    gGlGetIntegerv(GL_FRAMEBUFFER_BINDING, &gGpuFirstPersonPreviousFramebuffer);
    gGlGetIntegerv(GL_ACTIVE_TEXTURE, &gGpuFirstPersonPreviousActiveTexture);
    gGlGetIntegerv(GL_TEXTURE_BINDING_2D, &gGpuFirstPersonPreviousTexture);
    gGlGetIntegerv(GL_VERTEX_ARRAY_BINDING, &gGpuFirstPersonPreviousVertexArray);
    gGlGetIntegerv(GL_CURRENT_PROGRAM, &gGpuFirstPersonPreviousProgram);
    gGlGetIntegerv(GL_VIEWPORT, gGpuFirstPersonPreviousViewport);
    gGlGetFloatv(GL_COLOR_CLEAR_VALUE, gGpuFirstPersonPreviousClearColor);
    gGlBindFramebuffer(GL_FRAMEBUFFER, gGpuFirstPersonSceneFramebuffer);
    gGlViewport(0, 0, gGpuFirstPersonSceneWidth, gGpuFirstPersonSceneHeight);
    gGpuFirstPersonSceneBound = true;
    return true;
}

void gpuFirstPersonEndScene()
{
    if (!gGpuFirstPersonSceneBound) {
        return;
    }

    gGlBindFramebuffer(GL_FRAMEBUFFER,
        static_cast<GLuint>(gGpuFirstPersonPreviousFramebuffer));
    gGlViewport(gGpuFirstPersonPreviousViewport[0], gGpuFirstPersonPreviousViewport[1],
        gGpuFirstPersonPreviousViewport[2], gGpuFirstPersonPreviousViewport[3]);
    gGlUseProgram(static_cast<GLuint>(gGpuFirstPersonPreviousProgram));
    gGlBindVertexArray(static_cast<GLuint>(gGpuFirstPersonPreviousVertexArray));
    gGlBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(gGpuFirstPersonPreviousTexture));
    gGlActiveTexture(static_cast<GLenum>(gGpuFirstPersonPreviousActiveTexture));
    gGlClearColor(gGpuFirstPersonPreviousClearColor[0],
        gGpuFirstPersonPreviousClearColor[1],
        gGpuFirstPersonPreviousClearColor[2],
        gGpuFirstPersonPreviousClearColor[3]);
    gGpuFirstPersonSceneBound = false;
}

void gpuFirstPersonStageFloorCache(const unsigned char* pixels, int width, int height)
{
    if (pixels == nullptr || width <= 0 || height <= 0) {
        gGpuFirstPersonStagedFloorCache.clear();
        gGpuFirstPersonStagedFloorCacheWidth = 0;
        gGpuFirstPersonStagedFloorCacheHeight = 0;
        return;
    }

    const size_t pixelCount = static_cast<size_t>(width) * height;
    gGpuFirstPersonStagedFloorCache.assign(pixels, pixels + pixelCount);
    gGpuFirstPersonStagedFloorCacheWidth = width;
    gGpuFirstPersonStagedFloorCacheHeight = height;
    gGpuFirstPersonFloorCacheTextureDirty = true;
}

void gpuFirstPersonInvalidateFloorCachePalette()
{
    gGpuFirstPersonFloorCacheTextureDirty = true;
}

void gpuFirstPersonStageCamera(double cameraX, double cameraY,
    double directionX, double directionY, double cameraPlaneX, double cameraPlaneY,
    double mapWidth, double mapHeight)
{
    if (mapWidth <= 0.0 || mapHeight <= 0.0) {
        gGpuFirstPersonStagedCamera.valid = false;
        return;
    }

    gGpuFirstPersonStagedCamera.cameraX = cameraX;
    gGpuFirstPersonStagedCamera.cameraY = cameraY;
    gGpuFirstPersonStagedCamera.directionX = directionX;
    gGpuFirstPersonStagedCamera.directionY = directionY;
    gGpuFirstPersonStagedCamera.cameraPlaneX = cameraPlaneX;
    gGpuFirstPersonStagedCamera.cameraPlaneY = cameraPlaneY;
    gGpuFirstPersonStagedCamera.mapWidth = mapWidth;
    gGpuFirstPersonStagedCamera.mapHeight = mapHeight;
    gGpuFirstPersonStagedCamera.valid = true;
}

void gpuFirstPersonStageWallColumns(const float* columns, int width)
{
    if (columns == nullptr || width <= 0) {
        gGpuFirstPersonStagedWallColumns.clear();
        gGpuFirstPersonStagedWallColumnCount = 0;
        gGpuFirstPersonWallColumnsDirty = false;
        return;
    }

    gGpuFirstPersonStagedWallColumns.assign(columns, columns + width * 4);
    gGpuFirstPersonStagedWallColumnCount = width;
    gGpuFirstPersonWallColumnsDirty = true;
}

bool gpuFirstPersonDrawStagedFloor(double cameraX, double cameraY,
    double directionX, double directionY, double cameraPlaneX, double cameraPlaneY,
    double mapWidth, double mapHeight)
{
    if (!gGpuFirstPersonSceneBound && !gGpuFirstPersonSceneBackendEnabled
        || gGpuFirstPersonShaderProgram == 0
        || gGpuFirstPersonSceneVertexArray == 0
        || gGpuFirstPersonStagedFloorCache.empty()
        || gGpuFirstPersonStagedFloorCacheWidth <= 0
        || gGpuFirstPersonStagedFloorCacheHeight <= 0
        || mapWidth <= 0.0 || mapHeight <= 0.0) {
        return false;
    }

    if (gGpuFirstPersonFloorCacheTexture == 0) {
        gGlGenTextures(1, &gGpuFirstPersonFloorCacheTexture);
        if (gGpuFirstPersonFloorCacheTexture == 0) {
            return false;
        }
        gGpuFirstPersonFloorCacheTextureDirty = true;
    }

    gGlActiveTexture(GL_TEXTURE0);
    gGlBindTexture(GL_TEXTURE_2D, gGpuFirstPersonFloorCacheTexture);
    if (gGpuFirstPersonFloorCacheTextureDirty) {
        unsigned char* palette = directDrawGetPalette();
        std::vector<unsigned char> rgba(
            gGpuFirstPersonStagedFloorCache.size() * 4);
        for (size_t index = 0; index < gGpuFirstPersonStagedFloorCache.size(); index++) {
            const size_t paletteIndex =
                static_cast<size_t>(gGpuFirstPersonStagedFloorCache[index]) * 3;
            rgba[index * 4] = palette[paletteIndex] << 2;
            rgba[index * 4 + 1] = palette[paletteIndex + 1] << 2;
            rgba[index * 4 + 2] = palette[paletteIndex + 2] << 2;
            rgba[index * 4 + 3] = 255;
        }
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gGlTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
            gGpuFirstPersonStagedFloorCacheWidth,
            gGpuFirstPersonStagedFloorCacheHeight, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        gGpuFirstPersonFloorCacheTextureDirty = false;
    }

    gGlBindVertexArray(gGpuFirstPersonSceneVertexArray);
    gGlUseProgram(gGpuFirstPersonShaderProgram);
    gGlUniform1i(gGpuFirstPersonScenePassLocation, 0);
    gGlUniform1i(gGpuFirstPersonFloorCacheLocation, 0);
    gGlUniform2f(gGpuFirstPersonCameraLocation,
        static_cast<GLfloat>(cameraX), static_cast<GLfloat>(cameraY));
    gGlUniform2f(gGpuFirstPersonDirectionLocation,
        static_cast<GLfloat>(directionX), static_cast<GLfloat>(directionY));
    gGlUniform2f(gGpuFirstPersonCameraPlaneLocation,
        static_cast<GLfloat>(cameraPlaneX), static_cast<GLfloat>(cameraPlaneY));
    gGlUniform2f(gGpuFirstPersonViewportLocation,
        static_cast<GLfloat>(gGpuFirstPersonSceneWidth),
        static_cast<GLfloat>(gGpuFirstPersonSceneHeight));
    gGlUniform1f(gGpuFirstPersonHorizonLocation,
        static_cast<GLfloat>(gGpuFirstPersonSceneHeight / 2));
    gGlUniform2f(gGpuFirstPersonMapSizeLocation,
        static_cast<GLfloat>(mapWidth), static_cast<GLfloat>(mapHeight));
    gGlDrawArrays(GL_TRIANGLES, 0, 6);
    return true;
}

static bool gpuFirstPersonDrawStagedWalls()
{
    if (gGpuFirstPersonStagedWallColumns.empty()
        || gGpuFirstPersonStagedWallColumnCount <= 0) {
        return false;
    }

    if (gGpuFirstPersonWallColumnTexture == 0) {
        gGlGenTextures(1, &gGpuFirstPersonWallColumnTexture);
        gGpuFirstPersonWallColumnsDirty = true;
    }
    gGlActiveTexture(GL_TEXTURE1);
    gGlBindTexture(GL_TEXTURE_2D, gGpuFirstPersonWallColumnTexture);
    if (gGpuFirstPersonWallColumnsDirty) {
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gGlTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gGlTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F,
            gGpuFirstPersonStagedWallColumnCount, 1, 0,
            GL_RGBA, GL_FLOAT, gGpuFirstPersonStagedWallColumns.data());
        gGpuFirstPersonWallColumnsDirty = false;
    }

    gGlUseProgram(gGpuFirstPersonShaderProgram);
    gGlUniform1i(gGpuFirstPersonWallColumnsLocation, 1);
    gGlUniform1i(gGpuFirstPersonScenePassLocation, 1);
    gGlUniform2f(gGpuFirstPersonViewportLocation,
        static_cast<GLfloat>(gGpuFirstPersonSceneWidth),
        static_cast<GLfloat>(gGpuFirstPersonSceneHeight));
    gGlUniform1f(gGpuFirstPersonHorizonLocation,
        static_cast<GLfloat>(gGpuFirstPersonSceneHeight / 2));
    gGlDrawArrays(GL_TRIANGLES, 0, 6);
    gGlUniform1i(gGpuFirstPersonScenePassLocation, 0);
    return true;
}

static void gpuFirstPersonDrawStagedBillboards()
{
    if (gGpuBillboards.empty()) {
        return;
    }

    std::stable_sort(gGpuBillboards.begin(), gGpuBillboards.end(),
        [](const GpuBillboard& lhs, const GpuBillboard& rhs) {
            return lhs.depth > rhs.depth;
        });
    gGlUseProgram(gGpuFirstPersonShaderProgram);
    gGlUniform1i(gGpuFirstPersonScenePassLocation, 2);
    gGlUniform1i(gGpuFirstPersonBillboardTextureLocation, 2);
    gGlUniform2f(gGpuFirstPersonViewportLocation,
        static_cast<GLfloat>(gGpuFirstPersonSceneWidth),
        static_cast<GLfloat>(gGpuFirstPersonSceneHeight));
    gGlEnable(GL_BLEND);
    gGlBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (const GpuBillboard& billboard : gGpuBillboards) {
        if (billboard.glTexture == 0) {
            continue;
        }
        gGlActiveTexture(GL_TEXTURE2);
        gGlBindTexture(GL_TEXTURE_2D, billboard.glTexture);
        gGlUniform4f(gGpuFirstPersonBillboardRectLocation,
            static_cast<GLfloat>(billboard.destination.x),
            static_cast<GLfloat>(billboard.destination.y),
            static_cast<GLfloat>(billboard.destination.w),
            static_cast<GLfloat>(billboard.destination.h));
        gGlUniform4f(gGpuFirstPersonBillboardClipLocation,
            static_cast<GLfloat>(billboard.clip.x),
            static_cast<GLfloat>(billboard.clip.y),
            static_cast<GLfloat>(billboard.clip.w),
            static_cast<GLfloat>(billboard.clip.h));
        gGlDrawArrays(GL_TRIANGLES, 0, 6);
    }
    gGlUniform1i(gGpuFirstPersonScenePassLocation, 0);
}

void gpuBillboardBeginFrame()
{
    gGpuBillboards.clear();
    gGpuHasOverlayRect = false;
    const size_t expectedBillboardCount =
        static_cast<size_t>(std::max(64, screenGetWidth() / 2));
    if (gGpuBillboards.capacity() < expectedBillboardCount) {
        gGpuBillboards.reserve(expectedBillboardCount);
    }
    const unsigned int previousPaletteHash = gGpuPaletteHash;
    unsigned char* palette = directDrawGetPalette();
    gGpuPaletteHash = 2166136261u;
    for (int index = 0; index < 256 * 3; index++) {
        gGpuPalette[index] = palette[index];
        gGpuPaletteHash ^= palette[index];
        gGpuPaletteHash *= 16777619u;
    }
    if (screenUsesGpuFirstPersonScene() && previousPaletteHash != 0
        && previousPaletteHash != gGpuPaletteHash) {
        for (const auto& entry : gGpuFirstPersonBillboardTextures) {
            if (entry.second != 0) {
                gGlDeleteTextures(1, &entry.second);
            }
        }
        gGpuFirstPersonBillboardTextures.clear();
    }
}

void gpuBillboardMarkSceneComplete()
{
    gGpuHasOverlayRect = false;
}

void gpuBillboardClear()
{
    gGpuBillboards.clear();
    gGpuHasOverlayRect = false;
}

void gpuBillboardSubmit(const unsigned char* pixels, int width, int height,
    const SDL_Rect& destination, const SDL_Rect& clip, double depth)
{
    if ((!gSdlRendererHardwareAccelerated && !screenUsesGpuFirstPersonScene())
        || pixels == nullptr
        || width <= 0 || height <= 0
        || destination.w <= 0 || destination.h <= 0) {
        return;
    }

    GpuBillboardTexture* cachedTexture = nullptr;
    GLuint glTexture = 0;

    if (screenUsesGpuFirstPersonScene()) {
        auto it = gGpuFirstPersonBillboardTextures.find(pixels);
        if (it != gGpuFirstPersonBillboardTextures.end()) {
            glTexture = it->second;
        } else {
            // Convert source pixels to RGBA32. Adjust channel count to your source format.
            const int srcChannels = 4; // or 3 — must match what `pixels` actually holds
            std::vector<uint32_t> rgba(static_cast<size_t>(width) * height);
            for (size_t i = 0; i < rgba.size(); ++i) {
                const unsigned char* s = pixels + i * srcChannels;
                uint8_t r = s[0], g = s[1], b = s[2];
                uint8_t a = (srcChannels == 4) ? s[3] : 0xFF;
                rgba[i] = (uint32_t(a) << 24) | (uint32_t(b) << 16)
                        | (uint32_t(g) << 8)  |  uint32_t(r); // GL_RGBA, little-endian
            }

            glGenTextures(1, &glTexture);
            glBindTexture(GL_TEXTURE_2D, glTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

            gGpuFirstPersonBillboardTextures[pixels] = glTexture;
        }
    } else {
        // Non-GL hardware path: fill cachedTexture here (e.g., from a texture cache).
        // cachedTexture = ...;
    }

    // Bind + draw using glTexture or cachedTexture, honoring clip and depth.
    // ...
}

// 0x4CAD40 init_mode_320_400
int _init_mode_320_400()
{
    return _GNW95_init_mode_ex(320, 400, 8);
}

// 0x4CAD5C init_mode_640_480_16
int _init_mode_640_480_16()
{
    return -1;
}

// 0x4CAD64 init_mode_640_480
int _init_mode_640_480()
{
    return _init_vesa_mode(640, 480);
}

// 0x4CAD94 init_mode_640_400
int _init_mode_640_400()
{
    return _init_vesa_mode(640, 400);
}

// 0x4CADA8 init_mode_800_600
int _init_mode_800_600()
{
    return _init_vesa_mode(800, 600);
}

// 0x4CADBC init_mode_1024_768
int _init_mode_1024_768()
{
    return _init_vesa_mode(1024, 768);
}

// 0x4CADD0 init_mode_1280_1024
int _init_mode_1280_1024()
{
    return _init_vesa_mode(1280, 1024);
}

// 0x4CADF8
void _get_start_mode_()
{
}

// 0x4CADFC zero_vid_mem
void _zero_vid_mem()
{
    if (_zero_mem) {
        _zero_mem();
    }
}

// 0x4CAE1C GNW95_init_mode_ex
int _GNW95_init_mode_ex(int width, int height, int bpp)
{
    width = settings.screen.resolution_x;
    height = settings.screen.resolution_y;
    int scale = settings.screen.scale;

    // Only allow scaling if resulting game resolution is >= 640x480
    if ((width / scale) < 640 || (height / scale) < 480) {
        scale = 1;
    } else {
        width /= scale;
        height /= scale;
    }

    if (_GNW95_init_window(width, height, settings.screen.windowed, scale) == -1) {
        return -1;
    }

    if (directDrawInit(width, height, bpp) == -1) {
        return -1;
    }

    // macOS seems to require dequeuing NSApp events in order for window to
    // become visible. There is no concrete number of calls required to make
    // it happen. Sadly there is no particular event to watch for because SDL
    // marks window as shown immediately after creation (see
    // `SDL_FinishWindowCreation`).
    for (int i = 0; i < 10; i++) {
        SDL_PumpEvents();
    }

    _scr_size.left = 0;
    _scr_size.top = 0;
    _scr_size.right = width - 1;
    _scr_size.bottom = height - 1;

    _mouse_blit_trans = nullptr;
    _scr_blit = _GNW95_ShowRect;
    _zero_mem = _GNW95_zero_vid_mem;
    _mouse_blit = _GNW95_ShowRect;

    return 0;
}

// 0x4CAECC init_vesa_mode
int _init_vesa_mode(int width, int height)
{
    return _GNW95_init_mode_ex(width, height, 8);
}

// 0x4CAEDC GNW95_init_window
int _GNW95_init_window(int width, int height, WindowMode mode, int scale)
{
    if (gSdlWindow == nullptr) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

        Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

        if (mode == WindowMode::Fullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN;
        } else if (mode == WindowMode::WindowedFullscreen) {
            windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }

        gSdlWindow = SDL_CreateWindow(gProgramWindowTitle, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width * scale, height * scale, windowFlags);
        if (gSdlWindow == nullptr) {
            return -1;
        }

        if (!createRenderer(width, height)) {
            destroyRenderer();

            SDL_DestroyWindow(gSdlWindow);
            gSdlWindow = nullptr;

            return -1;
        }
    }

    return 0;
}

// 0x4CAF9C GNW95_init_DirectDraw
int directDrawInit(int width, int height, int bpp)
{
    if (gSdlSurface != nullptr) {
        unsigned char* palette = directDrawGetPalette();
        directDrawFree();

        if (directDrawInit(width, height, bpp) == -1) {
            return -1;
        }

        directDrawSetPalette(palette);

        return 0;
    }

    gSdlSurface = SDL_CreateRGBSurface(0, width, height, bpp, 0, 0, 0, 0);
    if (gSdlSurface == nullptr || gSdlSurface->format->palette == nullptr) {
        directDrawFree();
        return -1;
    }

    SDL_Color colors[256];
    for (int index = 0; index < 256; index++) {
        colors[index].r = index;
        colors[index].g = index;
        colors[index].b = index;
        colors[index].a = 255;
    }

    SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);

    return 0;
}

// 0x4CB1B0 GNW95_reset_mode
void directDrawFree()
{
    if (gSdlSurface != nullptr) {
        SDL_FreeSurface(gSdlSurface);
        gSdlSurface = nullptr;
    }
}

// 0x4CB310 GNW95_SetPaletteEntries
void directDrawSetPaletteInRange(unsigned char* palette, int start, int count)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        if (count != 0) {
            for (int index = 0; index < count; index++) {
                colors[index].r = palette[index * 3] << 2;
                colors[index].g = palette[index * 3 + 1] << 2;
                colors[index].b = palette[index * 3 + 2] << 2;
                colors[index].a = 255;
            }
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, start, count);
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
        gpuFirstPersonInvalidateFloorCachePalette();
    }
}

// 0x4CB568 GNW95_SetPalette
void directDrawSetPalette(unsigned char* palette)
{
    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color colors[256];

        for (int index = 0; index < 256; index++) {
            colors[index].r = palette[index * 3] << 2;
            colors[index].g = palette[index * 3 + 1] << 2;
            colors[index].b = palette[index * 3 + 2] << 2;
            colors[index].a = 255;
        }

        SDL_SetPaletteColors(gSdlSurface->format->palette, colors, 0, 256);
        SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
        gpuFirstPersonInvalidateFloorCachePalette();
    }
}

// 0x4CB68C GNW95_GetPalette
unsigned char* directDrawGetPalette()
{
    // 0x6ACA24
    static unsigned char palette[768];

    if (gSdlSurface != nullptr && gSdlSurface->format->palette != nullptr) {
        SDL_Color* colors = gSdlSurface->format->palette->colors;

        for (int index = 0; index < 256; index++) {
            SDL_Color* color = &(colors[index]);
            palette[index * 3] = color->r >> 2;
            palette[index * 3 + 1] = color->g >> 2;
            palette[index * 3 + 2] = color->b >> 2;
        }
    }

    return palette;
}

// 0x4CB850 GNW95_ShowRect
void _GNW95_ShowRect(unsigned char* src, int srcPitch, int unused, int srcX, int srcY, int srcWidth, int srcHeight, int destX, int destY)
{
    (void)unused;

    blitBufferToBuffer(src + srcPitch * srcY + srcX, srcWidth, srcHeight, srcPitch, (unsigned char*)gSdlSurface->pixels + gSdlSurface->pitch * destY + destX, gSdlSurface->pitch);

    SDL_Rect srcRect;
    srcRect.x = destX;
    srcRect.y = destY;
    srcRect.w = srcWidth;
    srcRect.h = srcHeight;

    SDL_Rect destRect;
    destRect.x = destX;
    destRect.y = destY;
    SDL_BlitSurface(gSdlSurface, &srcRect, gSdlTextureSurface, &destRect);

    if (gSdlRendererHardwareAccelerated) {
        if (!gGpuHasOverlayRect) {
            gGpuOverlayRect = destRect;
            gGpuHasOverlayRect = true;
        } else {
            const int right = std::max(gGpuOverlayRect.x + gGpuOverlayRect.w,
                destRect.x + destRect.w);
            const int bottom = std::max(gGpuOverlayRect.y + gGpuOverlayRect.h,
                destRect.y + destRect.h);
            gGpuOverlayRect.x = std::min(gGpuOverlayRect.x, destRect.x);
            gGpuOverlayRect.y = std::min(gGpuOverlayRect.y, destRect.y);
            gGpuOverlayRect.w = right - gGpuOverlayRect.x;
            gGpuOverlayRect.h = bottom - gGpuOverlayRect.y;
        }
    }
}

// Clears drawing surface.
//
// 0x4CBBC8 GNW95_zero_vid_mem
void _GNW95_zero_vid_mem()
{
    if (!gProgramIsActive) {
        return;
    }

    unsigned char* surface = (unsigned char*)gSdlSurface->pixels;
    for (int y = 0; y < gSdlSurface->h; y++) {
        memset(surface, 0, gSdlSurface->w);
        surface += gSdlSurface->pitch;
    }

    SDL_BlitSurface(gSdlSurface, nullptr, gSdlTextureSurface, nullptr);
}

int screenGetWidth()
{
    // TODO: Make it on par with _xres;
    return rectGetWidth(&_scr_size);
}

int screenGetHeight()
{
    // TODO: Make it on par with _yres.
    return rectGetHeight(&_scr_size);
}

int screenGetVisibleHeight()
{
    int windowBottomMargin = 0;

    if (!settings.ui.iface_bar_mode) {
        windowBottomMargin = INTERFACE_BAR_HEIGHT;
    }
    return screenGetHeight() - windowBottomMargin;
}

// returns true if the game is running in fullscreen mode, false otherwise (including windowed fullscreen mode)
bool screenIsExclusiveFullscreen()
{
    Uint32 flags = SDL_GetWindowFlags(gSdlWindow);
    return (flags & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN;
}

static bool createRenderer(int width, int height)
{
    gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, SDL_RENDERER_ACCELERATED);
    if (gSdlRenderer == nullptr) {
        debugPrint("Accelerated SDL renderer unavailable; using fallback renderer: %s\n",
            SDL_GetError());
        gSdlRenderer = SDL_CreateRenderer(gSdlWindow, -1, 0);
    }
    if (gSdlRenderer == nullptr) {
        return false;
    }

    SDL_RendererInfo rendererInfo {};
    if (SDL_GetRendererInfo(gSdlRenderer, &rendererInfo) == 0) {
        gSdlRendererHardwareAccelerated =
            (rendererInfo.flags & SDL_RENDERER_ACCELERATED) != 0;
        debugPrint("SDL renderer: %s (%s), target textures: %s\n",
            rendererInfo.name != nullptr ? rendererInfo.name : "unknown",
            gSdlRendererHardwareAccelerated ? "accelerated" : "software",
            (rendererInfo.flags & SDL_RENDERER_TARGETTEXTURE) != 0 ? "yes" : "no");
        debugPrint("GPU first-person backend: %s\n",
            screenSupportsGpuFirstPersonPass() && screenHasOpenGLContext()
                ? "available for isolated shader integration"
                : "CPU fallback");
    }

    if (SDL_RenderSetLogicalSize(gSdlRenderer, width, height) != 0) {
        return false;
    }

    gSdlTexture = SDL_CreateTexture(gSdlRenderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (gSdlTexture == nullptr) {
        return false;
    }

    Uint32 format;
    if (SDL_QueryTexture(gSdlTexture, &format, nullptr, nullptr, nullptr) != 0) {
        return false;
    }

    gSdlTextureSurface = SDL_CreateRGBSurfaceWithFormat(0, width, height, SDL_BITSPERPIXEL(format), format);
    if (gSdlTextureSurface == nullptr) {
        return false;
    }

    const bool shadersReady = initializeGpuFirstPersonShaders();
    const bool sceneTargetReady = shadersReady
        && initializeGpuFirstPersonScene(width, height);
    const bool sceneDrawReady = sceneTargetReady && testGpuFirstPersonSceneDraw();
    debugPrint("GPU first-person shader probe: %s\n",
        shadersReady ? "available" : "unavailable");
    debugPrint("GPU first-person scene target: %s\n",
        sceneTargetReady ? "available" : "unavailable");
    debugPrint("GPU first-person scene draw test: %s\n",
        sceneDrawReady ? "passed" : "failed");
    debugPrint("GPU first-person scene backend: disabled until full composition is wired\n");
    return true;
}

static void destroyRenderer()
{
    destroyGpuFirstPersonShaders();
    gGpuBillboards.clear();
    for (const auto& entry : gGpuBillboardTextures) {
        SDL_DestroyTexture(entry.second.texture);
    }
    gGpuBillboardTextures.clear();
    gSdlRendererHardwareAccelerated = false;
    if (gSdlTextureSurface != nullptr) {
        SDL_FreeSurface(gSdlTextureSurface);
        gSdlTextureSurface = nullptr;
    }

    if (gSdlTexture != nullptr) {
        SDL_DestroyTexture(gSdlTexture);
        gSdlTexture = nullptr;
    }

    if (gSdlRenderer != nullptr) {
        SDL_DestroyRenderer(gSdlRenderer);
        gSdlRenderer = nullptr;
    }
}

void handleWindowSizeChanged()
{
    movieHandleRendererReset();
    destroyRenderer();
    if (createRenderer(screenGetWidth(), screenGetHeight())) {
        // Repaint the newly created texture surface, just as on a window expose.
        windowRefreshAll(&_scr_size);
    }
    mouseDeviceRefreshWindowMapping();
}

void renderFpsCounter()
{
    if (!settings.debug.show_fps || gSdlSurface == nullptr || gSdlTextureSurface == nullptr) {
        return;
    }

    static unsigned int sampleStartTicks = 0;
    static int sampleFrames = 0;
    static double fps = 0.0;

    unsigned int now = SDL_GetTicks();
    if (sampleStartTicks == 0) {
        sampleStartTicks = now;
    }

    sampleFrames++;

    unsigned int elapsed = now - sampleStartTicks;
    if (elapsed >= 500) {
        fps = sampleFrames * 1000.0 / elapsed;
        sampleFrames = 0;
        sampleStartTicks = now;
    }

    char text[32];
    snprintf(text, sizeof(text), "FPS: %.1f", fps);

    ScopedFont font(101);

    constexpr int kPadding = 2;
    int textWidth = fontGetStringWidth(text);
    int textHeight = fontGetLineHeight();
    int width = textWidth + kPadding * 2;
    int height = textHeight + kPadding * 2;

    if (width > gSdlSurface->w) {
        width = gSdlSurface->w;
    }

    if (height > gSdlSurface->h) {
        height = gSdlSurface->h;
    }

    bufferFill(static_cast<unsigned char*>(gSdlSurface->pixels), width, height, gSdlSurface->pitch, COLOR_BLACK);
    if (width > kPadding * 2 && height > kPadding * 2) {
        fontDrawText(static_cast<unsigned char*>(gSdlSurface->pixels) + gSdlSurface->pitch * kPadding + kPadding, text, width - kPadding * 2, gSdlSurface->pitch, COLOR_LIGHT_GREY);
    }

    SDL_Rect rect;
    rect.x = 0;
    rect.y = 0;
    rect.w = width;
    rect.h = height;
    SDL_BlitSurface(gSdlSurface, &rect, gSdlTextureSurface, &rect);
}

void renderPresent()
{
    if (gGpuFirstPersonSceneBackendEnabled) {
        if (gpuFirstPersonPresentStagedFloor()) {
            return;
        }

        debugPrint("GPU first-person OpenGL mode: presentation failed; restoring SDL\n");
        gGpuFirstPersonExperimentalMode = false;
        gGpuFirstPersonSceneBackendEnabled = false;
        destroyGpuFirstPersonShaders();
        SDL_GL_DeleteContext(gGpuFirstPersonContext);
        gGpuFirstPersonContext = nullptr;
        if (!createRenderer(screenGetWidth(), screenGetHeight())) {
            return;
        }
        windowRefreshAll(&_scr_size);
    }

    static bool loggedExperimentalStatus = false;
    if (gGpuFirstPersonExperimentalMode && !loggedExperimentalStatus) {
        debugPrint("GPU first-person experimental mode: renderer gate active "
                   "(shaders=%s target=%s inputs=%s backend=disabled)\n",
            gGpuFirstPersonShadersReady ? "ready" : "unavailable",
            gGpuFirstPersonSceneReady ? "ready" : "unavailable",
            screenGpuFirstPersonStagedInputsReady() && gGpuFirstPersonFrameActive
                ? "ready"
                : "unavailable");
        debugPrint("GPU first-person experimental mode: presentation %s "
                   "(backend=disabled)\n",
            screenGpuFirstPersonPresentationEligible() ? "eligible" : "waiting");
        loggedExperimentalStatus = true;
    } else if (!gGpuFirstPersonExperimentalMode) {
        loggedExperimentalStatus = false;
    }

    SDL_UpdateTexture(gSdlTexture, nullptr, gSdlTextureSurface->pixels, gSdlTextureSurface->pitch);
    SDL_RenderClear(gSdlRenderer);
    SDL_RenderCopy(gSdlRenderer, gSdlTexture, nullptr, nullptr);
    std::stable_sort(gGpuBillboards.begin(), gGpuBillboards.end(),
        [](const GpuBillboard& lhs, const GpuBillboard& rhs) {
            return lhs.depth > rhs.depth;
        });
    SDL_Rect activeClip {};
    bool hasActiveClip = false;
    for (const GpuBillboard& billboard : gGpuBillboards) {
        const bool clipChanged = !hasActiveClip
            || activeClip.x != billboard.clip.x
            || activeClip.y != billboard.clip.y
            || activeClip.w != billboard.clip.w
            || activeClip.h != billboard.clip.h;
        if (clipChanged) {
            SDL_RenderSetClipRect(gSdlRenderer, &billboard.clip);
            activeClip = billboard.clip;
            hasActiveClip = true;
        }
        SDL_RenderCopy(gSdlRenderer, billboard.texture, nullptr, &billboard.destination);
    }
    if (hasActiveClip) {
        SDL_RenderSetClipRect(gSdlRenderer, nullptr);
    }
    if (gGpuHasOverlayRect) {
        SDL_RenderCopy(gSdlRenderer, gSdlTexture, &gGpuOverlayRect, &gGpuOverlayRect);
    }
    // render movie SDL texture if present
    movieRenderDirectOverlay();
    SDL_RenderPresent(gSdlRenderer);
}

} // namespace fallout
