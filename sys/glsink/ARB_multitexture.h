/*************************************************************
 *                                                           *
 * file  : ARB_multitexture.h                                *
 * author: Jens Schneider                                    *
 * date  : 01.Mar.2001-10.Jul.2001                           *
 * e-mail: jens@glHint.de                                    *
 *                                                           *
 * version 1.0ß                                              *
 *                                                           *
 *************************************************************/  
    
#ifndef __ARB_MULTITEXTURE_H_
#define __ARB_MULTITEXTURE_H_
    
/*
 *  GLOBAL SWITCHES - enable/disable advanced features of this header
 *
 */ 
#define ARB_MULTITEXTURE_INITIALIZE 1	// enable generic init-routines
#ifndef _WIN32
#define GL_GLEXT_PROTOTYPES 1
#endif /*  */
    
#ifdef __cplusplus
extern "C"
{
  
#endif				/*  */
  
#if defined(_WIN32) && !defined(APIENTRY) && !defined(__CYGWIN__)
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif				/*  */
  
#ifndef APIENTRY
#define APIENTRY
#endif				/*  */
  
// Header file version number, required by OpenGL ABI for Linux
//#define GL_GLEXT_VERSION 7
  
/* 
 * NEW TOKENS TO OPENGL 1.2.1 
 *
 */ 
#ifndef GL_ARB_multitexture
#define GL_TEXTURE0_ARB                   0x84C0
#define GL_TEXTURE1_ARB                   0x84C1
#define GL_TEXTURE2_ARB                   0x84C2
#define GL_TEXTURE3_ARB                   0x84C3
#define GL_TEXTURE4_ARB                   0x84C4
#define GL_TEXTURE5_ARB                   0x84C5
#define GL_TEXTURE6_ARB                   0x84C6
#define GL_TEXTURE7_ARB                   0x84C7
#define GL_TEXTURE8_ARB                   0x84C8
#define GL_TEXTURE9_ARB                   0x84C9
#define GL_TEXTURE10_ARB                  0x84CA
#define GL_TEXTURE11_ARB                  0x84CB
#define GL_TEXTURE12_ARB                  0x84CC
#define GL_TEXTURE13_ARB                  0x84CD
#define GL_TEXTURE14_ARB                  0x84CE
#define GL_TEXTURE15_ARB                  0x84CF
#define GL_TEXTURE16_ARB                  0x84D0
#define GL_TEXTURE17_ARB                  0x84D1
#define GL_TEXTURE18_ARB                  0x84D2
#define GL_TEXTURE19_ARB                  0x84D3
#define GL_TEXTURE20_ARB                  0x84D4
#define GL_TEXTURE21_ARB                  0x84D5
#define GL_TEXTURE22_ARB                  0x84D6
#define GL_TEXTURE23_ARB                  0x84D7
#define GL_TEXTURE24_ARB                  0x84D8
#define GL_TEXTURE25_ARB                  0x84D9
#define GL_TEXTURE26_ARB                  0x84DA
#define GL_TEXTURE27_ARB                  0x84DB
#define GL_TEXTURE28_ARB                  0x84DC
#define GL_TEXTURE29_ARB                  0x84DD
#define GL_TEXTURE30_ARB                  0x84DE
#define GL_TEXTURE31_ARB                  0x84DF
#define GL_ACTIVE_TEXTURE_ARB             0x84E0
#define GL_CLIENT_ACTIVE_TEXTURE_ARB      0x84E1
#define GL_MAX_TEXTURE_UNITS_ARB          0x84E2
#define GL_ARB_multitexture 1
#endif				/*  */
  
#ifndef _WIN32
#ifdef GL_GLEXT_PROTOTYPES
  extern void APIENTRY glActiveTextureARB (GLenum);
   extern void APIENTRY glClientActiveTextureARB (GLenum);
   extern void APIENTRY glMultiTexCoord1dARB (GLenum, GLdouble);
   extern void APIENTRY glMultiTexCoord1dvARB (GLenum, const GLdouble *);
   extern void APIENTRY glMultiTexCoord1fARB (GLenum, GLfloat);
   extern void APIENTRY glMultiTexCoord1fvARB (GLenum, const GLfloat *);
   extern void APIENTRY glMultiTexCoord1iARB (GLenum, GLint);
   extern void APIENTRY glMultiTexCoord1ivARB (GLenum, const GLint *);
   extern void APIENTRY glMultiTexCoord1sARB (GLenum, GLshort);
   extern void APIENTRY glMultiTexCoord1svARB (GLenum, const GLshort *);
   extern void APIENTRY glMultiTexCoord2dARB (GLenum, GLdouble, GLdouble);
   extern void APIENTRY glMultiTexCoord2dvARB (GLenum, const GLdouble *);
   extern void APIENTRY glMultiTexCoord2fARB (GLenum, GLfloat, GLfloat);
   extern void APIENTRY glMultiTexCoord2fvARB (GLenum, const GLfloat *);
   extern void APIENTRY glMultiTexCoord2iARB (GLenum, GLint, GLint);
   extern void APIENTRY glMultiTexCoord2ivARB (GLenum, const GLint *);
   extern void APIENTRY glMultiTexCoord2sARB (GLenum, GLshort, GLshort);
   extern void APIENTRY glMultiTexCoord2svARB (GLenum, const GLshort *);
   extern void APIENTRY glMultiTexCoord3dARB (GLenum, GLdouble, GLdouble,
      GLdouble);
   extern void APIENTRY glMultiTexCoord3dvARB (GLenum, const GLdouble *);
   extern void APIENTRY glMultiTexCoord3fARB (GLenum, GLfloat, GLfloat,
      GLfloat);
   extern void APIENTRY glMultiTexCoord3fvARB (GLenum, const GLfloat *);
   extern void APIENTRY glMultiTexCoord3iARB (GLenum, GLint, GLint, GLint);
   extern void APIENTRY glMultiTexCoord3ivARB (GLenum, const GLint *);
   extern void APIENTRY glMultiTexCoord3sARB (GLenum, GLshort, GLshort,
      GLshort);
   extern void APIENTRY glMultiTexCoord3svARB (GLenum, const GLshort *);
   extern void APIENTRY glMultiTexCoord4dARB (GLenum, GLdouble, GLdouble,
      GLdouble, GLdouble);
   extern void APIENTRY glMultiTexCoord4dvARB (GLenum, const GLdouble *);
   extern void APIENTRY glMultiTexCoord4fARB (GLenum, GLfloat, GLfloat,
      GLfloat, GLfloat);
   extern void APIENTRY glMultiTexCoord4fvARB (GLenum, const GLfloat *);
   extern void APIENTRY glMultiTexCoord4iARB (GLenum, GLint, GLint, GLint,
      GLint);
   extern void APIENTRY glMultiTexCoord4ivARB (GLenum, const GLint *);
   extern void APIENTRY glMultiTexCoord4sARB (GLenum, GLshort, GLshort,
      GLshort, GLshort);
   extern void APIENTRY glMultiTexCoord4svARB (GLenum, const GLshort *);
   
#endif				// GL_GLEXT_PROTOTYPES
#else				// not _WIN32
  typedef void (APIENTRY * PFNGLACTIVETEXTUREARBPROC) (GLenum texture);
   typedef void (APIENTRY * PFNGLCLIENTACTIVETEXTUREARBPROC) (GLenum texture);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1DARBPROC) (GLenum target,
      GLdouble s);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1DVARBPROC) (GLenum target,
      const GLdouble * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1FARBPROC) (GLenum target,
      GLfloat s);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1FVARBPROC) (GLenum target,
      const GLfloat * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1IARBPROC) (GLenum target,
      GLint s);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1IVARBPROC) (GLenum target,
      const GLint * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1SARBPROC) (GLenum target,
      GLshort s);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD1SVARBPROC) (GLenum target,
      const GLshort * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2DARBPROC) (GLenum target,
      GLdouble s, GLdouble t);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2DVARBPROC) (GLenum target,
      const GLdouble * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2FARBPROC) (GLenum target,
      GLfloat s, GLfloat t);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2FVARBPROC) (GLenum target,
      const GLfloat * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2IARBPROC) (GLenum target,
      GLint s, GLint t);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2IVARBPROC) (GLenum target,
      const GLint * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2SARBPROC) (GLenum target,
      GLshort s, GLshort t);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD2SVARBPROC) (GLenum target,
      const GLshort * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3DARBPROC) (GLenum target,
      GLdouble s, GLdouble t, GLdouble r);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3DVARBPROC) (GLenum target,
      const GLdouble * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3FARBPROC) (GLenum target,
      GLfloat s, GLfloat t, GLfloat r);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3FVARBPROC) (GLenum target,
      const GLfloat * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3IARBPROC) (GLenum target,
      GLint s, GLint t, GLint r);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3IVARBPROC) (GLenum target,
      const GLint * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3SARBPROC) (GLenum target,
      GLshort s, GLshort t, GLshort r);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD3SVARBPROC) (GLenum target,
      const GLshort * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4DARBPROC) (GLenum target,
      GLdouble s, GLdouble t, GLdouble r, GLdouble q);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4DVARBPROC) (GLenum target,
      const GLdouble * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4FARBPROC) (GLenum target,
      GLfloat s, GLfloat t, GLfloat r, GLfloat q);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4FVARBPROC) (GLenum target,
      const GLfloat * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4IARBPROC) (GLenum target,
      GLint s, GLint t, GLint r, GLint q);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4IVARBPROC) (GLenum target,
      const GLint * v);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4SARBPROC) (GLenum target,
      GLshort s, GLshort t, GLshort r, GLshort q);
   typedef void (APIENTRY * PFNGLMULTITEXCOORD4SVARBPROC) (GLenum target,
      const GLshort * v);
   
