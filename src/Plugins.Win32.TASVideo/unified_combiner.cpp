#include "stdafx.h"
#include "OpenGL.h"
#include "Combiner.h"
#include "unified_combiner.h"
#include "Textures.h"
#include "gDP.h"

// ---------------------------------------------------------------------------
// Legacy texture-env stage descriptors.
//
// These structs and tables are kept intact because Compile() still uses them
// to derive the per-combiner flags (usesT0, usesT1, vertex colour source)
// that the rest of the pipeline reads.  The GL-source/operand fields are no
// longer passed to glTexEnv — they are simply dead storage after Compile().
// ---------------------------------------------------------------------------

struct UCTexArg
{
    GLenum source, operand;
};

struct UCTexStage
{
    WORD constant;
    BOOL used;
    GLenum combine;
    UCTexArg arg0, arg1, arg2;
    WORD outputTexture;
};

struct UnifiedCompiledCombiner
{
    BOOL usesT0, usesT1, usesNoise;
    WORD usedUnits;
    struct
    {
        WORD color, secondaryColor, alpha;
    } vertex;
    UCTexStage color[8];
    UCTexStage alpha[8];

    // NEW: Canonical combiner inputs for direct shader upload.
    // These are decoded from gDP.combine.mux in Compile() and uploaded
    // as uniforms in Set().  They represent the original (A-B)*C+D form
    // before stage simplification.
    int saRGB0, sbRGB0, mRGB0, aRGB0;
    int saA0, sbA0, mA0, aA0;
    int saRGB1, sbRGB1, mRGB1, aRGB1;
    int saA1, sbA1, mA1, aA1;
    int numCycles;
};

static UCTexArg TexEnvArgs[] = {
    // CMB
    {GL_PREVIOUS_ARB, GL_SRC_COLOR},
    // T0
    {GL_TEXTURE, GL_SRC_COLOR},
    // T1
    {GL_TEXTURE, GL_SRC_COLOR},
    // PRIM
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // SHADE
    {GL_PRIMARY_COLOR_ARB, GL_SRC_COLOR},
    // ENV
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // CENTER
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // SCALE
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // CMBALPHA
    {GL_PREVIOUS_ARB, GL_SRC_ALPHA},
    // T0ALPHA
    {GL_TEXTURE, GL_SRC_ALPHA},
    // T1ALPHA
    {GL_TEXTURE, GL_SRC_ALPHA},
    // PRIMALPHA
    {GL_CONSTANT_ARB, GL_SRC_ALPHA},
    // SHADEALPHA
    {GL_PRIMARY_COLOR_ARB, GL_SRC_ALPHA},
    // ENVALPHA
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // LODFRAC
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // PRIMLODFRAC
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // NOISE
    {GL_TEXTURE, GL_SRC_COLOR},
    // K4
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // K5
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // ONE
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
    // ZERO
    {GL_CONSTANT_ARB, GL_SRC_COLOR},
};

#define SetColorCombinerArg(n, a, i)                                                                                   \
    if (TexEnvArgs[i].source == GL_CONSTANT_ARB)                                                                       \
    {                                                                                                                  \
        if ((i > 5) && ((envCombiner->alpha[n].constant == COMBINED) || (envCombiner->alpha[n].constant == i)))        \
        {                                                                                                              \
            envCombiner->alpha[n].constant = i;                                                                        \
            envCombiner->color[n].a.source = GL_CONSTANT_ARB;                                                          \
            envCombiner->color[n].a.operand = GL_SRC_ALPHA;                                                            \
        }                                                                                                              \
        else if ((i > 5) && ((envCombiner->vertex.alpha == COMBINED) || (envCombiner->vertex.alpha == i)))             \
        {                                                                                                              \
            envCombiner->vertex.alpha = i;                                                                             \
            envCombiner->color[n].a.source = GL_PRIMARY_COLOR_ARB;                                                     \
            envCombiner->color[n].a.operand = GL_SRC_ALPHA;                                                            \
        }                                                                                                              \
        else if ((envCombiner->color[n].constant == COMBINED) || (envCombiner->color[n].constant == i))                \
        {                                                                                                              \
            envCombiner->color[n].constant = i;                                                                        \
            envCombiner->color[n].a.source = GL_CONSTANT_ARB;                                                          \
            envCombiner->color[n].a.operand = GL_SRC_COLOR;                                                            \
        }                                                                                                              \
        else if ((envCombiner->vertex.color == COMBINED) || (envCombiner->vertex.color == i))                          \
        {                                                                                                              \
            envCombiner->vertex.color = i;                                                                             \
            envCombiner->color[n].a.source = GL_PRIMARY_COLOR_ARB;                                                     \
            envCombiner->color[n].a.operand = GL_SRC_COLOR;                                                            \
        }                                                                                                              \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
        envCombiner->color[n].a.source = TexEnvArgs[i].source;                                                         \
        envCombiner->color[n].a.operand = TexEnvArgs[i].operand;                                                       \
    }

