/*************************************************************
 *                                                           *
 * file  : NV_register_combiners.h                           *
 * author: Jens Schneider                                    *
 * date  : 12.Mar.2001-04.Jul.2001                           *
 * e-mail: jens@glHint.de                                    *
 *                                                           *
 * version 2.0ß                                              *
 *                                                           *
 *************************************************************/

#ifndef __NV_register_combiners_H_
#define __NV_register_combiners_H_

/*
 *  GLOBAL SWITCHES - enable/disable advanced features of this header
 *
 */
#define NV_REGISTER_COMBINERS_INITIALIZE 1 // enable generic init-routines
#ifndef _WIN32
#define GL_GLEXT_PROTOTYPES 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(APIENTRY) && !defined(__CYGWIN__)
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#endif

#ifndef APIENTRY
#define APIENTRY
#endif


// Header file version number, required by OpenGL ABI for Linux
//#define GL_GLEXT_VERSION 7


/* 
 * NEW TOKENS TO OPENGL 1.2.1 
 *
 */
#ifndef GL_NV_register_combiners
#define GL_REGISTER_COMBINERS_NV          0x8522
#define GL_COMBINER0_NV                   0x8550
#define GL_COMBINER1_NV                   0x8551
#define GL_COMBINER2_NV                   0x8552
#define GL_COMBINER3_NV                   0x8553
#define GL_COMBINER4_NV                   0x8554
#define GL_COMBINER5_NV                   0x8555
#define GL_COMBINER6_NV                   0x8556
#define GL_COMBINER7_NV                   0x8557
#define GL_VARIABLE_A_NV                  0x8523
#define GL_VARIABLE_B_NV                  0x8524
#define GL_VARIABLE_C_NV                  0x8525
#define GL_VARIABLE_D_NV                  0x8526
#define GL_VARIABLE_E_NV                  0x8527
#define GL_VARIABLE_F_NV                  0x8528
#define GL_VARIABLE_G_NV                  0x8529
#define GL_CONSTANT_COLOR0_NV             0x852A 
#define GL_CONSTANT_COLOR1_NV             0x852B 
#define GL_PRIMARY_COLOR_NV               0x852C 
#define GL_SECONDARY_COLOR_NV             0x852D 
#define GL_SPARE0_NV                      0x852E 
#define GL_SPARE1_NV                      0x852F 
#define GL_UNSIGNED_IDENTITY_NV           0x8536 
#define GL_UNSIGNED_INVERT_NV             0x8537 
#define GL_EXPAND_NORMAL_NV               0x8538 
#define GL_EXPAND_NEGATE_NV               0x8539 
#define GL_HALF_BIAS_NORMAL_NV            0x853A 
#define GL_HALF_BIAS_NEGATE_NV            0x853B 
#define GL_SIGNED_IDENTITY_NV             0x853C
#define GL_SIGNED_NEGATE_NV               0x853D
#define GL_E_TIMES_F_NV                   0x8531 
#define GL_SPARE0_PLUS_SECONDARY_COLOR_NV 0x8532
#define GL_SCALE_BY_TWO_NV                0x853E 
#define GL_SCALE_BY_FOUR_NV               0x853F 
#define GL_SCALE_BY_ONE_HALF_NV           0x8540
#define GL_BIAS_BY_NEGATIVE_ONE_HALF_NV   0x8541
#define GL_DISCARD_NV                     0x8530
#define GL_COMBINER_INPUT_NV              0x8542 
#define GL_COMBINER_MAPPING_NV            0x8543 
#define GL_COMBINER_COMPONENT_USAGE_NV    0x8544
#define GL_COMBINER_AB_DOT_PRODUCT_NV     0x8545 
#define GL_COMBINER_CD_DOT_PRODUCT_NV     0x8546 
#define GL_COMBINER_MUX_SUM_NV            0x8547 
#define GL_COMBINER_SCALE_NV              0x8548 
#define GL_COMBINER_BIAS_NV               0x8549 
#define GL_COMBINER_AB_OUTPUT_NV          0x854A 
#define GL_COMBINER_CD_OUTPUT_NV          0x854B 
#define GL_COMBINER_SUM_OUTPUT_NV         0x854C
#define GL_NUM_GENERAL_COMBINERS_NV       0x854E 
#define GL_COLOR_SUM_CLAMP_NV             0x854F
#define GL_MAX_GENERAL_COMBINERS_NV       0x854D
#define GL_NV_register_combiners 1
#endif