#endif				// _WIN32
   
#ifdef ARB_MULTITEXTURE_INITIALIZE
#include<string.h>		// string manipulation for runtime-check
   
#ifdef _WIN32
    PFNGLACTIVETEXTUREARBPROC glActiveTextureARB = NULL;
   PFNGLCLIENTACTIVETEXTUREARBPROC glClientActiveTextureARB = NULL;
   PFNGLMULTITEXCOORD1DARBPROC glMultiTexCoord1dARB = NULL;
   PFNGLMULTITEXCOORD1DVARBPROC glMultiTexCoord1dvARB = NULL;
   PFNGLMULTITEXCOORD1FARBPROC glMultiTexCoord1fARB = NULL;
   PFNGLMULTITEXCOORD1FVARBPROC glMultiTexCoord1fvARB = NULL;
   PFNGLMULTITEXCOORD1IARBPROC glMultiTexCoord1iARB = NULL;
   PFNGLMULTITEXCOORD1IVARBPROC glMultiTexCoord1ivARB = NULL;
   PFNGLMULTITEXCOORD1SARBPROC glMultiTexCoord1sARB = NULL;
   PFNGLMULTITEXCOORD1SVARBPROC glMultiTexCoord1svARB = NULL;
   PFNGLMULTITEXCOORD2DARBPROC glMultiTexCoord2dARB = NULL;
   PFNGLMULTITEXCOORD2DVARBPROC glMultiTexCoord2dvARB = NULL;
   PFNGLMULTITEXCOORD2FARBPROC glMultiTexCoord2fARB = NULL;
   PFNGLMULTITEXCOORD2FVARBPROC glMultiTexCoord2fvARB = NULL;
   PFNGLMULTITEXCOORD2IARBPROC glMultiTexCoord2iARB = NULL;
   PFNGLMULTITEXCOORD2IVARBPROC glMultiTexCoord2ivARB = NULL;
   PFNGLMULTITEXCOORD2SARBPROC glMultiTexCoord2sARB = NULL;
   PFNGLMULTITEXCOORD2SVARBPROC glMultiTexCoord2svARB = NULL;
   PFNGLMULTITEXCOORD3DARBPROC glMultiTexCoord3dARB = NULL;
   PFNGLMULTITEXCOORD3DVARBPROC glMultiTexCoord3dvARB = NULL;
   PFNGLMULTITEXCOORD3FARBPROC glMultiTexCoord3fARB = NULL;
   PFNGLMULTITEXCOORD3FVARBPROC glMultiTexCoord3fvARB = NULL;
   PFNGLMULTITEXCOORD3IARBPROC glMultiTexCoord3iARB = NULL;
   PFNGLMULTITEXCOORD3IVARBPROC glMultiTexCoord3ivARB = NULL;
   PFNGLMULTITEXCOORD3SARBPROC glMultiTexCoord3sARB = NULL;
   PFNGLMULTITEXCOORD3SVARBPROC glMultiTexCoord3svARB = NULL;
   PFNGLMULTITEXCOORD4DARBPROC glMultiTexCoord4dARB = NULL;
   PFNGLMULTITEXCOORD4DVARBPROC glMultiTexCoord4dvARB = NULL;
   PFNGLMULTITEXCOORD4FARBPROC glMultiTexCoord4fARB = NULL;
   PFNGLMULTITEXCOORD4FVARBPROC glMultiTexCoord4fvARB = NULL;
   PFNGLMULTITEXCOORD4IARBPROC glMultiTexCoord4iARB = NULL;
   PFNGLMULTITEXCOORD4IVARBPROC glMultiTexCoord4ivARB = NULL;
   PFNGLMULTITEXCOORD4SARBPROC glMultiTexCoord4sARB = NULL;
   PFNGLMULTITEXCOORD4SVARBPROC glMultiTexCoord4svARB = NULL;
   
