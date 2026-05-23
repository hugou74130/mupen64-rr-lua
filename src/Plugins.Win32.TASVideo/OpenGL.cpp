#include "stdafx.h"
#include "glN64.h"
#include "OpenGL.h"
#include "Types.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "Textures.h"
#include "Combiner.h"
#include "VI.h"

#include <format>
#include <cstring>

GLInfo OGL{};

static GLuint g_n64VAO = 0;
static GLuint g_n64VBO = 0;
static GLuint g_n64Program = 0;
static GLint g_n64UniTexture0 = -1;
static GLint g_n64UniTexture1 = -1;
static GLint g_n64UniUseTexture0 = -1;
static GLint g_n64UniUseTexture1 = -1;

// N64 uber-combiner uniforms
static GLint g_n64UniPrimColor = -1;
static GLint g_n64UniEnvColor = -1;
static GLint g_n64UniPrimLODFrac = -1;
static GLint g_n64UniFogColor = -1;
static GLint g_n64UniFogEnabled = -1;
static GLint g_n64UniFogMultiplier = -1;
static GLint g_n64UniFogOffset = -1;
static GLint g_n64UniAlphaTestEnabled = -1;
static GLint g_n64UniAlphaTestThreshold = -1;
static GLint g_n64UniAlphaTestFunction = -1;
static GLint g_n64UniStippleEnabled = -1;
static GLint g_n64UniStipplePattern = -1;
// Combiner: packed (A,B,C,D) per cycle per channel
static GLint g_n64UniCombine0RGB = -1;
static GLint g_n64UniCombine0A = -1;
static GLint g_n64UniCombine1RGB = -1;
static GLint g_n64UniCombine1A = -1;
static GLint g_n64UniNumCycles = -1;

// Global combiner state, filled by unified_combiner and consumed by OGL_DrawTriangles
static N64CombinerState g_n64State;

void *gCapturedPixels; // pointer to buffer to fill

// Forward declarations for Core OpenGL context abstraction
void OGL_SwapBuffers();

// Forward declarations for Core OpenGL context abstraction
void OGL_SwapBuffers();

void OGL_ReadPixels()
{
    GLint oldMode;
    glGetIntegerv(GL_READ_BUFFER, &oldMode);
    // glReadBuffer(GL_FRONT);

    glReadBuffer(GL_BACK);
    glReadPixels(0, OGL.heightOffset, OGL.width, OGL.height, GL_BGRA, GL_UNSIGNED_BYTE, gCapturedPixels);
    glReadBuffer(oldMode); // restore old read buffer
}

void OGL_InitExtensions()
{
    GLenum glew = glewInit();
    if (glew != GLEW_OK)
    {
        g_ef->log_error(L"Error initialising glew");
        return;
    }

    OGL.ARB_multitexture = GLEW_ARB_multitexture;
    glGetIntegerv(GL_MAX_TEXTURE_UNITS_ARB, &OGL.maxTextureUnits);
    OGL.maxTextureUnits = min(8, OGL.maxTextureUnits); // The plugin only supports 8, and 4 is really enough

    OGL.EXT_fog_coord = GLEW_EXT_fog_coord;
    OGL.EXT_secondary_color = GLEW_EXT_secondary_color;
    OGL.ARB_texture_env_combine = GLEW_ARB_texture_env_combine;
    OGL.ARB_texture_env_crossbar = GLEW_ARB_texture_env_crossbar;
    OGL.EXT_texture_env_combine = GLEW_EXT_texture_env_combine;
}

void OGL_InitStates()
{
    // Legacy fixed-function matrix setup removed — N64 vertices are already in
    // clip-space and shaders handle projection via uniforms.
    // TODO: if a projection matrix is ever needed, pass it as a uniform.

    // NOTE: glVertexPointer / glColorPointer / glTexCoordPointer /
    // glEnableClientState are deprecated in Core OpenGL. The N64 pipeline
    // now uses a VAO/VBO with glVertexAttribPointer. The Combiner
    // (glTexEnv) has been shaderized — see unified_combiner.cpp and
    // OGL_SetN64Combiner().
    OGL_InitN64Resources();
    if (!g_n64Program) return;

    glBindVertexArray(g_n64VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_n64VBO);

    // Position (4 floats) — matches GLVertex layout exactly
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)0);

    // Primary color (4 floats)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(4 * sizeof(float)));

    // Secondary color (4 floats — alpha kept for alignment)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(8 * sizeof(float)));

    // TexCoord0 (2 floats)
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(12 * sizeof(float)));

    // TexCoord1 (2 floats)
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(14 * sizeof(float)));

    // Fog (1 float)
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(16 * sizeof(float)));

    glBindVertexArray(0);

    glPolygonOffset(-3.0f, -3.0f);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    srand(timeGetTime());

    for (int i = 0; i < 32; i++)
    {
        for (int j = 0; j < 8; j++)
            for (int k = 0; k < 128; k++)
                OGL.stipplePattern[i][j][k] = ((i > (rand() >> 10)) << 7) | ((i > (rand() >> 10)) << 6) |
                                              ((i > (rand() >> 10)) << 5) | ((i > (rand() >> 10)) << 4) |
                                              ((i > (rand() >> 10)) << 3) | ((i > (rand() >> 10)) << 2) |
                                              ((i > (rand() >> 10)) << 1) | ((i > (rand() >> 10)) << 0);
    }

    OGL_SwapBuffers();
}

void OGL_UpdateScale()
{
    OGL.scaleX = OGL.width / (float)VI.width;
    OGL.scaleY = OGL.height / (float)VI.height;
}

void OGL_ResizeWindow()
{
    RECT windowRect, statusRect, toolRect;

    OGL.width = OGL.windowedWidth;
    OGL.height = OGL.windowedHeight;

    GetClientRect(hWnd, &windowRect);

    if (hStatusBar)
        GetWindowRect(hStatusBar, &statusRect);
    else
        statusRect.bottom = statusRect.top = 0;

    if (hToolBar)
        GetWindowRect(hToolBar, &toolRect);
    else
        toolRect.bottom = toolRect.top = 0;

    OGL.heightOffset = (statusRect.bottom - statusRect.top);
    windowRect.right = windowRect.left + OGL.windowedWidth - 1;
    windowRect.bottom = windowRect.top + OGL.windowedHeight - 1 + OGL.heightOffset;

    AdjustWindowRect(&windowRect, GetWindowLong(hWnd, GWL_STYLE), GetMenu(hWnd) != NULL);

    SetWindowPos(hWnd, NULL, 0, 0, windowRect.right - windowRect.left + 1,
                 windowRect.bottom - windowRect.top + 1 + toolRect.bottom - toolRect.top + 1,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOMOVE);
}

bool OGL_CreateContext()
{
#ifdef _WIN32
    int pixelFormat;

    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), // size of this pfd
        1,                             // version number
        PFD_DRAW_TO_WINDOW |           // support window
            PFD_SUPPORT_OPENGL |       // support OpenGL
            PFD_DOUBLEBUFFER,          // double buffered
        PFD_TYPE_RGBA,                 // RGBA type
        32,                            // color depth
        0,
        0,
        0,
        0,
        0,
        0, // color bits ignored
        0, // no alpha buffer
        0, // shift bit ignored
        0, // no accumulation buffer
        0,
        0,
        0,
        0,              // accum bits ignored
        32,             // z-buffer
        0,              // no stencil buffer
        0,              // no auxiliary buffer
        PFD_MAIN_PLANE, // main layer
        0,              // reserved
        0,
        0,
        0 // layer masks ignored
    };

    if ((OGL.hDC = GetDC(hWnd)) == NULL)
    {
        MessageBox(hWnd, L"Error while getting a device context!", PLUGIN_NAME, MB_ICONERROR | MB_OK);
        return FALSE;
    }

    if ((pixelFormat = ChoosePixelFormat(OGL.hDC, &pfd)) == 0)
    {
        MessageBox(hWnd, L"Unable to find a suitable pixel format!", PLUGIN_NAME, MB_ICONERROR | MB_OK);
        OGL_Stop();
        return FALSE;
    }

    if ((SetPixelFormat(OGL.hDC, pixelFormat, &pfd)) == FALSE)
    {
        MessageBox(hWnd, L"Error while setting pixel format!", PLUGIN_NAME, MB_ICONERROR | MB_OK);
        OGL_Stop();
        return FALSE;
    }
    if ((OGL.hRC = wglCreateContext(OGL.hDC)) == NULL)
    {
        MessageBox(hWnd, L"Error while creating OpenGL context!", PLUGIN_NAME, MB_ICONERROR | MB_OK);
        OGL_Stop();
        return FALSE;
    }

    if ((wglMakeCurrent(OGL.hDC, OGL.hRC)) == FALSE)
    {
        MessageBox(hWnd, L"Error while making OpenGL context current!", PLUGIN_NAME, MB_ICONERROR | MB_OK);
        OGL_Stop();
        return FALSE;
    }

    return TRUE;
