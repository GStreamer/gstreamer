// these includes don't do a lot in Linux, they are more needed in Win32 (for the OpenGL function call pointers)
// but they are used at least for checking if the necessary extensions are present

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ARB_multitexture.h"
#include "NV_register_combiners.h"
#include "EXT_paletted_texture.h"

/***********************************************************************************************
 INTERESTING PART:                                                                            *
 handles initialization of the Nvidia register combiners for YUV->RGB conversion. 
 This code was created by Jens Schneider <schneider@glhint.de>                      
 ***********************************************************************************************/

GLuint Yhandle;
GLuint Uhandle;
GLuint Vhandle;
unsigned char *YPlane;
unsigned char *UPlane;
unsigned char *VPlane;

// YUV 4:2:2 example
unsigned int Ywidth = 512, Yheight = 512;
unsigned int UVwidth = 256, UVheight = 512;

int tex_xsize, tex_ysize;

void
GenerateRGBTables (unsigned char *Ytable,       // Y-palette
    unsigned char *Utable,      // U-palette
    unsigned char *Vtable,      // V-palette
    float *bias,                // bias (fourth vector to be added)
    float *Uscale,              // scaling color for U
    float *Vscale)              // scaling color for V
{
  int i;
  const float mat[9] = {        // the modified YUV->RGB matrix
    +1.130469478f, -0.058755723f, +1.596026304f,
    +1.130469478f, -0.450515935f, -0.812967512f,
    +1.130469478f, +1.958477882f, 0.0f
  };

#define COMPRESS(a)(0.5f*(a)+128.0f)    // counter-piece to EXPAND_NORMAL
#define fCOMPRESS(a) (0.5f*(a)+0.5f);
#define XCLAMP(a) ((a)<0.0f ? 0.0f : ((a)>255.0f ? 255.0f : (a)))       // should not be necessary, but what do you know.
  bias[0] = fCOMPRESS (-0.842580964f);
  bias[1] = fCOMPRESS (+0.563287723f);
  bias[2] = fCOMPRESS (-1.0f);
  bias[3] = 0.0f;
  Uscale[0] = 8.0f / 255.0f;
  Uscale[1] = 60.0f / 255.0f;
  Uscale[2] = 250.0f / 255.0f;
  Uscale[3] = 0.0f;
  Vscale[0] = 204.0f / 255.0f;
  Vscale[1] = 105.0f / 255.0f;
  Vscale[2] = 0.5f;
  Vscale[3] = 0.0f;
  for (i = 0; i < 256; i++) {
    // Y-table holds unsigned values
    Ytable[3 * i] = (unsigned char) XCLAMP (mat[0] * (float) i);        // R
    Ytable[3 * i + 1] = (unsigned char) XCLAMP (mat[3] * (float) i);    // G
    Ytable[3 * i + 2] = (unsigned char) XCLAMP (mat[6] * (float) i);    // B
    // U-table holds signed values
    Utable[3 * i] = (unsigned char) XCLAMP (COMPRESS (255.0f / 16.0f * mat[1] * (float) i));    // R
    Utable[3 * i + 1] = (unsigned char) XCLAMP (COMPRESS (255.0f / 120.0f * mat[4] * (float) i));       // G
    Utable[3 * i + 2] = (unsigned char) XCLAMP (COMPRESS (255.0f / 500.0f * mat[7] * (float) i));       // B
    // V-table holds signed values
    Vtable[3 * i] = (unsigned char) XCLAMP (COMPRESS (255.0f / 408.0f * mat[2] * (float) i));   // R
    Vtable[3 * i + 1] = (unsigned char) XCLAMP (COMPRESS (255.0f / 210.0f * mat[5] * (float) i));       // G
    Vtable[3 * i + 2] = (unsigned char) (128.0f - 14.0f);       // G constant
  }
#undef fCOMPRESS
#undef COMPRESS
#undef XCLAMP
}