#define SetColorCombinerValues(n, a, s, o)                                                                             \
    envCombiner->color[n].a.source = s;                                                                                \
    envCombiner->color[n].a.operand = o

#define SetAlphaCombinerArg(n, a, i)                                                                                   \
    if (TexEnvArgs[i].source == GL_CONSTANT_ARB)                                                                       \
    {                                                                                                                  \
        if ((envCombiner->alpha[n].constant == COMBINED) || (envCombiner->alpha[n].constant == i))                     \
        {                                                                                                              \
            envCombiner->alpha[n].constant = i;                                                                        \
            envCombiner->alpha[n].a.source = GL_CONSTANT_ARB;                                                          \
            envCombiner->alpha[n].a.operand = GL_SRC_ALPHA;                                                            \
        }                                                                                                              \
        else if ((envCombiner->vertex.alpha == COMBINED) || (envCombiner->vertex.alpha == i))                          \
        {                                                                                                              \
            envCombiner->vertex.alpha = i;                                                                             \
            envCombiner->alpha[n].a.source = GL_PRIMARY_COLOR_ARB;                                                     \
            envCombiner->alpha[n].a.operand = GL_SRC_ALPHA;                                                            \
        }                                                                                                              \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
        envCombiner->alpha[n].a.source = TexEnvArgs[i].source;                                                         \
        envCombiner->alpha[n].a.operand = GL_SRC_ALPHA;                                                                \
    }

#define SetAlphaCombinerValues(n, a, s, o)                                                                             \
    envCombiner->alpha[n].a.source = s;                                                                                \
    envCombiner->alpha[n].a.operand = o

static void Init()
{
    for (int i = 0; i < OGL.maxTextureUnits; i++) TextureCache_ActivateDummy(i);
}

static void Uninit()
{
    // Nothing to clean up — shader uniforms are managed by the GL driver.
}

static void UpdateColors(UnifiedCompiledCombiner * /*envCombiner*/)
{
    // Fixed-function glTexEnvfv(GL_TEXTURE_ENV_COLOR) has been replaced by
    // shader uniforms.  Ask the renderer to re-upload the current combiner
    // state (which includes the latest prim/env colours from gDP).
    OGL_UpdateN64CombinerColors();
}