#elif defined(USE_GLFW)
    // TODO: GLFW context creation for Linux / macOS
    g_view_logger->warn(L"GLFW backend not yet implemented");
    return FALSE;
#elif defined(USE_EGL)
    // TODO: EGL context creation for ANGLE / Linux
    g_view_logger->warn(L"EGL backend not yet implemented");
    return FALSE;
#else
    g_view_logger->error(L"No OpenGL context backend available");
    return FALSE;
#endif
}

bool OGL_DestroyContextImpl()
{
#ifdef _WIN32
    wglMakeCurrent(NULL, NULL);

    if (OGL.hRC)
    {
        wglDeleteContext(OGL.hRC);
        OGL.hRC = NULL;
    }

    if (OGL.hDC)
    {
        ReleaseDC(hWnd, OGL.hDC);
        OGL.hDC = NULL;
    }

    return TRUE;
#elif defined(USE_GLFW)
    // TODO: GLFW cleanup
    return TRUE;
#elif defined(USE_EGL)
    // TODO: EGL cleanup
    return TRUE;
#else
    return TRUE;
#endif
}

void OGL_SwapBuffers()
{
#ifdef _WIN32
    if (OGL.hDC) SwapBuffers(OGL.hDC);
#elif defined(USE_GLFW)
    // TODO: glfwSwapBuffers(OGL.glfwWindow);
#elif defined(USE_EGL)
    // TODO: eglSwapBuffers(OGL.eglDisplay, OGL.eglSurface);
#endif
}

bool OGL_InitContext()
{
    if (!OGL_CreateContext()) return FALSE;

    OGL_InitExtensions();
    OGL_InitStates();
    return TRUE;
}

bool OGL_DestroyContext()
{
    return OGL_DestroyContextImpl();
}

bool OGL_Start()
{

    if (OGL.recycle_context)
    {
        if (!OGL.context_initialized)
        {
            OGL_InitContext();
        }
    }
    else
    {
        OGL_InitContext();
    }

    TextureCache_Init();
    FrameBuffer_Init();
    Combiner_Init();

    gSP.changed = gDP.changed = 0xFFFFFFFF;
    OGL_UpdateScale();

    return TRUE;
}

void OGL_Stop()
{
    Combiner_Destroy();
    FrameBuffer_Destroy();
    TextureCache_Destroy();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    OGL_SwapBuffers();

    if (!OGL.recycle_context)
    {
        OGL_DestroyContext();
    }
}

void OGL_UpdateCullFace()
{
    if (gSP.geometryMode & G_CULL_BOTH)
    {
        glEnable(GL_CULL_FACE);

        if (gSP.geometryMode & G_CULL_BACK)
            glCullFace(GL_BACK);
        else
            glCullFace(GL_FRONT);
    }
    else
        glDisable(GL_CULL_FACE);
}

void OGL_UpdateViewport()
{
    glViewport(gSP.viewport.x * OGL.scaleX,
               (VI.height - (gSP.viewport.y + gSP.viewport.height)) * OGL.scaleY + OGL.heightOffset,
               gSP.viewport.width * OGL.scaleX, gSP.viewport.height * OGL.scaleY);
    glDepthRange(0.0f, 1.0f); // gSP.viewport.nearz, gSP.viewport.farz );
}

void OGL_UpdateDepthUpdate()
{
    if (gDP.otherMode.depthUpdate)
        glDepthMask(TRUE);
    else
        glDepthMask(FALSE);
}