#ifndef _WIN32
#ifdef GL_GLEXT_PROTOTYPES
extern void APIENTRY glCombinerParameterfvNV(GLenum, const GLfloat *);
extern void APIENTRY glCombinerParameterivNV(GLenum, const GLint *); 
extern void APIENTRY glCombinerParameterfNV (GLenum, GLfloat); 
extern void APIENTRY glCombinerParameteriNV (GLenum, GLint); 
extern void APIENTRY glCombinerInputNV      (GLenum, GLenum, GLenum, GLenum, GLenum, GLenum); 
extern void APIENTRY glCombinerOutputNV     (GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLenum, GLboolean, GLboolean, GLboolean); 
extern void APIENTRY glFinalCombinerInputNV (GLenum, GLenum, GLenum, GLenum); 
extern void APIENTRY glGetCombinerInputParameterfvNV (GLenum, GLenum, GLenum, GLenum, GLfloat *); 
extern void APIENTRY glGetCombinerInputParameterivNV (GLenum, GLenum, GLenum, GLenum, GLint *); 
extern void APIENTRY glGetCombinerOutputParameterfvNV(GLenum, GLenum, GLenum, GLfloat *); 
extern void APIENTRY glGetCombinerOutputParameterivNV(GLenum, GLenum, GLenum, GLint *);
extern void APIENTRY glGetFinalCombinerInputParameterfvNV(GLenum, GLenum, GLfloat *); 
extern void APIENTRY glGetFinalCombinerInputParameterivNV(GLenum, GLenum, GLint *);
#endif // GL_GLEXT_PROTOTYPES 
#else // _WIN32
typedef void (APIENTRY * PFNGLCOMBINERPARAMETERFVNVPROC) (GLenum pname, const GLfloat *params);
typedef void (APIENTRY * PFNGLCOMBINERPARAMETERIVNVPROC) (GLenum pname, const GLint *params); 
typedef void (APIENTRY * PFNGLCOMBINERPARAMETERFNVPROC)  (GLenum pname, GLfloat param); 
typedef void (APIENTRY * PFNGLCOMBINERPARAMETERINVPROC)  (GLenum pname, GLint param); 
typedef void (APIENTRY * PFNGLCOMBINERINPUTNVPROC)       (GLenum stage, GLenum portion, GLenum variable, GLenum input, GLenum mapping, GLenum componentUsage); 
typedef void (APIENTRY * PFNGLCOMBINEROUTPUTNVPROC)      (GLenum stage, GLenum portion, GLenum abOutput, GLenum cdOutput, GLenum sumOutput, GLenum scale, GLenum bias, GLboolean abDotProduct, GLboolean cdDotProduct, GLboolean muxSum); 
typedef void (APIENTRY * PFNGLFINALCOMBINERINPUTNVPROC)  (GLenum variable, GLenum input, GLenum mapping, GLenum componentUsage); 
typedef void (APIENTRY * PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC) (GLenum stage, GLenum portion, GLenum variable, GLenum pname, GLfloat *params); 
typedef void (APIENTRY * PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC) (GLenum stage, GLenum portion, GLenum variable, GLenum pname, GLint *params); 
typedef void (APIENTRY * PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC)(GLenum stage, GLenum portion, GLenum pname, GLfloat *params); 
typedef void (APIENTRY * PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC)(GLenum stage, GLenum portion, GLenum pname, GLint *params);
typedef void (APIENTRY * PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC)(GLenum variable, GLenum pname, GLfloat *params); 
typedef void (APIENTRY * PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC)(GLenum variable, GLenum pname, GLint *params);
#endif // not _WIN32

#ifdef NV_REGISTER_COMBINERS_INITIALIZE
#include<string.h> // string manipulation for runtime-check

#ifdef _WIN32
PFNGLCOMBINERPARAMETERFVNVPROC              glCombinerParameterfvNV              = NULL;
PFNGLCOMBINERPARAMETERIVNVPROC              glCombinerParameterivNV              = NULL;
PFNGLCOMBINERPARAMETERFNVPROC               glCombinerParameterfNV               = NULL;
PFNGLCOMBINERPARAMETERINVPROC               glCombinerParameteriNV               = NULL;
PFNGLCOMBINERINPUTNVPROC                    glCombinerInputNV                    = NULL;
PFNGLCOMBINEROUTPUTNVPROC                   glCombinerOutputNV                   = NULL;
PFNGLFINALCOMBINERINPUTNVPROC               glFinalCombinerInputNV               = NULL;
PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC      glGetCombinerInputParameterfvNV      = NULL;
PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC      glGetCombinerInputParameterivNV      = NULL;
PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC     glGetCombinerOutputParameterfvNV     = NULL;
PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC     glGetCombinerOutputParameterivNV     = NULL;
PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC glGetFinalCombinerInputParameterfvNV = NULL;
PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC glGetFinalCombinerInputParameterivNV = NULL;
#endif // _WIN32

int CheckForNVRegisterCombinersSupport(void) {
    const char search[]="GL_NV_register_combiners";
    int i, pos=0;
    int maxpos=strlen(search)-1;
    char extensions[10000];
    strcpy(extensions,(const char *)glGetString(GL_EXTENSIONS));
    int len=strlen(extensions);
    for (i=0; i<len; i++) {
        if ((i==0) || ((i>1) && extensions[i-1]==' ')) {
             pos=0;
             while(extensions[i]!=' ') {
                if (extensions[i]==search[pos]) pos++;
                if ((pos>maxpos) && extensions[i+1]==' ') {
		  //printf(search);
		  //  printf(" supported.\n");
                     return 1;
                }
                i++;
            }
        }
    }
    //printf(search);
    //printf(" not supported.\n");
    return 0;
}