// Sets the constants. Call once prior to rendering.
void
SetConsts (float *bias, float *Uscale, float *Vscale)
{
  glEnable (GL_REGISTER_COMBINERS_NV);
  glColor3fv (bias);
  //printf("%f %f %f\n",bias[0],bias[1],bias[2]);
  glCombinerParameterfvNV (GL_CONSTANT_COLOR0_NV, Uscale);
  glCombinerParameterfvNV (GL_CONSTANT_COLOR1_NV, Vscale);
}

/*
 * SOFTWARE PATH
 */

inline void
map_EXPAND_NORMAL (float *v)
{
  v[0] = 2.0f * v[0] - 1.0f;
  v[1] = 2.0f * v[1] - 1.0f;
  v[2] = 2.0f * v[2] - 1.0f;
}

inline void
map_UNSIGNED_INVERT (float *v)
{
  v[0] = 1.0f - v[0];
  v[1] = 1.0f - v[1];
  v[2] = 1.0f - v[2];
}

inline void
map_UNSIGNED_IDENTITY (float *v)
{
  v[0] = (v[0] < 0.0f ? 0.0f : v[0]);
  v[1] = (v[1] < 0.0f ? 0.0f : v[1]);
  v[2] = (v[2] < 0.0f ? 0.0f : v[2]);
}

inline void
map_SIGNED_IDENTITY (float *v)
{
}

inline void
omap_SCALE_BY_TWO (float *v)
{
  v[0] *= 2.0f;
  v[1] *= 2.0f;
  v[2] *= 2.0f;
}

inline void
omap_SCALE_BY_ONE_HALF (float *v)
{
  v[0] *= 0.5f;
  v[1] *= 0.5f;
  v[2] *= 0.5f;
}

inline void
omap_RANGE (float *v)
{
  v[0] = (v[0] < -1.0f ? -1.0f : (v[0] > 1.0f ? 1.0f : v[0]));
  v[1] = (v[1] < -1.0f ? -1.0f : (v[1] > 1.0f ? 1.0f : v[1]));
  v[2] = (v[2] < -1.0f ? -1.0f : (v[2] > 1.0f ? 1.0f : v[2]));
}


inline void
omap_CLAMP_01 (float *v)
{
  v[0] = (v[0] < 0.0f ? 0.0f : (v[0] > 1.0f ? 1.0f : v[0]));
  v[1] = (v[1] < 0.0f ? 0.0f : (v[1] > 1.0f ? 1.0f : v[1]));
  v[2] = (v[2] < 0.0f ? 0.0f : (v[2] > 1.0f ? 1.0f : v[2]));
}