static void Compile(UnifiedCompiledCombiner *envCombiner, Combiner *color, Combiner *alpha)
{
    int curUnit, combinedUnit;

    for (int i = 0; i < OGL.maxTextureUnits; i++)
    {
        envCombiner->color[i].combine = GL_REPLACE;
        envCombiner->alpha[i].combine = GL_REPLACE;

        SetColorCombinerValues(i, arg0, GL_PREVIOUS_ARB, GL_SRC_COLOR);
        SetColorCombinerValues(i, arg1, GL_PREVIOUS_ARB, GL_SRC_COLOR);
        SetColorCombinerValues(i, arg2, GL_PREVIOUS_ARB, GL_SRC_COLOR);
        envCombiner->color[i].constant = COMBINED;
        envCombiner->color[i].outputTexture = GL_TEXTURE0 + i;

        SetAlphaCombinerValues(i, arg0, GL_PREVIOUS_ARB, GL_SRC_ALPHA);
        SetAlphaCombinerValues(i, arg1, GL_PREVIOUS_ARB, GL_SRC_ALPHA);
        SetAlphaCombinerValues(i, arg2, GL_PREVIOUS_ARB, GL_SRC_ALPHA);
        envCombiner->alpha[i].constant = COMBINED;
        envCombiner->alpha[i].outputTexture = GL_TEXTURE0 + i;
    }

    envCombiner->usesT0 = FALSE;
    envCombiner->usesT1 = FALSE;

    envCombiner->vertex.color = COMBINED;
    envCombiner->vertex.secondaryColor = COMBINED;
    envCombiner->vertex.alpha = COMBINED;

    curUnit = 0;

    for (int i = 0; i < alpha->numStages; i++)
    {
        for (int j = 0; j < alpha->stage[i].numOps; j++)
        {
            float sb = 0.0f;
            if (alpha->stage[i].op[j].param1 == PRIMITIVE_ALPHA)
                sb = gDP.primColor.a;
            else if (alpha->stage[i].op[j].param1 == ENV_ALPHA)
                sb = gDP.envColor.a;
            else if (alpha->stage[i].op[j].param1 == ONE)
                sb = 1.0f;

            if (((alpha->stage[i].numOps - j) >= 3) && (alpha->stage[i].op[j].op == SUB) &&
                (alpha->stage[i].op[j + 1].op == MUL) && (alpha->stage[i].op[j + 2].op == ADD) && (sb > 0.5f))
            {
                envCombiner->usesT0 |= alpha->stage[i].op[j].param1 == TEXEL0_ALPHA;
                envCombiner->usesT1 |= alpha->stage[i].op[j].param1 == TEXEL1_ALPHA;

                if (alpha->stage[i].op[j].param1 == ONE)
                {
                    SetAlphaCombinerValues(curUnit, arg0, envCombiner->alpha[curUnit].arg0.source,
                                           GL_ONE_MINUS_SRC_ALPHA);
                }
                else
                {
                    envCombiner->alpha[curUnit].combine = GL_SUBTRACT_ARB;
                    SetAlphaCombinerValues(curUnit, arg1, envCombiner->alpha[curUnit].arg0.source, GL_SRC_ALPHA);
                    SetAlphaCombinerArg(curUnit, arg0, alpha->stage[i].op[j].param1);
                    curUnit++;
                }
                j++;

                envCombiner->usesT0 |= alpha->stage[i].op[j].param1 == TEXEL0_ALPHA;
                envCombiner->usesT1 |= alpha->stage[i].op[j].param1 == TEXEL1_ALPHA;
                envCombiner->alpha[curUnit].combine = GL_MODULATE;
                SetAlphaCombinerArg(curUnit, arg1, alpha->stage[i].op[j].param1);
                curUnit++;
                j++;

                envCombiner->usesT0 |= alpha->stage[i].op[j].param1 == TEXEL0_ALPHA;
                envCombiner->usesT1 |= alpha->stage[i].op[j].param1 == TEXEL1_ALPHA;
                envCombiner->alpha[curUnit].combine = GL_SUBTRACT_ARB;
                SetAlphaCombinerArg(curUnit, arg0, alpha->stage[i].op[j].param1);
                curUnit++;
            }
            else
            {
                envCombiner->usesT0 |= alpha->stage[i].op[j].param1 == TEXEL0_ALPHA;
                envCombiner->usesT1 |= alpha->stage[i].op[j].param1 == TEXEL1_ALPHA;

                switch (alpha->stage[i].op[j].op)
                {
                case LOAD:
                    envCombiner->alpha[curUnit].combine = GL_REPLACE;
                    SetAlphaCombinerArg(curUnit, arg0, alpha->stage[i].op[j].param1);
                    break;

                case SUB:
                    if ((j > 0) && (alpha->stage[i].op[j - 1].op == LOAD) && (alpha->stage[i].op[j - 1].param1 == ONE))
                    {
                        SetAlphaCombinerArg(curUnit, arg0, alpha->stage[i].op[j].param1);
                        envCombiner->alpha[curUnit].arg0.operand = GL_ONE_MINUS_SRC_ALPHA;
                    }
                    else
                    {
                        envCombiner->alpha[curUnit].combine = GL_SUBTRACT_ARB;
                        SetAlphaCombinerArg(curUnit, arg1, alpha->stage[i].op[j].param1);
                        curUnit++;
                    }
                    break;

                case MUL:
                    envCombiner->alpha[curUnit].combine = GL_MODULATE;
                    SetAlphaCombinerArg(curUnit, arg1, alpha->stage[i].op[j].param1);
                    curUnit++;
                    break;

                case ADD:
                    envCombiner->alpha[curUnit].combine = GL_ADD;
                    SetAlphaCombinerArg(curUnit, arg1, alpha->stage[i].op[j].param1);
                    curUnit++;
                    break;

                case INTER:
                    envCombiner->usesT0 |= (alpha->stage[i].op[j].param2 == TEXEL0_ALPHA) ||
                                           (alpha->stage[i].op[j].param3 == TEXEL0_ALPHA);
                    envCombiner->usesT1 |= (alpha->stage[i].op[j].param2 == TEXEL1_ALPHA) ||
                                           (alpha->stage[i].op[j].param3 == TEXEL1_ALPHA);
                    envCombiner->alpha[curUnit].combine = GL_INTERPOLATE_ARB;
                    SetAlphaCombinerArg(curUnit, arg0, alpha->stage[i].op[j].param1);
                    SetAlphaCombinerArg(curUnit, arg1, alpha->stage[i].op[j].param2);
                    SetAlphaCombinerArg(curUnit, arg2, alpha->stage[i].op[j].param3);
                    curUnit++;
                    break;
                }
            }
        }
        combinedUnit = max(curUnit - 1, 0);
    }

    envCombiner->usedUnits = max(curUnit, 1);

    curUnit = 0;
    for (int i = 0; i < color->numStages; i++)
    {
        for (int j = 0; j < color->stage[i].numOps; j++)
        {
            float sb = 0.0f;
            if (color->stage[i].op[j].param1 == PRIMITIVE)
                sb = (gDP.primColor.r + gDP.primColor.b + gDP.primColor.g) / 3.0f;
            else if (color->stage[i].op[j].param1 == ENVIRONMENT)
                sb = (gDP.envColor.r + gDP.envColor.b + gDP.envColor.g) / 3.0f;

            if (((color->stage[i].numOps - j) >= 3) && (color->stage[i].op[j].op == SUB) &&
                (color->stage[i].op[j + 1].op == MUL) && (color->stage[i].op[j + 2].op == ADD) && (sb > 0.5f))
            {
                envCombiner->usesT0 |=
                    (color->stage[i].op[j].param1 == TEXEL0) || (color->stage[i].op[j].param1 == TEXEL0_ALPHA);
                envCombiner->usesT1 |=
                    (color->stage[i].op[j].param1 == TEXEL1) || (color->stage[i].op[j].param1 == TEXEL1_ALPHA);

                envCombiner->color[curUnit].combine = GL_SUBTRACT_ARB;
                SetColorCombinerValues(curUnit, arg1, envCombiner->color[curUnit].arg0.source,
                                       envCombiner->color[curUnit].arg0.operand);
                SetColorCombinerArg(curUnit, arg0, color->stage[i].op[j].param1);
                curUnit++;
                j++;

                envCombiner->usesT0 |=
                    (color->stage[i].op[j].param1 == TEXEL0) || (color->stage[i].op[j].param1 == TEXEL0_ALPHA);
                envCombiner->usesT1 |=
                    (color->stage[i].op[j].param1 == TEXEL1) || (color->stage[i].op[j].param1 == TEXEL1_ALPHA);
                envCombiner->color[curUnit].combine = GL_MODULATE;
                SetColorCombinerArg(curUnit, arg1, color->stage[i].op[j].param1);
                curUnit++;
                j++;

                envCombiner->usesT0 |=
                    (color->stage[i].op[j].param1 == TEXEL0) || (color->stage[i].op[j].param1 == TEXEL0_ALPHA);
                envCombiner->usesT1 |=
                    (color->stage[i].op[j].param1 == TEXEL1) || (color->stage[i].op[j].param1 == TEXEL1_ALPHA);
                envCombiner->color[curUnit].combine = GL_SUBTRACT_ARB;
                SetColorCombinerArg(curUnit, arg0, color->stage[i].op[j].param1);
                curUnit++;
            }
            else
            {
                envCombiner->usesT0 |=
                    (color->stage[i].op[j].param1 == TEXEL0) || (color->stage[i].op[j].param1 == TEXEL0_ALPHA);
                envCombiner->usesT1 |=
                    (color->stage[i].op[j].param1 == TEXEL1) || (color->stage[i].op[j].param1 == TEXEL1_ALPHA);

                switch (color->stage[i].op[j].op)
                {
                case LOAD:
                    envCombiner->color[curUnit].combine = GL_REPLACE;
                    SetColorCombinerArg(curUnit, arg0, color->stage[i].op[j].param1);
                    break;

                case SUB:
                    if ((j > 0) && (color->stage[i].op[j - 1].op == LOAD) && (color->stage[i].op[j - 1].param1 == ONE))
                    {
                        SetColorCombinerArg(curUnit, arg0, color->stage[i].op[j].param1);
                        envCombiner->color[curUnit].arg0.operand = GL_ONE_MINUS_SRC_COLOR;
                    }
                    else
                    {
                        envCombiner->color[curUnit].combine = GL_SUBTRACT_ARB;
                        SetColorCombinerArg(curUnit, arg1, color->stage[i].op[j].param1);
                        curUnit++;
                    }
                    break;

                case MUL:
                    envCombiner->color[curUnit].combine = GL_MODULATE;
                    SetColorCombinerArg(curUnit, arg1, color->stage[i].op[j].param1);
                    curUnit++;
                    break;

                case ADD:
                    envCombiner->color[curUnit].combine = GL_ADD;
                    SetColorCombinerArg(curUnit, arg1, color->stage[i].op[j].param1);
                    curUnit++;
                    break;

                case INTER:
                    envCombiner->usesT0 |= (color->stage[i].op[j].param2 == TEXEL0) ||
                                           (color->stage[i].op[j].param3 == TEXEL0) ||
                                           (color->stage[i].op[j].param3 == TEXEL0_ALPHA);
                    envCombiner->usesT1 |= (color->stage[i].op[j].param2 == TEXEL1) ||
                                           (color->stage[i].op[j].param3 == TEXEL1) ||
                                           (color->stage[i].op[j].param3 == TEXEL1_ALPHA);

                    envCombiner->color[curUnit].combine = GL_INTERPOLATE_ARB;
                    SetColorCombinerArg(curUnit, arg0, color->stage[i].op[j].param1);
                    SetColorCombinerArg(curUnit, arg1, color->stage[i].op[j].param2);
                    SetColorCombinerArg(curUnit, arg2, color->stage[i].op[j].param3);
                    curUnit++;
                    break;
                }
            }
        }
        combinedUnit = max(curUnit - 1, 0);
    }

    envCombiner->usedUnits = max(curUnit, (int)envCombiner->usedUnits);

    // -------------------------------------------------------------------
    // NEW: Decode the original canonical (A-B)*C+D form directly from
    // gDP.combine.mux.  The shader needs these values to replicate the
    // N64 combiner exactly.  The stage simplification above is kept only
    // to produce usesT0 / usesT1 / vertex colour source flags.
    // -------------------------------------------------------------------
    gDPCombine combine = gDP.combine;

    envCombiner->saRGB0 = saRGBExpanded[combine.saRGB0];
    envCombiner->sbRGB0 = sbRGBExpanded[combine.sbRGB0];
    envCombiner->mRGB0 = mRGBExpanded[combine.mRGB0];
    envCombiner->aRGB0 = aRGBExpanded[combine.aRGB0];

    envCombiner->saA0 = saAExpanded[combine.saA0];
    envCombiner->sbA0 = sbAExpanded[combine.sbA0];
    envCombiner->mA0 = mAExpanded[combine.mA0];
    envCombiner->aA0 = aAExpanded[combine.aA0];

    envCombiner->saRGB1 = saRGBExpanded[combine.saRGB1];
    envCombiner->sbRGB1 = sbRGBExpanded[combine.sbRGB1];
    envCombiner->mRGB1 = mRGBExpanded[combine.mRGB1];
    envCombiner->aRGB1 = aRGBExpanded[combine.aRGB1];

    envCombiner->saA1 = saAExpanded[combine.saA1];
    envCombiner->sbA1 = sbAExpanded[combine.sbA1];
    envCombiner->mA1 = mAExpanded[combine.mA1];
    envCombiner->aA1 = aAExpanded[combine.aA1];

    envCombiner->numCycles = (gDP.otherMode.cycleType == G_CYC_2CYCLE) ? 2 : 1;
}