void OGL_UpdateStates()
{
    if (gSP.changed & CHANGED_GEOMETRYMODE)
    {
        OGL_UpdateCullFace();

        // Fog is now handled in the uber-combiner fragment shader via
        // uFogEnabled / uFogColor / uFogMultiplier / uFogOffset uniforms.
        // The fixed-function GL_FOG enable/disable is deprecated in Core GL.

        gSP.changed &= ~CHANGED_GEOMETRYMODE;
    }

    if (gSP.geometryMode & G_ZBUFFER)
        glEnable(GL_DEPTH_TEST);
    else
        glDisable(GL_DEPTH_TEST);

    if (gDP.changed & CHANGED_RENDERMODE)
    {
        if (gDP.otherMode.depthCompare)
            glDepthFunc(GL_LEQUAL);
        else
            glDepthFunc(GL_ALWAYS);

        OGL_UpdateDepthUpdate();

        if (gDP.otherMode.depthMode == ZMODE_DEC)
            glEnable(GL_POLYGON_OFFSET_FILL);
        else
        {
            //			glPolygonOffset( -3.0f, -3.0f );
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    if ((gDP.changed & CHANGED_ALPHACOMPARE) || (gDP.changed & CHANGED_RENDERMODE))
    {
        // Alpha test and polygon stipple are now handled in the uber-combiner
        // fragment shader via uniforms (uAlphaTestEnabled, uAlphaTestThreshold,
        // uPolygonStippleEnabled, uStipplePattern).
        // The fixed-function glAlphaFunc, GL_ALPHA_TEST and GL_POLYGON_STIPPLE
        // are deprecated in Core OpenGL / unavailable in GLES.

        // Legacy alpha test and stipple state tracking kept for reference
        // but actual implementation is shader-based.
        (void)(gDP.otherMode.alphaCompare);
        (void)(gDP.otherMode.alphaCvgSel);
        (void)(gDP.otherMode.cvgXAlpha);
    }

    if (gDP.changed & CHANGED_SCISSOR)
    {
        glScissor(gDP.scissor.ulx * OGL.scaleX, (VI.height - gDP.scissor.lry) * OGL.scaleY + OGL.heightOffset,
                  (gDP.scissor.lrx - gDP.scissor.ulx) * OGL.scaleX, (gDP.scissor.lry - gDP.scissor.uly) * OGL.scaleY);
    }

    if (gSP.changed & CHANGED_VIEWPORT)
    {
        OGL_UpdateViewport();
    }

    if ((gDP.changed & CHANGED_COMBINE) || (gDP.changed & CHANGED_CYCLETYPE))
    {
        if (gDP.otherMode.cycleType == G_CYC_COPY)
            Combiner_SetCombine(EncodeCombineMode(0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0, 0, 0, 0, TEXEL0));
        else if (gDP.otherMode.cycleType == G_CYC_FILL)
            Combiner_SetCombine(EncodeCombineMode(0, 0, 0, SHADE, 0, 0, 0, 1, 0, 0, 0, SHADE, 0, 0, 0, 1));
        else
            Combiner_SetCombine(gDP.combine.mux);
    }

    if (gDP.changed & CHANGED_COMBINE_COLORS)
    {
        Combiner_UpdateCombineColors();
    }

    if ((gSP.changed & CHANGED_TEXTURE) || (gDP.changed & CHANGED_TILE) || (gDP.changed & CHANGED_TMEM))
    {
        Combiner_BeginTextureUpdate();

        if (combiner.usesT0)
        {
            TextureCache_Update(0);

            gSP.changed &= ~CHANGED_TEXTURE;
            gDP.changed &= ~CHANGED_TILE;
            gDP.changed &= ~CHANGED_TMEM;
        }
        else
        {
            TextureCache_ActivateDummy(0);
        }

        if (combiner.usesT1)
        {
            TextureCache_Update(1);

            gSP.changed &= ~CHANGED_TEXTURE;
            gDP.changed &= ~CHANGED_TILE;
            gDP.changed &= ~CHANGED_TMEM;
        }
        else
        {
            TextureCache_ActivateDummy(1);
        }

        Combiner_EndTextureUpdate();
    }

    // Fog color is now handled in the uber-combiner fragment shader via
    // the uFogColor uniform. glFogfv is deprecated in Core OpenGL.
    if ((gDP.changed & CHANGED_FOGCOLOR) && OGL.fog)
    {
        // Shader uniform uFogColor is updated via OGL_SetN64Combiner()
    }

    if ((gDP.changed & CHANGED_RENDERMODE) || (gDP.changed & CHANGED_CYCLETYPE))
    {
        if ((gDP.otherMode.forceBlender) && (gDP.otherMode.cycleType != G_CYC_COPY) &&
            (gDP.otherMode.cycleType != G_CYC_FILL) && !(gDP.otherMode.alphaCvgSel))
        {
            glEnable(GL_BLEND);

            switch (gDP.otherMode.l >> 16)
            {
            case 0x0448: // Add
            case 0x055A:
                glBlendFunc(GL_ONE, GL_ONE);
                break;
            case 0x0C08: // 1080 Sky
            case 0x0F0A: // Used LOTS of places
                glBlendFunc(GL_ONE, GL_ZERO);
                break;
            case 0xC810: // Blends fog
            case 0xC811: // Blends fog
            case 0x0C18: // Standard interpolated blend
            case 0x0C19: // Used for antialiasing
            case 0x0050: // Standard interpolated blend
            case 0x0055: // Used for antialiasing
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;
            case 0x0FA5: // Seems to be doing just blend color - maybe combiner can be used for this?
            case 0x5055: // Used in Paper Mario intro, I'm not sure if this is right...
                glBlendFunc(GL_ZERO, GL_ONE);
                break;
            default:
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                break;
            }
        }
        else
            glDisable(GL_BLEND);

        if (gDP.otherMode.cycleType == G_CYC_FILL)
        {
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_BLEND);
        }
    }

    gDP.changed &= CHANGED_TILE | CHANGED_TMEM;
    gSP.changed &= CHANGED_TEXTURE | CHANGED_MATRIX;
}

void OGL_AddTriangle(SPVertex *vertices, int v0, int v1, int v2)
{
    int v[] = {v0, v1, v2};

    if (gSP.changed || gDP.changed) OGL_UpdateStates();

    //	Playing around with lod fraction junk...
    //	float ds = max( max( fabs( vertices[v0].s - vertices[v1].s ), fabs( vertices[v0].s - vertices[v2].s ) ), fabs(
    // vertices[v1].s - vertices[v2].s ) ) * cache.current[0]->shiftScaleS * gSP.texture.scales; 	float dx = max( max(
    // fabs( vertices[v0].x / vertices[v0].w - vertices[v1].x / vertices[v1].w ), fabs( vertices[v0].x / vertices[v0].w
    // - vertices[v2].x / vertices[v2].w ) ), fabs( vertices[v1].x / vertices[v1].w - vertices[v2].x / vertices[v2].w )
    // ) * gSP.viewport.vscale[0]; 	float lod = ds / dx; 	float lod_fraction = min( 1.0f, max( 0.0f, lod - 1.0f ) /
    // max( 1.0f, gSP.texture.level ) );

    for (int i = 0; i < 3; i++)
    {
        OGL.vertices[OGL.numVertices].x = vertices[v[i]].x;
        OGL.vertices[OGL.numVertices].y = vertices[v[i]].y;
        OGL.vertices[OGL.numVertices].z =
            gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z * vertices[v[i]].w : vertices[v[i]].z;
        OGL.vertices[OGL.numVertices].w = vertices[v[i]].w;

        OGL.vertices[OGL.numVertices].color.r = vertices[v[i]].r;
        OGL.vertices[OGL.numVertices].color.g = vertices[v[i]].g;
        OGL.vertices[OGL.numVertices].color.b = vertices[v[i]].b;
        OGL.vertices[OGL.numVertices].color.a = vertices[v[i]].a;
        SetConstant(OGL.vertices[OGL.numVertices].color, combiner.vertex.color, combiner.vertex.alpha);
        // SetConstant( OGL.vertices[OGL.numVertices].secondaryColor, combiner.vertex.secondaryColor, ONE );

        if (OGL.EXT_secondary_color)
        {
            OGL.vertices[OGL.numVertices].secondaryColor.r = 0.0f; // lod_fraction; //vertices[v[i]].r;
            OGL.vertices[OGL.numVertices].secondaryColor.g = 0.0f; // lod_fraction; //vertices[v[i]].g;
            OGL.vertices[OGL.numVertices].secondaryColor.b = 0.0f; // lod_fraction; //vertices[v[i]].b;
            OGL.vertices[OGL.numVertices].secondaryColor.a = 1.0f;
            SetConstant(OGL.vertices[OGL.numVertices].secondaryColor, combiner.vertex.secondaryColor, ONE);
        }

        if ((gSP.geometryMode & G_FOG) && OGL.EXT_fog_coord && OGL.fog)
        {
            if (vertices[v[i]].z < -vertices[v[i]].w)
                OGL.vertices[OGL.numVertices].fog = max(0.0f, -(float)gSP.fog.multiplier + (float)gSP.fog.offset);
            else
                OGL.vertices[OGL.numVertices].fog =
                    max(0.0f, vertices[v[i]].z / vertices[v[i]].w * (float)gSP.fog.multiplier + (float)gSP.fog.offset);
        }

        if (combiner.usesT0)
        {
            if (cache.current[0]->frameBufferTexture)
            {
                /*				OGL.vertices[OGL.numVertices].s0 = (cache.current[0]->offsetS + (vertices[v[i]].s *
                   cache.current[0]->shiftScaleS * gSP.texture.scales - gSP.textureTile[0]->fuls)) *
                   cache.current[0]->scaleS; OGL.vertices[OGL.numVertices].t0 = (cache.current[0]->offsetT -
                   (vertices[v[i]].t * cache.current[0]->shiftScaleT * gSP.texture.scalet - gSP.textureTile[0]->fult)) *
                   cache.current[0]->scaleT;*/

                if (gSP.textureTile[0]->masks)
                    OGL.vertices[OGL.numVertices].s0 =
                        (cache.current[0]->offsetS +
                         (vertices[v[i]].s * cache.current[0]->shiftScaleS * gSP.texture.scales -
                          fmod(gSP.textureTile[0]->fuls, 1 << gSP.textureTile[0]->masks))) *
                        cache.current[0]->scaleS;
                else
                    OGL.vertices[OGL.numVertices].s0 =
                        (cache.current[0]->offsetS +
                         (vertices[v[i]].s * cache.current[0]->shiftScaleS * gSP.texture.scales -
                          gSP.textureTile[0]->fuls)) *
                        cache.current[0]->scaleS;

                if (gSP.textureTile[0]->maskt)
                    OGL.vertices[OGL.numVertices].t0 =
                        (cache.current[0]->offsetT -
                         (vertices[v[i]].t * cache.current[0]->shiftScaleT * gSP.texture.scalet -
                          fmod(gSP.textureTile[0]->fult, 1 << gSP.textureTile[0]->maskt))) *
                        cache.current[0]->scaleT;
                else
                    OGL.vertices[OGL.numVertices].t0 =
                        (cache.current[0]->offsetT -
                         (vertices[v[i]].t * cache.current[0]->shiftScaleT * gSP.texture.scalet -
                          gSP.textureTile[0]->fult)) *
                        cache.current[0]->scaleT;
            }
            else
            {
                OGL.vertices[OGL.numVertices].s0 =
                    (vertices[v[i]].s * cache.current[0]->shiftScaleS * gSP.texture.scales - gSP.textureTile[0]->fuls +
                     cache.current[0]->offsetS) *
                    cache.current[0]->scaleS;
                OGL.vertices[OGL.numVertices].t0 =
                    (vertices[v[i]].t * cache.current[0]->shiftScaleT * gSP.texture.scalet - gSP.textureTile[0]->fult +
                     cache.current[0]->offsetT) *
                    cache.current[0]->scaleT;
            }
        }

        if (combiner.usesT1)
        {
            if (cache.current[0]->frameBufferTexture)
            {
                OGL.vertices[OGL.numVertices].s1 =
                    (cache.current[1]->offsetS +
                     (vertices[v[i]].s * cache.current[1]->shiftScaleS * gSP.texture.scales -
                      gSP.textureTile[1]->fuls)) *
                    cache.current[1]->scaleS;
                OGL.vertices[OGL.numVertices].t1 =
                    (cache.current[1]->offsetT -
                     (vertices[v[i]].t * cache.current[1]->shiftScaleT * gSP.texture.scalet -
                      gSP.textureTile[1]->fult)) *
                    cache.current[1]->scaleT;
            }
            else
            {
                OGL.vertices[OGL.numVertices].s1 =
                    (vertices[v[i]].s * cache.current[1]->shiftScaleS * gSP.texture.scales - gSP.textureTile[1]->fuls +
                     cache.current[1]->offsetS) *
                    cache.current[1]->scaleS;
                OGL.vertices[OGL.numVertices].t1 =
                    (vertices[v[i]].t * cache.current[1]->shiftScaleT * gSP.texture.scalet - gSP.textureTile[1]->fult +
                     cache.current[1]->offsetT) *
                    cache.current[1]->scaleT;
            }
        }
        OGL.numVertices++;
    }
    OGL.numTriangles++;

    if (OGL.numVertices >= 255) OGL_DrawTriangles();
}

void OGL_DrawTriangles()
{
    if (OGL.numVertices == 0) return;

    // ---- Build combiner state (replaces fixed-function glTexEnv / glAlphaFunc / glFog) ----
    N64CombinerState &state = g_n64State;

    // Stipple
    if (OGL.usePolygonStipple && (gDP.otherMode.alphaCompare == G_AC_DITHER) && !(gDP.otherMode.alphaCvgSel))
    {
        OGL.lastStipple = (OGL.lastStipple + 1) & 0x7;
        state.stippleEnabled = 1;
        state.stipplePattern = OGL.lastStipple;
    }
    else
    {
        state.stippleEnabled = 0;
        state.stipplePattern = 0;
    }

    // Core OpenGL: upload vertex data and draw with N64 uber-combiner shader
    OGL_InitN64Resources();
    if (g_n64Program)
    {
        glUseProgram(g_n64Program);

        // Re-bind textures before every draw call — other helpers (blit, textured rect)
        // may have switched GL_TEXTURE0/1 between OGL_UpdateStates() and now.
        // Always bind *something* (real texture or dummy) so the sampler is never
        // left without a bound texture, which produces undefined (usually white).
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D,
                      (combiner.usesT0 && cache.current[0]) ? cache.current[0]->glName : cache.dummy->glName);
        if (OGL.ARB_multitexture)
        {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D,
                          (combiner.usesT1 && cache.current[1]) ? cache.current[1]->glName : cache.dummy->glName);
        }

        glUniform1i(g_n64UniUseTexture0, combiner.usesT0 ? GL_TRUE : GL_FALSE);
        glUniform1i(g_n64UniUseTexture1, combiner.usesT1 ? GL_TRUE : GL_FALSE);
        glUniform1i(g_n64UniTexture0, 0);
        glUniform1i(g_n64UniTexture1, 1);

        // Only upload stipple state here. The full combiner state (colors,
        // combine uniforms, fog, alpha test) was already uploaded by
        // Set() / EndTextureUpdate_unified_combiner() in OGL_UpdateStates().
        if (g_n64UniStippleEnabled >= 0) glUniform1i(g_n64UniStippleEnabled, state.stippleEnabled);
        if (g_n64UniStipplePattern >= 0) glUniform1i(g_n64UniStipplePattern, state.stipplePattern);

        glBindVertexArray(g_n64VAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_n64VBO);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(GLVertex) * OGL.numVertices, OGL.vertices);
        glDrawArrays(GL_TRIANGLES, 0, OGL.numVertices);
        glBindVertexArray(0);
    }

    OGL.numTriangles = OGL.numVertices = 0;
}