void
PerformSWCombiner (unsigned char *Result,
    unsigned char *tex0,
    unsigned char *tex1,
    unsigned char *tex2, float *COLOR0, float *CONST0, float *CONST1)
{
  float SPARE0[3];
  float SPARE1[3];
  float A[3], B[3], C[3], D[3];
  float TEX0[3], TEX1[3], TEX2[3];
  float ZERO[3] = { 0.0f, 0.0f, 0.0f };

  TEX0[0] = (float) tex0[0] / 255.0f;
  TEX0[1] = (float) tex0[1] / 255.0f;
  TEX0[2] = (float) tex0[2] / 255.0f;

  TEX1[0] = (float) tex1[0] / 255.0f;
  TEX1[1] = (float) tex1[1] / 255.0f;
  TEX1[2] = (float) tex1[2] / 255.0f;

  TEX2[0] = (float) tex2[0] / 255.0f;
  TEX2[1] = (float) tex2[1] / 255.0f;
  TEX2[2] = (float) tex2[2] / 255.0f;

  // Combiner Stage 0:
  memcpy (A, TEX0, 3 * sizeof (float));
  map_UNSIGNED_IDENTITY (A);
  memcpy (B, ZERO, 3 * sizeof (float));
  map_UNSIGNED_INVERT (B);
  memcpy (C, COLOR0, 3 * sizeof (float));
  map_EXPAND_NORMAL (C);
  memcpy (D, ZERO, 3 * sizeof (float));
  map_UNSIGNED_INVERT (D);
  SPARE0[0] = A[0] * B[0] + C[0] * D[0];
  SPARE0[1] = A[1] * B[1] + C[1] * D[1];
  SPARE0[2] = A[2] * B[2] + C[2] * D[2];
  omap_SCALE_BY_ONE_HALF (SPARE0);
  omap_RANGE (SPARE0);

  // Combiner Stage 1:
  memcpy (A, TEX1, 3 * sizeof (float));
  map_EXPAND_NORMAL (A);
  memcpy (B, CONST0, 3 * sizeof (float));
  map_UNSIGNED_IDENTITY (B);
  memcpy (C, TEX2, 3 * sizeof (float));
  map_EXPAND_NORMAL (C);
  memcpy (D, CONST1, 3 * sizeof (float));
  map_UNSIGNED_IDENTITY (D);
  SPARE1[0] = A[0] * B[0] + C[0] * D[0];
  SPARE1[1] = A[1] * B[1] + C[1] * D[1];
  SPARE1[2] = A[2] * B[2] + C[2] * D[2];
  omap_RANGE (SPARE1);

  // Combiner Stage 2:
  memcpy (A, SPARE0, 3 * sizeof (float));
  map_SIGNED_IDENTITY (A);
  memcpy (B, ZERO, 3 * sizeof (float));
  map_UNSIGNED_INVERT (B);
  memcpy (C, SPARE1, 3 * sizeof (float));
  map_SIGNED_IDENTITY (C);
  memcpy (D, ZERO, 3 * sizeof (float));
  map_UNSIGNED_INVERT (D);
  SPARE0[0] = A[0] * B[0] + C[0] * D[0];
  SPARE0[1] = A[1] * B[1] + C[1] * D[1];
  SPARE0[2] = A[2] * B[2] + C[2] * D[2];
  omap_SCALE_BY_TWO (SPARE0);
  omap_RANGE (SPARE0);

  // Final Combiner Stage:
  memcpy (A, ZERO, 3 * sizeof (float));
  map_UNSIGNED_INVERT (A);
  memcpy (B, SPARE0, 3 * sizeof (float));
  map_UNSIGNED_IDENTITY (B);
  memcpy (C, ZERO, 3 * sizeof (float));
  map_UNSIGNED_IDENTITY (C);
  memcpy (D, ZERO, 3 * sizeof (float));
  map_UNSIGNED_IDENTITY (D);
  SPARE0[0] = A[0] * B[0] + (1.0f - A[0]) * C[0] + D[0];
  SPARE0[1] = A[1] * B[1] + (1.0f - A[1]) * C[1] + D[1];
  SPARE0[2] = A[2] * B[2] + (1.0f - A[2]) * C[2] + D[2];
  omap_CLAMP_01 (SPARE0);
  Result[0] = (unsigned char) (SPARE0[0] * 255.0f);
  Result[1] = (unsigned char) (SPARE0[1] * 255.0f);
  Result[2] = (unsigned char) (SPARE0[2] * 255.0f);
}