int GL_NV_register_combiners_Init(void) {
    if (!CheckForNVRegisterCombinersSupport()) return 0;

#ifdef _WIN32
    glCombinerParameterfvNV=(PFNGLCOMBINERPARAMETERFVNVPROC) wglGetProcAddress("glCombinerParameterfvNV");
        if (glCombinerParameterfvNV==NULL) {fprintf(stderr,"glCombinerParameterfvNV not found.\n"); return 0;}
    glCombinerParameterivNV=(PFNGLCOMBINERPARAMETERIVNVPROC) wglGetProcAddress("glCombinerParameterivNV");
        if (glCombinerParameterivNV==NULL) {fprintf(stderr,"glCombinerParameterivNV not found.\n"); return 0;}
    glCombinerParameterfNV=(PFNGLCOMBINERPARAMETERFNVPROC) wglGetProcAddress("glCombinerParameterfNV");
        if (glCombinerParameterfvNV==NULL) {fprintf(stderr,"glCombinerParameterfNV not found.\n"); return 0;}
    glCombinerParameteriNV=(PFNGLCOMBINERPARAMETERINVPROC) wglGetProcAddress("glCombinerParameteriNV");
        if (glCombinerParameterivNV==NULL) {fprintf(stderr,"glCombinerParameteriNV not found.\n"); return 0;}
    glCombinerInputNV=(PFNGLCOMBINERINPUTNVPROC) wglGetProcAddress("glCombinerInputNV");
        if (glCombinerInputNV==NULL) {fprintf(stderr,"glCombinerInputNV not found.\n"); return 0;}
    glCombinerOutputNV=(PFNGLCOMBINEROUTPUTNVPROC) wglGetProcAddress("glCombinerOutputNV");
        if (glCombinerOutputNV==NULL) {fprintf(stderr,"glCombinerOutputNV not found.\n"); return 0;}
    glFinalCombinerInputNV=(PFNGLFINALCOMBINERINPUTNVPROC) wglGetProcAddress("glFinalCombinerInputNV");
        if (glFinalCombinerInputNV==NULL) {fprintf(stderr,"glFinalCombinerInputNV not found.\n"); return 0;}
    glGetCombinerInputParameterfvNV=(PFNGLGETCOMBINERINPUTPARAMETERFVNVPROC) wglGetProcAddress("glGetCombinerInputParameterfvNV");
        if (glGetCombinerInputParameterfvNV==NULL) {fprintf(stderr,"glGetCombinerInputParameterfvNV not found.\n"); return 0;}
    glGetCombinerInputParameterivNV=(PFNGLGETCOMBINERINPUTPARAMETERIVNVPROC) wglGetProcAddress("glGetCombinerInputParameterivNV");
        if (glGetCombinerInputParameterivNV==NULL) {fprintf(stderr,"glGetCombinerInputParameterivNV not found.\n"); return 0;}
    glGetCombinerOutputParameterfvNV=(PFNGLGETCOMBINEROUTPUTPARAMETERFVNVPROC) wglGetProcAddress("glGetCombinerOutputParameterfvNV");
        if (glGetCombinerOutputParameterfvNV==NULL) {fprintf(stderr,"glGetCombinerOutputParameterfvNV not found.\n"); return 0;}
    glGetCombinerOutputParameterivNV=(PFNGLGETCOMBINEROUTPUTPARAMETERIVNVPROC) wglGetProcAddress("glGetCombinerOutputParameterivNV");
        if (glGetCombinerOutputParameterivNV==NULL) {fprintf(stderr,"glGetCombinerOutputParameterivNV not found.\n"); return 0;}
    glGetFinalCombinerInputParameterfvNV=(PFNGLGETFINALCOMBINERINPUTPARAMETERFVNVPROC) wglGetProcAddress("glGetFinalCombinerInputParameterfvNV");
        if (glGetFinalCombinerInputParameterfvNV==NULL) {fprintf(stderr,"glGetFinalCombinerInputParameterfvNV not found.\n"); return 0;}
    glGetFinalCombinerInputParameterivNV=(PFNGLGETFINALCOMBINERINPUTPARAMETERIVNVPROC) wglGetProcAddress("glGetFinalCombinerInputParameterivNV");
        if (glGetFinalCombinerInputParameterivNV==NULL) {fprintf(stderr,"glGetFinalCombinerInputParameterivNV not found.\n"); return 0;}
#endif // _WIN32
    return 1;
}

#endif // NV_REGISTER_COMBINERS_INITIALIZE

#ifdef __cplusplus
}
#endif

#endif // not __NV_REGISTER_COMBINERS_H_
