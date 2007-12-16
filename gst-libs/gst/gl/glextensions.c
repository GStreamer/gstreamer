
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <GL/glx.h>
#include <GL/glxext.h>
#include <string.h>

#include "glextensions.h"

int
gl_have_extension (const char *name)
{
  const char *s;

  s = (const char *) glGetString (GL_EXTENSIONS);
  if (s == NULL)
    return FALSE;

  if (strstr (s, name))
    return TRUE;
  return FALSE;
}

extern __GLXextFuncPtr glXGetProcAddressARB (const GLubyte *);

#define DEFINE_FUNC_RET(name,return_type,prototype,args) \
return_type name prototype \
{ \
  static return_type (*func) prototype; \
  if (func == NULL) { \
    func = (void *) glXGetProcAddressARB ((unsigned char *) #name); \
  } \
  return func args; \
}

#define DEFINE_FUNC(name,prototype,args) \
void name prototype \
{ \
  static void (*func) prototype; \
  if (func == NULL) { \
    func = (void *) glXGetProcAddressARB ((unsigned char *) #name); \
  } \
  func args; \
}

DEFINE_FUNC_RET (glCreateShaderObjectARB, GLhandleARB,
    (GLenum shaderType), (shaderType));
#if 0
typedef GLhandleARB type_glCreateShaderObjectARB (GLenum shaderType);
GLhandleARB
glCreateShaderObjectARB (GLenum shaderType)
{
  type_glCreateShaderObjectARB *func;

  if (func == NULL) {
    func = (type_glCreateShaderObjectARB *)
        glXGetProcAddress ((unsigned char *) "glCreateShaderObjectARB");
  }
  return (*func) (shaderType);
}
#endif

DEFINE_FUNC (glShaderSourceARB,
    (GLhandleARB shaderObj, GLsizei count, const GLcharARB ** string,
        const GLint * length), (shaderObj, count, string, length));

DEFINE_FUNC (glUniform2fARB,
    (GLint location, GLfloat val1, GLfloat val2), (location, val1, val2));

DEFINE_FUNC_RET (glGetUniformLocationARB, GLint,
    (GLhandleARB programObj, const GLcharARB * name), (programObj, name));

DEFINE_FUNC (glUniform1iARB, (GLint location, GLint val), (location, val));

DEFINE_FUNC (glGetObjectParameterivARB, (GLhandleARB object, GLenum pname,
        GLint * params), (object, pname, params));

DEFINE_FUNC (glCompileShaderARB, (GLhandleARB shader), (shader));

DEFINE_FUNC (glGetInfoLogARB, (GLhandleARB object, GLsizei maxLength,
        GLsizei * length, GLcharARB * infoLog), (object, maxLength, length,
        infoLog));

DEFINE_FUNC_RET (glCreateProgramObjectARB, GLhandleARB, (void), ());

DEFINE_FUNC (glAttachObjectARB, (GLhandleARB program, GLhandleARB shader),
    (program, shader));

DEFINE_FUNC (glLinkProgramARB, (GLhandleARB program), (program));

DEFINE_FUNC (glUseProgramObjectARB, (GLhandleARB program), (program));

DEFINE_FUNC (glPixelDataRangeNV, (GLenum target, GLsizei length, void *pointer),
    (target, length, pointer));

DEFINE_FUNC_RET (glXGetSyncValuesOML, Bool,
    (Display * display, GLXDrawable drawable, int64_t * ust, int64_t * msc,
        int64_t * sbc), (display, drawable, ust, msc, sbc));

DEFINE_FUNC_RET (glXGetMscRateOML, Bool,
    (Display * display, GLXDrawable drawable, int32_t * numerator,
        int32_t * denominator), (display, drawable, numerator, denominator));

DEFINE_FUNC_RET (glXSwapBuffersMscOML, int64_t,
    (Display * display, GLXDrawable drawable, int64_t target_msc,
        int64_t divisor, int64_t remainder), (display, drawable, target_msc,
        divisor, remainder));

DEFINE_FUNC_RET (glXWaitForMscOML, Bool,
    (Display * display, GLXDrawable drawable, int64_t target_msc,
        int64_t divisor, int64_t remainder, int64_t * ust, int64_t * msc,
        int64_t * sbc), (display, drawable, target_msc, divisor, remainder, ust,
        msc, sbc));

DEFINE_FUNC_RET (glXWaitForSbcOML, Bool,
    (Display * display, GLXDrawable drawable, int64_t target_sbc, int64_t * ust,
        int64_t * msc, int64_t * sbc), (display, drawable, target_sbc, ust, msc,
        sbc));

DEFINE_FUNC_RET (glXSwapIntervalSGI, int, (int interval), (interval));

DEFINE_FUNC_RET (glXSwapIntervalMESA, int, (unsigned int interval), (interval));