// Forward declare CompileShader (defined later with blit resources)
static GLuint CompileShader(GLenum type, const char *source);

// ============================================================================
// GLSL version selection
// ============================================================================
// Define USE_OPENGL_ES when targeting GLES 3.0 or ANGLE.
// On desktop Core OpenGL this macro is absent and we default to #version 330.
#ifdef USE_OPENGL_ES
#define GLSL_VERSION_HEADER "#version 300 es\n"
#else
#define GLSL_VERSION_HEADER "#version 330 core\n"
#endif

// ---- Core OpenGL Primitive Resources (lines, rects, textured rects) ----
static GLuint g_primVAO = 0;
static GLuint g_primVBO = 0;
static GLuint g_primProgram = 0;
static GLint g_primUniOrtho = -1;
static GLint g_primUniUseOrtho = -1;
static GLint g_primUniUseTexture = -1;
static GLint g_primUniTexture0 = -1;
static GLint g_primUniTexture1 = -1;

static const char *g_primVS = GLSL_VERSION_HEADER R"(
#ifdef GL_ES
precision highp float;
#endif

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec2 aTexCoord0;
layout(location = 3) in vec2 aTexCoord1;

uniform mat4 uOrtho;
uniform bool uUseOrtho;

out vec4 vColor;
out vec2 vTexCoord0;
out vec2 vTexCoord1;

void main() {
    if (uUseOrtho)
        gl_Position = uOrtho * aPos;
    else
        gl_Position = aPos;
    vColor = aColor;
    vTexCoord0 = aTexCoord0;
    vTexCoord1 = aTexCoord1;
}
)";

static const char *g_primFS = GLSL_VERSION_HEADER R"(
#ifdef GL_ES
precision mediump float;
precision mediump sampler2D;
#endif

in vec4 vColor;
in vec2 vTexCoord0;
in vec2 vTexCoord1;

uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform bool uUseTexture;
uniform bool uUseMultiTexture;

out vec4 FragColor;

void main() {
    vec4 color = vColor;
    if (uUseTexture) {
        color = color * texture(uTexture0, vTexCoord0);
        if (uUseMultiTexture)
            color = color * texture(uTexture1, vTexCoord1);
    }
    FragColor = color;
}
)";

static void OGL_GetOrthoMatrix(float *out)
{
    float w = (float)VI.width;
    float h = (float)VI.height;
    // Ortho(0, w, h, 0, -1, 1)
    out[0] = 2.0f / w;
    out[1] = 0;
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
    out[5] = -2.0f / h;
    out[6] = 0;
    out[7] = 0;
    out[8] = 0;
    out[9] = 0;
    out[10] = 1;
    out[11] = 0;
    out[12] = -1;
    out[13] = 1;
    out[14] = 0;
    out[15] = 1;
}

void OGL_InitPrimitiveResources()
{
    if (g_primProgram) return;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, g_primVS);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, g_primFS);
    if (!vs || !fs) return;
    g_primProgram = glCreateProgram();
    glAttachShader(g_primProgram, vs);
    glAttachShader(g_primProgram, fs);
    glBindAttribLocation(g_primProgram, 0, "aPos");
    glBindAttribLocation(g_primProgram, 1, "aColor");
    glBindAttribLocation(g_primProgram, 2, "aTexCoord");
    glLinkProgram(g_primProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linkStatus;
    glGetProgramiv(g_primProgram, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus)
    {
        char buf[512];
        glGetProgramInfoLog(g_primProgram, 512, nullptr, buf);
        std::string narrow(buf);
        std::wstring wide(narrow.begin(), narrow.end());
        g_ef->log_error(std::format(L"Primitive shader link failed: {}", wide.c_str()).c_str());
        glDeleteProgram(g_primProgram);
        g_primProgram = 0;
        return;
    }
    g_primUniOrtho = glGetUniformLocation(g_primProgram, "uOrtho");
    g_primUniUseOrtho = glGetUniformLocation(g_primProgram, "uUseOrtho");
    g_primUniUseTexture = glGetUniformLocation(g_primProgram, "uUseTexture");
    g_primUniTexture0 = glGetUniformLocation(g_primProgram, "uTexture0");
    g_primUniTexture1 = glGetUniformLocation(g_primProgram, "uTexture1");

    glGenVertexArrays(1, &g_primVAO);
    glGenBuffers(1, &g_primVBO);
    glBindVertexArray(g_primVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_primVBO);
    // Max 6 vertices * (pos4 + color4 + texcoord0_2 + texcoord1_2) floats
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 12, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void *)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 12 * sizeof(float), (void *)(10 * sizeof(float)));
    glBindVertexArray(0);
}

