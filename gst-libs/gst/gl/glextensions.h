#ifndef __GST_GLEXTENSIONS_H__
#define __GST_GLEXTENSIONS_H__

#include <GL/gl.h>
#include <glib.h>

int gl_have_extension (const char *name);

GLhandleARB glCreateShaderObjectARB (GLenum shaderType);
void glShaderSourceARB (GLhandleARB shaderObj, GLsizei count, const GLcharARB ** string, const GLint *length);
void glUniform2fARB (GLint location, GLfloat val1, GLfloat val2);
GLint glGetUniformLocationARB (GLhandleARB programObj, const GLcharARB *name);
void glUniform1iARB (GLint location, GLint val);
void glCompileShaderARB (GLhandleARB shader);
void glGetObjectParameterivARB (GLhandleARB object, GLenum pname, GLint *params);
void glGetInfoLogARB (GLhandleARB object, GLsizei maxLength, GLsizei *length,
    GLcharARB *infoLog);
GLhandleARB glCreateProgramObjectARB (void);
void glAttachObjectARB (GLhandleARB program, GLhandleARB shader);
void glLinkProgramARB (GLhandleARB program);
void glUseProgramObjectARB (GLhandleARB program);
void glPixelDataRangeNV(GLenum target, GLsizei length, void *pointer);
void glActiveTexture(GLenum target);
Bool glXGetSyncValuesOML (Display *, GLXDrawable, int64_t *, int64_t *, int64_t *);
Bool glXGetMscRateOML (Display *, GLXDrawable, int32_t *, int32_t *);
int64_t glXSwapBuffersMscOML (Display *, GLXDrawable, int64_t, int64_t, int64_t);
Bool glXWaitForMscOML (Display *, GLXDrawable, int64_t, int64_t, int64_t, int64_t *, int64_t *, int64_t *);
Bool glXWaitForSbcOML (Display *, GLXDrawable, int64_t, int64_t *, int64_t *, int64_t *);
int glXSwapIntervalSGI (int);
int glXSwapIntervalMESA (unsigned int);

/* EXT_framebuffer_object */
Bool glIsRenderbufferEXT (GLuint renderbuffer);
void glBindRenderbufferEXT (GLenum target, GLuint renderbuffer);
void glDeleteRenderbuffersEXT (GLsizei n, GLuint *renderbuffers);
void glGenRenderbuffersEXT (GLsizei n, GLuint *renderbuffers);
void glRenderbufferStorageEXT (GLenum target, GLenum internalformat,
    GLsizei width, GLsizei height);
void glGetRenderbufferParameterivEXT (GLenum target, GLenum pname, GLint *params);
Bool glIsFramebufferEXT (GLuint framebuffer);
void glBindFramebufferEXT (GLenum target, GLuint framebuffer);
void glDeleteFramebuffersEXT (GLsizei n, GLuint *framebuffers);
void glGenFramebuffersEXT (GLsizei n, GLuint *framebuffers);
GLenum glCheckFramebufferStatusEXT (GLenum target);
void glFramebufferTexture1DEXT (GLenum target, GLenum attachment,
    GLenum textarget, GLuint texture, GLint level);
void glFramebufferTexture2DEXT (GLenum target, GLenum attachment,
    GLenum textarget, GLuint texture, GLint level);
void glFramebufferTexture3DEXT (GLenum target, GLenum attachment,
    GLenum textarget, GLuint texture, GLint level, GLint zoffset);
void glFramebufferRenderbufferEXT (GLenum target, GLenum attachment,
    GLenum renderbuffertarget, GLuint renderbuffer);
void glGetFramebufferAttachmentParameterivEXT (GLenum target, GLenum pname,
    GLint *params);
void glGenerateMipmapEXT (GLenum target);

#endif

