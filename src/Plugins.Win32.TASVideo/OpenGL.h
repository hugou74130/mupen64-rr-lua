#pragma once

#include "gSP.h"

struct GLVertex
{
    float x, y, z, w;

    struct
    {
        float r, g, b, a;
    } color, secondaryColor;

    float s0, t0, s1, t1;
    float fog;
};

struct GLInfo
{
    BOOL recycle_context;
    BOOL context_initialized;
    HGLRC hRC, hPbufferRC;
    HDC hDC, hPbufferDC;
    HWND hWnd;
    HPBUFFERARB hPbuffer;

    DWORD width, height, windowedWidth, windowedHeight, heightOffset;

    BOOL forceBilinear, fog;

    float scaleX, scaleY;

    BOOL ARB_multitexture;         // TNT, GeForce, Rage 128, Radeon
    BOOL ARB_texture_env_combine;  // GeForce, Rage 128, Radeon
    BOOL ARB_texture_env_crossbar; // Radeon (GeForce supports it, but doesn't report it)

    BOOL EXT_fog_coord;           // TNT, GeForce, Rage 128, Radeon
    BOOL EXT_texture_env_combine; // TNT, GeForce, Rage 128, Radeon
    BOOL EXT_secondary_color;     // GeForce, Radeon

    BOOL ARB_buffer_region;
    BOOL ARB_pbuffer;
    BOOL ARB_render_texture;
    BOOL ARB_pixel_format;

    int maxTextureUnits; // TNT = 2, GeForce = 2-4, Rage 128 = 2, Radeon = 3-6

    TextureFilter textureFilter = TextureFilter::None;
    float originAdjust;
    // 2xSAI: 2
    // xBRZ: 2, 3, 4, 5, 6
    // Hqx: 2, 3, 4
    int filterScale = 4;
    BOOL filterChanged = FALSE; // for cache

    GLVertex vertices[256];
    BYTE triangles[80][3];
    BYTE numTriangles;
    BYTE numVertices;
    HWND hFullscreenWnd;

    BOOL usePolygonStipple;
    GLubyte stipplePattern[32][8][128];
    BYTE lastStipple;

    BOOL ignoreScissor;

    // Clears the game with black color every frame regardless of what N64 asks
    BOOL clear_override = TRUE;
};

extern GLInfo OGL;

struct GLcolor
{
    float r{}, g{}, b{}, a{};
};

void OGL_ReadPixels();
bool OGL_Start();
void OGL_Stop();
void OGL_AddTriangle(SPVertex *vertices, int v0, int v1, int v2);
void OGL_DrawTriangles();
void OGL_DrawLine(SPVertex *vertices, int v0, int v1, float width);
void OGL_DrawRect(int ulx, int uly, int lrx, int lry, float *color);
void OGL_DrawTexturedRect(float ulx, float uly, float lrx, float lry, float uls, float ult, float lrs, float lrt,
                          bool flip);
void OGL_UpdateScale();
void OGL_ClearDepthBuffer();
void OGL_ClearColorBuffer(float *color);
void OGL_ResizeWindow();

// Core OpenGL primitive helpers (lines, rects, textured rects)
void OGL_InitPrimitiveResources();
void OGL_DestroyPrimitiveResources();

// Core OpenGL N64 pipeline helpers (3D rendering)
void OGL_InitN64Resources();
void OGL_DestroyN64Resources();

// Core OpenGL blit helpers (for FrameBuffer.cpp)
void OGL_InitBlitResources();
void OGL_DestroyBlitResources();
void OGL_BlitTexture(GLuint texture, float x, float y, float w, float h, float u1, float v1);

// N64 combiner uniform state (replaces fixed-function glTexEnv)
struct N64CombinerState
{
    float primColor[4];
    float envColor[4];
    float primLODFrac;
    float fogColor[4];
    int fogEnabled;
    float fogMultiplier;
    float fogOffset;
    int alphaTestEnabled;
    float alphaTestThreshold;
    int alphaTestFunction;
    int stippleEnabled;
    int stippleAlpha;
    int stipplePattern;
    unsigned int stippleBits[32];
    // Cycle 0 RGB: (A - B) * C + D
    int saRGB0, sbRGB0, mRGB0, aRGB0;
    // Cycle 0 Alpha: (A - B) * C + D
    int saA0, sbA0, mA0, aA0;
    // Cycle 1 RGB: (A - B) * C + D
    int saRGB1, sbRGB1, mRGB1, aRGB1;
    // Cycle 1 Alpha: (A - B) * C + D
    int saA1, sbA1, mA1, aA1;
    int numCycles;
};

void OGL_SetN64Combiner(const N64CombinerState *state);
void OGL_UpdateN64CombinerColors(void);