#endif				// _WIN32 
   int CheckForARBMultitextureSupport (void)
  {
    const char search[] = "GL_ARB_multitexture";
     int i, pos = 0;
     int maxpos = strlen (search) - 1;
     char extensions[10000];
     printf ("Getting GLstring, context is %p\n", glXGetCurrentContext ());
     strcpy (extensions, (const char *) glGetString (GL_EXTENSIONS));
     printf ("Examinig GLstring\n");
     int len = strlen (extensions);
     for (i = 0; i < len; i++)
    {
      if ((i == 0) || ((i > 1) && extensions[i - 1] == ' ')) {
	pos = 0;
	while (extensions[i] != ' ')
	{
	  if (extensions[i] == search[pos])
	    pos++;
	  if ((pos > maxpos) && extensions[i + 1] == ' ') {
	    
		//if (debug)
	    {
	      
		  //fprintf(stderr, search);
		  //fprintf(stderr, " supported.\n");
	    }
	     return 1;
	  }
	  ++i;
	}
      }
    }
    
	//printf(search);
	//printf(" not supported.\n");
	return 0;
  }
  int GL_ARB_multitexture_Init (void)
  {
    if (!CheckForARBMultitextureSupport ())
      return 0;
    
#ifdef _WIN32
	glActiveTextureARB =
	(PFNGLACTIVETEXTUREARBPROC) wglGetProcAddress ("glActiveTextureARB");
    if (glActiveTextureARB == NULL) {
      fprintf (stderr, "glActiveTextureARB not found.\n");
      return 0;
    }
    glClientActiveTextureARB = (PFNGLCLIENTACTIVETEXTUREARBPROC)
	wglGetProcAddress ("glClientActiveTextureARB");
    if (glClientActiveTextureARB == NULL) {
      fprintf (stderr, "glClientActiveTextureARB not found.\n");
      return 0;
    }
    glMultiTexCoord1dARB = (PFNGLMULTITEXCOORD1DARBPROC)
	wglGetProcAddress ("glMultiTexCoord1dARB");
    if (glMultiTexCoord1dARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1dARB not found.\n");
      return 0;
    }
    glMultiTexCoord1dvARB = (PFNGLMULTITEXCOORD1DVARBPROC)
	wglGetProcAddress ("glMultiTexCoord1dvARB");
    if (glMultiTexCoord1dvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1dAvRB not found.\n");
      return 0;
    }
    glMultiTexCoord1fARB = (PFNGLMULTITEXCOORD1FARBPROC)
	wglGetProcAddress ("glMultiTexCoord1fARB");
    if (glMultiTexCoord1fARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1fARB not found.\n");
      return 0;
    }
    glMultiTexCoord1fvARB = (PFNGLMULTITEXCOORD1FVARBPROC)
	wglGetProcAddress ("glMultiTexCoord1fvARB");
    if (glMultiTexCoord1fvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1fvARB not found.\n");
      return 0;
    }
    glMultiTexCoord1iARB = (PFNGLMULTITEXCOORD1IARBPROC)
	wglGetProcAddress ("glMultiTexCoord1iARB");
    if (glMultiTexCoord1iARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1iARB not found.\n");
      return 0;
    }
    glMultiTexCoord1ivARB = (PFNGLMULTITEXCOORD1IVARBPROC)
	wglGetProcAddress ("glMultiTexCoord1ivARB");
    if (glMultiTexCoord1ivARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1ivARB not found.\n");
      return 0;
    }
    glMultiTexCoord1sARB = (PFNGLMULTITEXCOORD1SARBPROC)
	wglGetProcAddress ("glMultiTexCoord1sARB");
    if (glMultiTexCoord1sARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1sARB not found.\n");
      return 0;
    }
    glMultiTexCoord1svARB = (PFNGLMULTITEXCOORD1SVARBPROC)
	wglGetProcAddress ("glMultiTexCoord1svARB");
    if (glMultiTexCoord1svARB == NULL) {
      fprintf (stderr, "glMultiTexCoord1svARB not found.\n");
      return 0;
    }
    glMultiTexCoord2dARB = (PFNGLMULTITEXCOORD2DARBPROC)
	wglGetProcAddress ("glMultiTexCoord2dARB");
    if (glMultiTexCoord2dARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2dARB not found.\n");
      return 0;
    }
    glMultiTexCoord2dvARB = (PFNGLMULTITEXCOORD2DVARBPROC)
	wglGetProcAddress ("glMultiTexCoord2dvARB");
    if (glMultiTexCoord2dvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2dAvRB not found.\n");
      return 0;
    }
    glMultiTexCoord2fARB = (PFNGLMULTITEXCOORD2FARBPROC)
	wglGetProcAddress ("glMultiTexCoord2fARB");
    if (glMultiTexCoord2fARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2fARB not found.\n");
      return 0;
    }
    glMultiTexCoord2fvARB = (PFNGLMULTITEXCOORD2FVARBPROC)
	wglGetProcAddress ("glMultiTexCoord2fvARB");
    if (glMultiTexCoord2fvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2fvARB not found.\n");
      return 0;
    }
    glMultiTexCoord2iARB = (PFNGLMULTITEXCOORD2IARBPROC)
	wglGetProcAddress ("glMultiTexCoord2iARB");
    if (glMultiTexCoord2iARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2iARB not found.\n");
      return 0;
    }
    glMultiTexCoord2ivARB = (PFNGLMULTITEXCOORD2IVARBPROC)
	wglGetProcAddress ("glMultiTexCoord2ivARB");
    if (glMultiTexCoord2ivARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2ivARB not found.\n");
      return 0;
    }
    glMultiTexCoord2sARB = (PFNGLMULTITEXCOORD2SARBPROC)
	wglGetProcAddress ("glMultiTexCoord2sARB");
    if (glMultiTexCoord2sARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2sARB not found.\n");
      return 0;
    }
    glMultiTexCoord2svARB = (PFNGLMULTITEXCOORD2SVARBPROC)
	wglGetProcAddress ("glMultiTexCoord2svARB");
    if (glMultiTexCoord2svARB == NULL) {
      fprintf (stderr, "glMultiTexCoord2svARB not found.\n");
      return 0;
    }
    glMultiTexCoord3dARB = (PFNGLMULTITEXCOORD3DARBPROC)
	wglGetProcAddress ("glMultiTexCoord3dARB");
    if (glMultiTexCoord3dARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3dARB not found.\n");
      return 0;
    }
    glMultiTexCoord3dvARB = (PFNGLMULTITEXCOORD3DVARBPROC)
	wglGetProcAddress ("glMultiTexCoord3dvARB");
    if (glMultiTexCoord3dvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3dAvRB not found.\n");
      return 0;
    }
    glMultiTexCoord3fARB = (PFNGLMULTITEXCOORD3FARBPROC)
	wglGetProcAddress ("glMultiTexCoord3fARB");
    if (glMultiTexCoord3fARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3fARB not found.\n");
      return 0;
    }
    glMultiTexCoord3fvARB = (PFNGLMULTITEXCOORD3FVARBPROC)
	wglGetProcAddress ("glMultiTexCoord3fvARB");
    if (glMultiTexCoord3fvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3fvARB not found.\n");
      return 0;
    }
    glMultiTexCoord3iARB = (PFNGLMULTITEXCOORD3IARBPROC)
	wglGetProcAddress ("glMultiTexCoord3iARB");
    if (glMultiTexCoord3iARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3iARB not found.\n");
      return 0;
    }
    glMultiTexCoord3ivARB = (PFNGLMULTITEXCOORD3IVARBPROC)
	wglGetProcAddress ("glMultiTexCoord3ivARB");
    if (glMultiTexCoord3ivARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3ivARB not found.\n");
      return 0;
    }
    glMultiTexCoord3sARB = (PFNGLMULTITEXCOORD3SARBPROC)
	wglGetProcAddress ("glMultiTexCoord3sARB");
    if (glMultiTexCoord3sARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3sARB not found.\n");
      return 0;
    }
    glMultiTexCoord3svARB = (PFNGLMULTITEXCOORD3SVARBPROC)
	wglGetProcAddress ("glMultiTexCoord3svARB");
    if (glMultiTexCoord3svARB == NULL) {
      fprintf (stderr, "glMultiTexCoord3svARB not found.\n");
      return 0;
    }
    glMultiTexCoord4dARB = (PFNGLMULTITEXCOORD4DARBPROC)
	wglGetProcAddress ("glMultiTexCoord4dARB");
    if (glMultiTexCoord4dARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4dARB not found.\n");
      return 0;
    }
    glMultiTexCoord4dvARB = (PFNGLMULTITEXCOORD4DVARBPROC)
	wglGetProcAddress ("glMultiTexCoord4dvARB");
    if (glMultiTexCoord4dvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4dAvRB not found.\n");
      return 0;
    }
    glMultiTexCoord4fARB = (PFNGLMULTITEXCOORD4FARBPROC)
	wglGetProcAddress ("glMultiTexCoord4fARB");
    if (glMultiTexCoord4fARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4fARB not found.\n");
      return 0;
    }
    glMultiTexCoord4fvARB = (PFNGLMULTITEXCOORD4FVARBPROC)
	wglGetProcAddress ("glMultiTexCoord4fvARB");
    if (glMultiTexCoord4fvARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4fvARB not found.\n");
      return 0;
    }
    glMultiTexCoord4iARB = (PFNGLMULTITEXCOORD4IARBPROC)
	wglGetProcAddress ("glMultiTexCoord4iARB");
    if (glMultiTexCoord4iARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4iARB not found.\n");
      return 0;
    }
    glMultiTexCoord4ivARB = (PFNGLMULTITEXCOORD4IVARBPROC)
	wglGetProcAddress ("glMultiTexCoord4ivARB");
    if (glMultiTexCoord4ivARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4ivARB not found.\n");
      return 0;
    }
    glMultiTexCoord4sARB = (PFNGLMULTITEXCOORD4SARBPROC)
	wglGetProcAddress ("glMultiTexCoord4sARB");
    if (glMultiTexCoord4sARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4sARB not found.\n");
      return 0;
    }
    glMultiTexCoord4svARB = (PFNGLMULTITEXCOORD4SVARBPROC)
	wglGetProcAddress ("glMultiTexCoord4svARB");
    if (glMultiTexCoord4svARB == NULL) {
      fprintf (stderr, "glMultiTexCoord4svARB not found.\n");
      return 0;
    }
    
#endif // _WIN32
	return 1;
  }
  
#endif // ARB_MULTITEXTURE_INITIALIZE
      
#ifdef __cplusplus
}


#endif /*  */
    
#endif // not __ARB_MULTITEXTURE_H_