// Sets up the register combiners. Call once prior to rendering
void
SetupCombiners (void)
{
  glCombinerParameteriNV (GL_NUM_GENERAL_COMBINERS_NV, 3);
  // Combiner Stage 0: th. OK
  glCombinerInputNV (GL_COMBINER0_NV, GL_RGB, GL_VARIABLE_A_NV, GL_TEXTURE0_ARB,
      GL_UNSIGNED_IDENTITY_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER0_NV, GL_RGB, GL_VARIABLE_B_NV, GL_ZERO,
      GL_UNSIGNED_INVERT_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER0_NV, GL_RGB, GL_VARIABLE_C_NV,
      GL_PRIMARY_COLOR_NV, GL_EXPAND_NORMAL_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER0_NV, GL_RGB, GL_VARIABLE_D_NV, GL_ZERO,
      GL_UNSIGNED_INVERT_NV, GL_RGB);
  glCombinerOutputNV (GL_COMBINER0_NV, GL_RGB, GL_DISCARD_NV, GL_DISCARD_NV,
      GL_SPARE0_NV, GL_SCALE_BY_ONE_HALF_NV, GL_NONE, GL_FALSE, GL_FALSE,
      GL_FALSE);
  // Combiner Stage 1: th. OK
  glCombinerInputNV (GL_COMBINER1_NV, GL_RGB, GL_VARIABLE_A_NV, GL_TEXTURE1_ARB,
      GL_EXPAND_NORMAL_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER1_NV, GL_RGB, GL_VARIABLE_B_NV,
      GL_CONSTANT_COLOR0_NV, GL_UNSIGNED_IDENTITY_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER1_NV, GL_RGB, GL_VARIABLE_C_NV, GL_TEXTURE2_ARB,
      GL_EXPAND_NORMAL_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER1_NV, GL_RGB, GL_VARIABLE_D_NV,
      GL_CONSTANT_COLOR1_NV, GL_UNSIGNED_IDENTITY_NV, GL_RGB);
  glCombinerOutputNV (GL_COMBINER1_NV, GL_RGB, GL_DISCARD_NV, GL_DISCARD_NV,
      GL_SPARE1_NV, GL_NONE, GL_NONE, GL_FALSE, GL_FALSE, GL_FALSE);
  // Combiner Stage 2: th. OK
  glCombinerInputNV (GL_COMBINER2_NV, GL_RGB, GL_VARIABLE_A_NV, GL_SPARE0_NV,
      GL_SIGNED_IDENTITY_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER2_NV, GL_RGB, GL_VARIABLE_B_NV, GL_ZERO,
      GL_UNSIGNED_INVERT_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER2_NV, GL_RGB, GL_VARIABLE_C_NV, GL_SPARE1_NV,
      GL_SIGNED_IDENTITY_NV, GL_RGB);
  glCombinerInputNV (GL_COMBINER2_NV, GL_RGB, GL_VARIABLE_D_NV, GL_ZERO,
      GL_UNSIGNED_INVERT_NV, GL_RGB);
  glCombinerOutputNV (GL_COMBINER2_NV, GL_RGB, GL_DISCARD_NV, GL_DISCARD_NV,
      GL_SPARE0_NV, GL_SCALE_BY_TWO_NV, GL_NONE, GL_FALSE, GL_FALSE, GL_FALSE);
  // Final Sage: th. OK
  glFinalCombinerInputNV (GL_VARIABLE_A_NV, GL_ZERO, GL_UNSIGNED_INVERT_NV,
      GL_RGB);
  glFinalCombinerInputNV (GL_VARIABLE_B_NV, GL_SPARE0_NV,
      GL_UNSIGNED_IDENTITY_NV, GL_RGB);
  glFinalCombinerInputNV (GL_VARIABLE_C_NV, GL_ZERO, GL_UNSIGNED_IDENTITY_NV,
      GL_RGB);
  glFinalCombinerInputNV (GL_VARIABLE_D_NV, GL_ZERO, GL_UNSIGNED_IDENTITY_NV,
      GL_RGB);
  glFinalCombinerInputNV (GL_VARIABLE_G_NV, GL_ZERO, GL_UNSIGNED_INVERT_NV,
      GL_ALPHA);
}


unsigned int
PowerOfTwo (unsigned int i)
{
  unsigned int bitsum = 0;
  unsigned int shifts = 0;
  unsigned int j = (unsigned int) i;

  // Check wether i is a power of two - may contain at most one set bit
  do {
    bitsum += j & 1;
    j = j >> 1;
    ++shifts;
  } while (j > 0);
  if (bitsum == 1)
    return i;
  else
    return (1 << shifts);
}