static void Set(UnifiedCompiledCombiner *envCombiner)
{
    // Update the global CombinerInfo flags that the vertex-building code
    // in OpenGL.cpp reads (e.g.  combiner.usesT0, combiner.vertex.color).
    combiner.usesT0 = envCombiner->usesT0;
    combiner.usesT1 = envCombiner->usesT1;
    combiner.usesNoise = FALSE;

    combiner.vertex.color = envCombiner->vertex.color;
    combiner.vertex.secondaryColor = envCombiner->vertex.secondaryColor;
    combiner.vertex.alpha = envCombiner->vertex.alpha;

    // -----------------------------------------------------------------
    // Build the N64 combiner state and upload it to the shader as
    // uniforms.  This replaces the entire fixed-function glTexEnv block.
    // -----------------------------------------------------------------
    N64CombinerState state = {};

    // Constant colours from gDP
    state.primColor[0] = gDP.primColor.r;
    state.primColor[1] = gDP.primColor.g;
    state.primColor[2] = gDP.primColor.b;
    state.primColor[3] = gDP.primColor.a;

    state.envColor[0] = gDP.envColor.r;
    state.envColor[1] = gDP.envColor.g;
    state.envColor[2] = gDP.envColor.b;
    state.envColor[3] = gDP.envColor.a;

    state.primLODFrac = gDP.primColor.l;

    state.fogColor[0] = gDP.fogColor.r;
    state.fogColor[1] = gDP.fogColor.g;
    state.fogColor[2] = gDP.fogColor.b;
    state.fogColor[3] = gDP.fogColor.a;

    state.fogEnabled = ((gSP.geometryMode & G_FOG) && OGL.fog) ? 1 : 0;
    state.fogMultiplier = (float)gSP.fog.multiplier;
    state.fogOffset = (float)gSP.fog.offset;

    // Alpha test
    if ((gDP.otherMode.alphaCompare == G_AC_THRESHOLD) && !(gDP.otherMode.alphaCvgSel))
    {
        state.alphaTestEnabled = 1;
        state.alphaTestThreshold = gDP.blendColor.a;
        state.alphaTestFunction = (gDP.blendColor.a > 0.0f) ? 1 : 0; // 1 = GEQUAL, 0 = GREATER
    }
    else if (gDP.otherMode.cvgXAlpha)
    {
        state.alphaTestEnabled = 1;
        state.alphaTestThreshold = 0.5f;
        state.alphaTestFunction = 1; // GEQUAL
    }
    else
    {
        state.alphaTestEnabled = 0;
        state.alphaTestThreshold = 0.0f;
        state.alphaTestFunction = 0;
    }

    // Stipple (dithered alpha)
    if (OGL.usePolygonStipple && (gDP.otherMode.alphaCompare == G_AC_DITHER) && !(gDP.otherMode.alphaCvgSel))
    {
        state.stippleEnabled = 1;
        state.stippleAlpha = (int)(gDP.envColor.a * 255.0f);
        state.stipplePattern = OGL.lastStipple;
    }
    else
    {
        state.stippleEnabled = 0;
        state.stippleAlpha = 0;
        state.stipplePattern = 0;
    }

    // Canonical combiner inputs (decoded in Compile() from gDP.combine.mux)
    state.saRGB0 = envCombiner->saRGB0;
    state.sbRGB0 = envCombiner->sbRGB0;
    state.mRGB0 = envCombiner->mRGB0;
    state.aRGB0 = envCombiner->aRGB0;

    state.saA0 = envCombiner->saA0;
    state.sbA0 = envCombiner->sbA0;
    state.mA0 = envCombiner->mA0;
    state.aA0 = envCombiner->aA0;

    state.saRGB1 = envCombiner->saRGB1;
    state.sbRGB1 = envCombiner->sbRGB1;
    state.mRGB1 = envCombiner->mRGB1;
    state.aRGB1 = envCombiner->aRGB1;

    state.saA1 = envCombiner->saA1;
    state.sbA1 = envCombiner->sbA1;
    state.mA1 = envCombiner->mA1;
    state.aA1 = envCombiner->aA1;

    state.numCycles = envCombiner->numCycles;

    OGL_SetN64Combiner(&state);
}

// =========================================================================
// Public interface
// =========================================================================

void Init_unified_combiner()
{
    Init();
}

void Uninit_unified_combiner()
{
    Uninit();
}

UnifiedCompiledCombiner *Compile_unified_combiner(Combiner *color, Combiner *alpha)
{
    auto compiled = (UnifiedCompiledCombiner *)calloc(1, sizeof(UnifiedCompiledCombiner));
    Compile(compiled, color, alpha);
    return compiled;
}

void Update_unified_combiner_Colors(UnifiedCompiledCombiner *compiled)
{
    UpdateColors(compiled);
}

void Set_unified_combiner(UnifiedCompiledCombiner *compiled)
{
    Set(compiled);
}

void BeginTextureUpdate_unified_combiner()
{
    // With shader-based rendering there is no fixed-function texture-unit
    // state to disable.  Texture binding is handled by the caller via
    // glActiveTexture/glBindTexture before drawing.
}

void EndTextureUpdate_unified_combiner()
{
    if (combiner.current && combiner.current->compiled) Set_unified_combiner(combiner.current->compiled);
}