void OGL_DestroyPrimitiveResources()
{
    if (g_primVAO)
    {
        glDeleteVertexArrays(1, &g_primVAO);
        g_primVAO = 0;
    }
    if (g_primVBO)
    {
        glDeleteBuffers(1, &g_primVBO);
        g_primVBO = 0;
    }
    if (g_primProgram)
    {
        glDeleteProgram(g_primProgram);
        g_primProgram = 0;
    }
    g_primUniOrtho = g_primUniUseOrtho = g_primUniUseTexture = g_primUniTexture0 = g_primUniTexture1 = -1;
}

// ---- Core OpenGL N64 Pipeline Resources (3D rendering) ----
// Uber-combiner shader: replaces all glTexEnv/GL_COMBINE_ARB, glAlphaFunc,
// glFog* and glPolygonStipple fixed-function pipeline.
// Targets OpenGL 3.3 Core / GLES 3.0 / ANGLE.

static const char *g_n64VS = GLSL_VERSION_HEADER R"(
#ifdef GL_ES
precision highp float;
#endif

layout(location = 0) in vec4 aPos;
layout(location = 1) in vec4 aColor;
layout(location = 2) in vec4 aSecondaryColor;
layout(location = 3) in vec2 aTexCoord0;
layout(location = 4) in vec2 aTexCoord1;
layout(location = 5) in float aFog;

out vec4 vColor;
out vec4 vSecondaryColor;
out vec2 vTexCoord0;
out vec2 vTexCoord1;
out float vFog;

void main() {
    gl_Position = aPos;
    vColor = aColor;
    vSecondaryColor = aSecondaryColor;
    vTexCoord0 = aTexCoord0;
    vTexCoord1 = aTexCoord1;
    vFog = aFog;
}
)";

static const char *g_n64FS = GLSL_VERSION_HEADER R"(
#ifdef GL_ES
precision mediump float;
precision mediump sampler2D;
#endif

#define UC_COMBINED       0
#define UC_TEXEL0         1
#define UC_TEXEL1         2
#define UC_PRIMITIVE      3
#define UC_SHADE          4
#define UC_ENVIRONMENT    5
#define UC_CENTER         6
#define UC_ONE            7
#define UC_COMBINED_ALPHA 8
#define UC_TEXEL0_ALPHA   9
#define UC_TEXEL1_ALPHA   10
#define UC_PRIM_ALPHA     11
#define UC_SHADE_ALPHA    12
#define UC_ENV_ALPHA      13
#define UC_LOD_FRACTION   14
#define UC_PRIM_LOD_FRAC  15
#define UC_K4             16
#define UC_K5             17
#define UC_ZERO           18
#define UC_HALF           19

uniform sampler2D uTexture0;
uniform sampler2D uTexture1;
uniform bool      uUseTexture0;
uniform bool      uUseTexture1;

uniform vec4  uPrimColor;
uniform vec4  uEnvColor;
uniform float uPrimLODFrac;

uniform vec4  uFogColor;
uniform bool  uFogEnabled;
uniform float uFogMultiplier;
uniform float uFogOffset;

uniform bool  uAlphaTestEnabled;
uniform float uAlphaTestThreshold;
uniform int   uAlphaTestFunction;

uniform bool uPolygonStippleEnabled;
uniform int  uStipplePattern;

uniform ivec4 uCombine0RGB;
uniform ivec4 uCombine0A;
uniform ivec4 uCombine1RGB;
uniform ivec4 uCombine1A;
uniform int   uNumCycles;

in vec4 vColor;
in vec4 vSecondaryColor;
in vec2 vTexCoord0;
in vec2 vTexCoord1;
in float vFog;

out vec4 FragColor;

vec4 resolveInput(int src, vec4 texel0, vec4 texel1, vec4 combined, vec4 shade) {
    switch (src) {
        case UC_COMBINED:        return combined;
        case UC_TEXEL0:          return texel0;
        case UC_TEXEL1:          return texel1;
        case UC_PRIMITIVE:       return uPrimColor;
        case UC_SHADE:           return shade;
        case UC_ENVIRONMENT:     return uEnvColor;
        case UC_CENTER:          return vec4(0.5, 0.5, 0.5, 0.5);
        case UC_ONE:             return vec4(1.0, 1.0, 1.0, 1.0);
        case UC_COMBINED_ALPHA:  return vec4(combined.a);
        case UC_TEXEL0_ALPHA:    return vec4(texel0.a);
        case UC_TEXEL1_ALPHA:    return vec4(texel1.a);
        case UC_PRIM_ALPHA:      return vec4(uPrimColor.a);
        case UC_SHADE_ALPHA:     return vec4(shade.a);
        case UC_ENV_ALPHA:       return vec4(uEnvColor.a);
        case UC_LOD_FRACTION:    return vec4(uPrimLODFrac);
        case UC_PRIM_LOD_FRAC:   return vec4(uPrimLODFrac);
        case UC_K4:              return vec4(1.0, 1.0, 1.0, 1.0);
        case UC_K5:              return vec4(0.5, 0.5, 0.5, 0.5);
        case UC_HALF:            return vec4(0.5, 0.5, 0.5, 0.5);
        case UC_ZERO:            return vec4(0.0, 0.0, 0.0, 0.0);
        default:                 return vec4(0.0, 0.0, 0.0, 0.0);
    }
}

vec4 combinerCycle(ivec4 rgbABCD, ivec4 alphaABCD,
                   vec4 texel0, vec4 texel1, vec4 prev, vec4 shade) {
    vec4 aRGB = resolveInput(rgbABCD.x, texel0, texel1, prev, shade);
    vec4 bRGB = resolveInput(rgbABCD.y, texel0, texel1, prev, shade);
    vec4 cRGB = resolveInput(rgbABCD.z, texel0, texel1, prev, shade);
    vec4 dRGB = resolveInput(rgbABCD.w, texel0, texel1, prev, shade);
    vec3 rgb  = clamp((aRGB.rgb - bRGB.rgb) * cRGB.rgb + dRGB.rgb, 0.0, 1.0);

    float aA = resolveInput(alphaABCD.x, texel0, texel1, prev, shade).a;
    float bA = resolveInput(alphaABCD.y, texel0, texel1, prev, shade).a;
    float cA = resolveInput(alphaABCD.z, texel0, texel1, prev, shade).a;
    float dA = resolveInput(alphaABCD.w, texel0, texel1, prev, shade).a;
    float alpha = clamp((aA - bA) * cA + dA, 0.0, 1.0);

    return vec4(rgb, alpha);
}

void main() {
    // FALLBACK: hardcoded MODULATE — tests whether the issue is in the
    // uber-combiner logic or in the texture/sampler/coords pipeline.
    // If everything renders correctly with this, the combiner shader
    // has a bug. If things are still broken, the problem is texcoords
    // or texture binding.
    if (uUseTexture0) {
        FragColor = vColor * texture(uTexture0, vTexCoord0);
    } else {
        FragColor = vColor;
    }
}
)";