// Initializes textures. Call once prior to rendering
void
InitYUVPlanes (GLuint * Yhandle, GLuint * Uhandle, GLuint * Vhandle, unsigned int Ywidth, unsigned int Yheight, unsigned int UVwidth, unsigned int UVheight, GLenum filter,     // filter should be either GL_NEAREST or GL_LINEAR. Test this! 
    unsigned char *Ypal, unsigned char *Upal, unsigned char *Vpal)
{
  glGenTextures (1, Yhandle);
  glGenTextures (1, Uhandle);
  glGenTextures (1, Vhandle);
  glBindTexture (GL_TEXTURE_2D, (*Yhandle));
#ifdef _WIN32
  glColorTableEXT (GL_TEXTURE_2D, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, Ypal);
#else // Hopefully Linux
  glColorTable (GL_TEXTURE_2D, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, Ypal);
#endif
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  tex_xsize = PowerOfTwo (Ywidth);
  tex_ysize = PowerOfTwo (Yheight);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, PowerOfTwo (Ywidth),
      PowerOfTwo (Yheight), 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, NULL);

  glBindTexture (GL_TEXTURE_2D, (*Uhandle));
#ifdef _WIN32
  glColorTableEXT (GL_TEXTURE_2D, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, Upal);
#else // Hopefully Linux
  glColorTable (GL_TEXTURE_2D, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, Upal);
#endif
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, PowerOfTwo (UVwidth),
      PowerOfTwo (UVheight), 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, NULL);

  glBindTexture (GL_TEXTURE_2D, (*Vhandle));
#ifdef _WIN32
  glColorTableEXT (GL_TEXTURE_2D, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, Vpal);
#else // Hopefully Linux
  glColorTable (GL_TEXTURE_2D, GL_RGB8, 256, GL_RGB, GL_UNSIGNED_BYTE, Vpal);
#endif
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, PowerOfTwo (UVwidth),
      PowerOfTwo (UVheight), 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, NULL);
}

void
LoadYUVPlanes (GLuint Yhandle, GLuint Uhandle, GLuint Vhandle,
    unsigned int Ywidth, unsigned int Yheight,
    unsigned int UVwidth, unsigned int UVheight,
    unsigned char *Ydata, unsigned char *Udata, unsigned char *Vdata)
{
  glActiveTextureARB (GL_TEXTURE0_ARB);
  glBindTexture (GL_TEXTURE_2D, Yhandle);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, Ywidth, Yheight, GL_COLOR_INDEX,
      GL_UNSIGNED_BYTE, Ydata);
  glEnable (GL_TEXTURE_2D);

  glActiveTextureARB (GL_TEXTURE1_ARB);
  glBindTexture (GL_TEXTURE_2D, Uhandle);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, UVwidth, UVheight, GL_COLOR_INDEX,
      GL_UNSIGNED_BYTE, Udata);
  glEnable (GL_TEXTURE_2D);

  glActiveTextureARB (GL_TEXTURE2_ARB);
  glBindTexture (GL_TEXTURE_2D, Vhandle);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, UVwidth, UVheight, GL_COLOR_INDEX,
      GL_UNSIGNED_BYTE, Vdata);
  glEnable (GL_TEXTURE_2D);
}


void
Initialize_Backend (unsigned int Ywidth, unsigned int Yheight,
    unsigned int UVwidth, unsigned int UVheight, GLenum filter)
{
  printf ("Reinitializing register combiner backend with res %d x %d!\n",
      Ywidth, Yheight);
  //if (!GL_ARB_multitexture_Init()) exit(0);
  //if (!GL_EXT_paletted_texture_Init()) exit(0);
  //if (!GL_NV_register_combiners_Init()) exit(0);
  unsigned char Ypal[768];
  unsigned char Upal[768];
  unsigned char Vpal[768];
  float bias[4];
  float Uscale[4];
  float Vscale[4];

  GenerateRGBTables (Ypal, Upal, Vpal, bias, Uscale, Vscale);
  InitYUVPlanes (&Yhandle, &Uhandle, &Vhandle, Ywidth, Yheight, UVwidth,
      UVheight, filter, Ypal, Upal, Vpal);
  SetupCombiners ();
  SetConsts (bias, Uscale, Vscale);
}


void
initialize (GLenum filter)
{
  glShadeModel (GL_SMOOTH);
  glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glClearColor (0.0f, 0.0f, 0.2f, 1.0f);
  Initialize_Backend (Ywidth, Yheight, UVwidth, UVheight, filter);
}