void OGL_InitN64Resources()
{
    if (g_n64Program) return;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, g_n64VS);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, g_n64FS);
    if (!vs || !fs) return;
    g_n64Program = glCreateProgram();
    glAttachShader(g_n64Program, vs);
    glAttachShader(g_n64Program, fs);
    glBindAttribLocation(g_n64Program, 0, "aPos");
    glBindAttribLocation(g_n64Program, 1, "aColor");
    glBindAttribLocation(g_n64Program, 2, "aSecondaryColor");
    glBindAttribLocation(g_n64Program, 3, "aTexCoord0");
    glBindAttribLocation(g_n64Program, 4, "aTexCoord1");
    glBindAttribLocation(g_n64Program, 5, "aFog");
    glLinkProgram(g_n64Program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linkStatus;
    glGetProgramiv(g_n64Program, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus)
    {
        char buf[512];
        glGetProgramInfoLog(g_n64Program, 512, nullptr, buf);
        std::string narrow(buf);
        std::wstring wide(narrow.begin(), narrow.end());
        g_ef->log_error(std::format(L"N64 shader link failed: {}", wide.c_str()).c_str());
        glDeleteProgram(g_n64Program);
        g_n64Program = 0;
        return;
    }
    g_n64UniTexture0 = glGetUniformLocation(g_n64Program, "uTexture0");
    g_n64UniTexture1 = glGetUniformLocation(g_n64Program, "uTexture1");
    g_n64UniUseTexture0 = glGetUniformLocation(g_n64Program, "uUseTexture0");
    g_n64UniUseTexture1 = glGetUniformLocation(g_n64Program, "uUseTexture1");

    // N64 uber-combiner uniform locations
    g_n64UniPrimColor = glGetUniformLocation(g_n64Program, "uPrimColor");
    g_n64UniEnvColor = glGetUniformLocation(g_n64Program, "uEnvColor");
    g_n64UniPrimLODFrac = glGetUniformLocation(g_n64Program, "uPrimLODFrac");
    g_n64UniFogColor = glGetUniformLocation(g_n64Program, "uFogColor");
    g_n64UniFogEnabled = glGetUniformLocation(g_n64Program, "uFogEnabled");
    g_n64UniFogMultiplier = glGetUniformLocation(g_n64Program, "uFogMultiplier");
    g_n64UniFogOffset = glGetUniformLocation(g_n64Program, "uFogOffset");
    g_n64UniAlphaTestEnabled = glGetUniformLocation(g_n64Program, "uAlphaTestEnabled");
    g_n64UniAlphaTestThreshold = glGetUniformLocation(g_n64Program, "uAlphaTestThreshold");
    g_n64UniAlphaTestFunction = glGetUniformLocation(g_n64Program, "uAlphaTestFunction");
    g_n64UniStippleEnabled = glGetUniformLocation(g_n64Program, "uPolygonStippleEnabled");
    g_n64UniStipplePattern = glGetUniformLocation(g_n64Program, "uStipplePattern");
    g_n64UniCombine0RGB = glGetUniformLocation(g_n64Program, "uCombine0RGB");
    g_n64UniCombine0A = glGetUniformLocation(g_n64Program, "uCombine0A");
    g_n64UniCombine1RGB = glGetUniformLocation(g_n64Program, "uCombine1RGB");
    g_n64UniCombine1A = glGetUniformLocation(g_n64Program, "uCombine1A");
    g_n64UniNumCycles = glGetUniformLocation(g_n64Program, "uNumCycles");

    glGenVertexArrays(1, &g_n64VAO);
    glGenBuffers(1, &g_n64VBO);
    glBindVertexArray(g_n64VAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_n64VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLVertex) * 256, nullptr, GL_DYNAMIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(12 * sizeof(float)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(14 * sizeof(float)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(GLVertex), (void *)(16 * sizeof(float)));
    glBindVertexArray(0);
}

void OGL_DestroyN64Resources()
{
    if (g_n64VAO)
    {
        glDeleteVertexArrays(1, &g_n64VAO);
        g_n64VAO = 0;
    }
    if (g_n64VBO)
    {
        glDeleteBuffers(1, &g_n64VBO);
        g_n64VBO = 0;
    }
    if (g_n64Program)
    {
        glDeleteProgram(g_n64Program);
        g_n64Program = 0;
    }
    g_n64UniTexture0 = g_n64UniTexture1 = g_n64UniUseTexture0 = g_n64UniUseTexture1 = -1;
    g_n64UniPrimColor = g_n64UniEnvColor = g_n64UniPrimLODFrac = g_n64UniFogColor = -1;
    g_n64UniFogEnabled = g_n64UniFogMultiplier = g_n64UniFogOffset = -1;
    g_n64UniAlphaTestEnabled = g_n64UniAlphaTestThreshold = g_n64UniAlphaTestFunction = -1;
    g_n64UniStippleEnabled = g_n64UniStipplePattern = -1;
    g_n64UniCombine0RGB = g_n64UniCombine0A = g_n64UniCombine1RGB = g_n64UniCombine1A = -1;
    g_n64UniNumCycles = -1;
    memset(&g_n64State, 0, sizeof(g_n64State));
}

void OGL_DrawLine(SPVertex *vertices, int v0, int v1, float width)
{
    int v[] = {v0, v1};

    GLcolor color;

    if (gSP.changed || gDP.changed) OGL_UpdateStates();

    glLineWidth(width * OGL.scaleX);

    OGL_InitPrimitiveResources();
    if (!g_primProgram) return;

    float data[2 * 12] = {0};
    for (int i = 0; i < 2; i++)
    {
        int base = i * 12;
        // Position
        data[base + 0] = vertices[v[i]].x;
        data[base + 1] = vertices[v[i]].y;
        data[base + 2] = vertices[v[i]].z;
        data[base + 3] = vertices[v[i]].w;
        // Color (with SetConstant applied)
        color.r = vertices[v[i]].r;
        color.g = vertices[v[i]].g;
        color.b = vertices[v[i]].b;
        color.a = vertices[v[i]].a;
        SetConstant(color, combiner.vertex.color, combiner.vertex.alpha);
        data[base + 4] = color.r;
        data[base + 5] = color.g;
        data[base + 6] = color.b;
        data[base + 7] = color.a;
        // TexCoord0 (unused for lines)
        data[base + 8] = 0.0f;
        data[base + 9] = 0.0f;
        // TexCoord1 (unused for lines)
        data[base + 10] = 0.0f;
        data[base + 11] = 0.0f;
    }

    glUseProgram(g_primProgram);
    glUniform1i(g_primUniUseOrtho, GL_FALSE);
    glUniform1i(g_primUniUseTexture, GL_FALSE);

    glBindVertexArray(g_primVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_primVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
    glDrawArrays(GL_LINES, 0, 2);
    glBindVertexArray(0);
}

void OGL_DrawRect(int ulx, int uly, int lrx, int lry, float *color)
{
    OGL_UpdateStates();

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, OGL.heightOffset, OGL.width, OGL.height);
    glDepthRange(0.0f, 1.0f);

    OGL_InitPrimitiveResources();
    if (!g_primProgram) return;

    float ortho[16];
    OGL_GetOrthoMatrix(ortho);

    float z = (gDP.otherMode.depthSource == G_ZS_PRIM) ? gDP.primDepth.z : 0.0f;

    // Two triangles = 6 vertices
    float data[6 * 12] = {
        // Triangle 1
        (float)ulx,
        (float)uly,
        z,
        1.0f,
        color[0],
        color[1],
        color[2],
        color[3],
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        (float)lrx,
        (float)uly,
        z,
        1.0f,
        color[0],
        color[1],
        color[2],
        color[3],
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        (float)ulx,
        (float)lry,
        z,
        1.0f,
        color[0],
        color[1],
        color[2],
        color[3],
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        // Triangle 2
        (float)lrx,
        (float)uly,
        z,
        1.0f,
        color[0],
        color[1],
        color[2],
        color[3],
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        (float)lrx,
        (float)lry,
        z,
        1.0f,
        color[0],
        color[1],
        color[2],
        color[3],
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        (float)ulx,
        (float)lry,
        z,
        1.0f,
        color[0],
        color[1],
        color[2],
        color[3],
        0.0f,
        0.0f,
        0.0f,
        0.0f,
    };

    glUseProgram(g_primProgram);
    glUniformMatrix4fv(g_primUniOrtho, 1, GL_FALSE, ortho);
    glUniform1i(g_primUniUseOrtho, GL_TRUE);
    glUniform1i(g_primUniUseTexture, GL_FALSE);

    glBindVertexArray(g_primVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_primVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    OGL_UpdateCullFace();
    OGL_UpdateViewport();
    glEnable(GL_SCISSOR_TEST);
}

void OGL_DrawTexturedRect(float ulx, float uly, float lrx, float lry, float uls, float ult, float lrs, float lrt,
                          bool flip)
{
    GLVertex rect[2] = {
        {ulx,
         uly,
         gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z : gSP.viewport.nearz,
         1.0f,
         {/*gDP.blendColor.r, gDP.blendColor.g, gDP.blendColor.b, gDP.blendColor.a */ 1.0f, 1.0f, 1.0f, 0.0f},
         {1.0f, 1.0f, 1.0f, 1.0f},
         uls,
         ult,
         uls,
         ult,
         0.0f},
        {lrx,
         lry,
         gDP.otherMode.depthSource == G_ZS_PRIM ? gDP.primDepth.z : gSP.viewport.nearz,
         1.0f,
         {/*gDP.blendColor.r, gDP.blendColor.g, gDP.blendColor.b, gDP.blendColor.a*/ 1.0f, 1.0f, 1.0f, 0.0f},
         {1.0f, 1.0f, 1.0f, 1.0f},
         lrs,
         lrt,
         lrs,
         lrt,
         0.0f},
    };

    OGL_UpdateStates();

    glDisable(GL_CULL_FACE);
    glViewport(0, OGL.heightOffset, OGL.width, OGL.height);

    if (combiner.usesT0)
    {
        rect[0].s0 = rect[0].s0 * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
        rect[0].t0 = rect[0].t0 * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;
        rect[1].s0 = (rect[1].s0 + 1.0f) * cache.current[0]->shiftScaleS - gSP.textureTile[0]->fuls;
        rect[1].t0 = (rect[1].t0 + 1.0f) * cache.current[0]->shiftScaleT - gSP.textureTile[0]->fult;

        if ((cache.current[0]->maskS) && (fmod(rect[0].s0, cache.current[0]->width) == 0.0f) &&
            !(cache.current[0]->mirrorS))
        {
            rect[1].s0 -= rect[0].s0;
            rect[0].s0 = 0.0f;
        }

        if ((cache.current[0]->maskT) && (fmod(rect[0].t0, cache.current[0]->height) == 0.0f) &&
            !(cache.current[0]->mirrorT))
        {
            rect[1].t0 -= rect[0].t0;
            rect[0].t0 = 0.0f;
        }

        if (cache.current[0]->frameBufferTexture)
        {
            rect[0].s0 = cache.current[0]->offsetS + rect[0].s0;
            rect[0].t0 = cache.current[0]->offsetT - rect[0].t0;
            rect[1].s0 = cache.current[0]->offsetS + rect[1].s0;
            rect[1].t0 = cache.current[0]->offsetT - rect[1].t0;
        }

        if (OGL.ARB_multitexture) glActiveTexture(GL_TEXTURE0);

        if ((rect[0].s0 >= 0.0f) && (rect[1].s0 <= cache.current[0]->width))
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

        if ((rect[0].t0 >= 0.0f) && (rect[1].t0 <= cache.current[0]->height))
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        rect[0].s0 *= cache.current[0]->scaleS;
        rect[0].t0 *= cache.current[0]->scaleT;
        rect[1].s0 *= cache.current[0]->scaleS;
        rect[1].t0 *= cache.current[0]->scaleT;
    }

    if (combiner.usesT1 && OGL.ARB_multitexture)
    {
        rect[0].s1 = rect[0].s1 * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
        rect[0].t1 = rect[0].t1 * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;
        rect[1].s1 = (rect[1].s1 + 1.0f) * cache.current[1]->shiftScaleS - gSP.textureTile[1]->fuls;
        rect[1].t1 = (rect[1].t1 + 1.0f) * cache.current[1]->shiftScaleT - gSP.textureTile[1]->fult;

        if ((cache.current[1]->maskS) && (fmod(rect[0].s1, cache.current[1]->width) == 0.0f) &&
            !(cache.current[1]->mirrorS))
        {
            rect[1].s1 -= rect[0].s1;
            rect[0].s1 = 0.0f;
        }

        if ((cache.current[1]->maskT) && (fmod(rect[0].t1, cache.current[1]->height) == 0.0f) &&
            !(cache.current[1]->mirrorT))
        {
            rect[1].t1 -= rect[0].t1;
            rect[0].t1 = 0.0f;
        }

        if (cache.current[1]->frameBufferTexture)
        {
            rect[0].s1 = cache.current[1]->offsetS + rect[0].s1;
            rect[0].t1 = cache.current[1]->offsetT - rect[0].t1;
            rect[1].s1 = cache.current[1]->offsetS + rect[1].s1;
            rect[1].t1 = cache.current[1]->offsetT - rect[1].t1;
        }

        glActiveTexture(GL_TEXTURE1);

        if ((rect[0].s1 == 0.0f) && (rect[1].s1 <= cache.current[1]->width))
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

        if ((rect[0].t1 == 0.0f) && (rect[1].t1 <= cache.current[1]->height))
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        rect[0].s1 *= cache.current[1]->scaleS;
        rect[0].t1 *= cache.current[1]->scaleT;
        rect[1].s1 *= cache.current[1]->scaleS;
        rect[1].t1 *= cache.current[1]->scaleT;
    }

    if ((gDP.otherMode.cycleType == G_CYC_COPY) && !OGL.forceBilinear)
    {
        if (OGL.ARB_multitexture) glActiveTexture(GL_TEXTURE0);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    SetConstant(rect[0].color, combiner.vertex.color, combiner.vertex.alpha);

    if (OGL.EXT_secondary_color)
        SetConstant(rect[0].secondaryColor, combiner.vertex.secondaryColor, combiner.vertex.alpha);

    // ---- Core OpenGL: build VBO and draw with shader ----
    OGL_InitPrimitiveResources();
    if (!g_primProgram) return;

    float ortho[16];
    OGL_GetOrthoMatrix(ortho);

    // Compute per-vertex texcoords matching original glBegin(GL_QUADS) behavior
    float t0_ul_s = rect[0].s0, t0_ul_t = rect[0].t0;
    float t0_ur_s = rect[1].s0, t0_ur_t = rect[0].t0;
    float t0_lr_s = rect[1].s0, t0_lr_t = rect[1].t0;
    float t0_ll_s = rect[0].s0, t0_ll_t = rect[1].t0;

    float t1_ul_s = rect[0].s1, t1_ul_t = rect[0].t1;
    float t1_ur_s = rect[1].s1, t1_ur_t = rect[0].t1;
    float t1_lr_s = rect[1].s1, t1_lr_t = rect[1].t1;
    float t1_ll_s = rect[0].s1, t1_ll_t = rect[1].t1;

    if (!OGL.ARB_multitexture)
    {
        // Non-multitexture path with flip (matches original logic exactly)
        if (flip)
        {
            t0_ur_s = rect[1].s0;
            t0_ur_t = rect[0].t0;
            t0_ll_s = rect[1].s0;
            t0_ll_t = rect[0].t0;
        }
        else
        {
            t0_ur_s = rect[0].s0;
            t0_ur_t = rect[1].t0;
            t0_ll_s = rect[1].s0;
            t0_ll_t = rect[0].t0;
        }
    }

    // All vertices share the same primary color (SetConstant was called once)
    float rc = rect[0].color.r;
    float gc = rect[0].color.g;
    float bc = rect[0].color.b;
    float ac = rect[0].color.a;

    // Quad as 2 triangles (6 vertices): UL, UR, LL + UR, LR, LL
    float data[6 * 12] = {
        // Triangle 1: UL, UR, LL
        rect[0].x,
        rect[0].y,
        rect[0].z,
        1.0f,
        rc,
        gc,
        bc,
        ac,
        t0_ul_s,
        t0_ul_t,
        t1_ul_s,
        t1_ul_t,
        rect[1].x,
        rect[0].y,
        rect[0].z,
        1.0f,
        rc,
        gc,
        bc,
        ac,
        t0_ur_s,
        t0_ur_t,
        t1_ur_s,
        t1_ur_t,
        rect[0].x,
        rect[1].y,
        rect[0].z,
        1.0f,
        rc,
        gc,
        bc,
        ac,
        t0_ll_s,
        t0_ll_t,
        t1_ll_s,
        t1_ll_t,
        // Triangle 2: UR, LR, LL
        rect[1].x,
        rect[0].y,
        rect[0].z,
        1.0f,
        rc,
        gc,
        bc,
        ac,
        t0_ur_s,
        t0_ur_t,
        t1_ur_s,
        t1_ur_t,
        rect[1].x,
        rect[1].y,
        rect[0].z,
        1.0f,
        rc,
        gc,
        bc,
        ac,
        t0_lr_s,
        t0_lr_t,
        t1_lr_s,
        t1_lr_t,
        rect[0].x,
        rect[1].y,
        rect[0].z,
        1.0f,
        rc,
        gc,
        bc,
        ac,
        t0_ll_s,
        t0_ll_t,
        t1_ll_s,
        t1_ll_t,
    };

    glUseProgram(g_primProgram);
    glUniformMatrix4fv(g_primUniOrtho, 1, GL_FALSE, ortho);
    glUniform1i(g_primUniUseOrtho, GL_TRUE);
    glUniform1i(g_primUniUseTexture, combiner.usesT0 ? GL_TRUE : GL_FALSE);
    glUniform1i(g_primUniTexture0, 0);
    glUniform1i(g_primUniTexture1, 1);
    glUniform1i(glGetUniformLocation(g_primProgram, "uUseMultiTexture"),
                (combiner.usesT1 && OGL.ARB_multitexture) ? GL_TRUE : GL_FALSE);

    glActiveTexture(GL_TEXTURE0);
    if (OGL.ARB_multitexture) glActiveTexture(GL_TEXTURE0);

    glBindVertexArray(g_primVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_primVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    OGL_UpdateCullFace();
    OGL_UpdateViewport();
}

void OGL_ClearDepthBuffer()
{
    OGL_UpdateStates();
    glDepthMask(TRUE);
    //	glEnable( GL_DEPTH_TEST );
    glClear(GL_DEPTH_BUFFER_BIT);

    OGL_UpdateDepthUpdate();
}

void OGL_ClearColorBuffer(float *color)
{
    glClearColor(color[0], color[1], color[2], color[3]);
    glClear(GL_COLOR_BUFFER_BIT);
}

// ---- Core OpenGL Blit Resources (for FrameBuffer.cpp) ----
static GLuint g_blitVAO = 0;
static GLuint g_blitVBO = 0;
static GLuint g_blitProgram = 0;
static GLint g_blitUniOrtho = -1;
static GLint g_blitUniTexture = -1;

static const char *g_blitVS = GLSL_VERSION_HEADER R"(
#ifdef GL_ES
precision highp float;
#endif

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
uniform mat4 uOrtho;
out vec2 vTexCoord;
void main() {
    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

static const char *g_blitFS = GLSL_VERSION_HEADER R"(
#ifdef GL_ES
precision mediump float;
precision mediump sampler2D;
#endif

in vec2 vTexCoord;
uniform sampler2D uTexture;
out vec4 FragColor;
void main() {
    FragColor = texture(uTexture, vTexCoord);
}
)";

static GLuint CompileShader(GLenum type, const char *source)
{
    GLuint shader = glCreateShader(type);
    const GLchar *src = source;
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char buf[512];
        glGetShaderInfoLog(shader, 512, nullptr, buf);
        std::string narrow(buf);
        std::wstring wide(narrow.begin(), narrow.end());
        g_ef->log_error(std::format(L"Shader compilation failed: {}", wide.c_str()).c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void OGL_InitBlitResources()
{
    if (g_blitProgram) return;
    GLuint vs = CompileShader(GL_VERTEX_SHADER, g_blitVS);
    GLuint fs = CompileShader(GL_FRAGMENT_SHADER, g_blitFS);
    if (!vs || !fs) return;
    g_blitProgram = glCreateProgram();
    glAttachShader(g_blitProgram, vs);
    glAttachShader(g_blitProgram, fs);
    glBindAttribLocation(g_blitProgram, 0, "aPos");
    glBindAttribLocation(g_blitProgram, 1, "aTexCoord");
    glLinkProgram(g_blitProgram);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linkStatus;
    glGetProgramiv(g_blitProgram, GL_LINK_STATUS, &linkStatus);
    if (!linkStatus)
    {
        char buf[512];
        glGetProgramInfoLog(g_blitProgram, 512, nullptr, buf);
        std::string narrow(buf);
        std::wstring wide(narrow.begin(), narrow.end());
        g_ef->log_error(std::format(L"Shader link failed: {}", wide.c_str()).c_str());
        glDeleteProgram(g_blitProgram);
        g_blitProgram = 0;
        return;
    }
    g_blitUniOrtho = glGetUniformLocation(g_blitProgram, "uOrtho");
    g_blitUniTexture = glGetUniformLocation(g_blitProgram, "uTexture");

    glGenVertexArrays(1, &g_blitVAO);
    glGenBuffers(1, &g_blitVBO);
    glBindVertexArray(g_blitVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_blitVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindVertexArray(0);
}

void OGL_DestroyBlitResources()
{
    if (g_blitVAO)
    {
        glDeleteVertexArrays(1, &g_blitVAO);
        g_blitVAO = 0;
    }
    if (g_blitVBO)
    {
        glDeleteBuffers(1, &g_blitVBO);
        g_blitVBO = 0;
    }
    if (g_blitProgram)
    {
        glDeleteProgram(g_blitProgram);
        g_blitProgram = 0;
    }
    g_blitUniOrtho = -1;
    g_blitUniTexture = -1;
}

void OGL_BlitTexture(GLuint texture, float x, float y, float w, float h, float u1, float v1)
{
    if (!g_blitProgram) OGL_InitBlitResources();
    if (!g_blitProgram) return;

    // Ortho(0, width, 0, height, -1, 1)
    float r = (float)OGL.width;
    float t = (float)OGL.height;
    float ortho[16] = {2.0f / r, 0, 0, 0, 0, 2.0f / t, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1};

    float data[6 * 4] = {
        x, y,     0.0f, 0.0f, x + w, y, u1, 0.0f, x,     y + h, 0.0f, v1,
        x, y + h, 0.0f, v1,   x + w, y, u1, 0.0f, x + w, y + h, u1,   v1,
    };

    glUseProgram(g_blitProgram);
    glUniformMatrix4fv(g_blitUniOrtho, 1, GL_FALSE, ortho);
    glUniform1i(g_blitUniTexture, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);

    glBindVertexArray(g_blitVAO);
    glBindBuffer(GL_ARRAY_BUFFER, g_blitVBO);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(data), data);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

// ---- N64 Combiner Uniform Upload (replaces fixed-function glTexEnv) ----

void OGL_SetN64Combiner(const N64CombinerState *state)
{
    OGL_InitN64Resources();
    if (!g_n64Program) return;

    glUseProgram(g_n64Program);

    // Colors
    if (g_n64UniPrimColor >= 0) glUniform4fv(g_n64UniPrimColor, 1, state->primColor);
    if (g_n64UniEnvColor >= 0) glUniform4fv(g_n64UniEnvColor, 1, state->envColor);
    if (g_n64UniPrimLODFrac >= 0) glUniform1f(g_n64UniPrimLODFrac, state->primLODFrac);
    if (g_n64UniFogColor >= 0) glUniform4fv(g_n64UniFogColor, 1, state->fogColor);

    // Fog
    if (g_n64UniFogEnabled >= 0) glUniform1i(g_n64UniFogEnabled, state->fogEnabled);
    if (g_n64UniFogMultiplier >= 0) glUniform1f(g_n64UniFogMultiplier, state->fogMultiplier);
    if (g_n64UniFogOffset >= 0) glUniform1f(g_n64UniFogOffset, state->fogOffset);

    // Alpha test
    if (g_n64UniAlphaTestEnabled >= 0) glUniform1i(g_n64UniAlphaTestEnabled, state->alphaTestEnabled);
    if (g_n64UniAlphaTestThreshold >= 0) glUniform1f(g_n64UniAlphaTestThreshold, state->alphaTestThreshold);
    if (g_n64UniAlphaTestFunction >= 0) glUniform1i(g_n64UniAlphaTestFunction, state->alphaTestFunction);

    // Polygon stipple
    if (g_n64UniStippleEnabled >= 0) glUniform1i(g_n64UniStippleEnabled, state->stippleEnabled);
    if (g_n64UniStipplePattern >= 0) glUniform1i(g_n64UniStipplePattern, state->stipplePattern);

    // Combiner: pack canonical (A, B, C, D) into ivec4 uniforms per cycle per channel
    if (g_n64UniCombine0RGB >= 0)
        glUniform4i(g_n64UniCombine0RGB, state->saRGB0, state->sbRGB0, state->mRGB0, state->aRGB0);
    if (g_n64UniCombine0A >= 0) glUniform4i(g_n64UniCombine0A, state->saA0, state->sbA0, state->mA0, state->aA0);
    if (g_n64UniCombine1RGB >= 0)
        glUniform4i(g_n64UniCombine1RGB, state->saRGB1, state->sbRGB1, state->mRGB1, state->aRGB1);
    if (g_n64UniCombine1A >= 0) glUniform4i(g_n64UniCombine1A, state->saA1, state->sbA1, state->mA1, state->aA1);
    if (g_n64UniNumCycles >= 0) glUniform1i(g_n64UniNumCycles, state->numCycles);
}

void OGL_UpdateN64CombinerColors(void)
{
    // Re-upload current combiner state with updated colors.
    // The full state is re-uploaded for simplicity; the driver will
    // filter out uniforms that haven't changed.
    if (combiner.current && combiner.current->compiled)
    {
        Set_unified_combiner(combiner.current->compiled);
    }
}
