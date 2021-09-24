/*
 *  gstvaapiutils_glx.c - GLX utilties
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2012 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#define _GNU_SOURCE 1           /* RTLD_DEFAULT */
#include "sysdeps.h"
#include <math.h>
#include <dlfcn.h>
#include "gstvaapiutils_glx.h"
#include "gstvaapiutils_x11.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/** Lookup for substring NAME in string EXT using SEP as separators */
static gboolean
find_string (const gchar * name, const gchar * ext, const gchar * sep)
{
  const gchar *end;
  int name_len, n;

  if (!name || !ext)
    return FALSE;

  end = ext + strlen (ext);
  name_len = strlen (name);
  while (ext < end) {
    n = strcspn (ext, sep);
    if (n == name_len && strncmp (name, ext, n) == 0)
      return TRUE;
    ext += (n + 1);
  }
  return FALSE;
}

/**
 * gl_get_error_string:
 * @error: an OpenGL error enumeration
 *
 * Retrieves the string representation the OpenGL @error.
 *
 * Return error: the static string representing the OpenGL @error
 */
const gchar *
gl_get_error_string (GLenum error)
{
  switch (error) {
#define MAP(id, str) \
        case id: return str " (" #id ")"
      MAP (GL_NO_ERROR, "no error");
      MAP (GL_INVALID_ENUM, "invalid enumerant");
      MAP (GL_INVALID_VALUE, "invalid value");
      MAP (GL_INVALID_OPERATION, "invalid operation");
      MAP (GL_STACK_OVERFLOW, "stack overflow");
      MAP (GL_STACK_UNDERFLOW, "stack underflow");
      MAP (GL_OUT_OF_MEMORY, "out of memory");
#ifdef GL_INVALID_FRAMEBUFFER_OPERATION_EXT
      MAP (GL_INVALID_FRAMEBUFFER_OPERATION_EXT,
          "invalid framebuffer operation");
#endif
#undef MAP
    default:
      break;
  };
  return "<unknown>";
}

/**
 * gl_purge_errors:
 *
 * Purges all OpenGL errors. This function is generally useful to
 * clear up the pending errors prior to calling gl_check_error().
 */
void
gl_purge_errors (void)
{
  while (glGetError () != GL_NO_ERROR); /* nothing */
}

/**
 * gl_check_error:
 *
 * Checks whether there is any OpenGL error pending.
 *
 * Return value: %TRUE if an error was encountered
 */
gboolean
gl_check_error (void)
{
  GLenum error;
  gboolean has_errors = FALSE;

  while ((error = glGetError ()) != GL_NO_ERROR) {
    GST_DEBUG ("glError: %s caught", gl_get_error_string (error));
    has_errors = TRUE;
  }
  return has_errors;
}

/**
 * gl_get_param:
 * @param: the parameter name
 * @pval: return location for the value
 *
 * This function is a wrapper around glGetIntegerv() that does extra
 * error checking.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_get_param (GLenum param, guint * pval)
{
  GLint val;

  gl_purge_errors ();
  glGetIntegerv (param, &val);
  if (gl_check_error ())
    return FALSE;

  if (pval)
    *pval = val;
  return TRUE;
}

/**
 * gl_get_texture_param:
 * @target: the target to which the texture is bound
 * @param: the parameter name
 * @pval: return location for the value
 *
 * This function is a wrapper around glGetTexLevelParameteriv() that
 * does extra error checking.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_get_texture_param (GLenum target, GLenum param, guint * pval)
{
  GLint val;

  gl_purge_errors ();
  glGetTexLevelParameteriv (target, 0, param, &val);
  if (gl_check_error ())
    return FALSE;

  if (pval)
    *pval = val;
  return TRUE;
}

/**
 * gl_get_texture_binding:
 * @target: a texture target
 *
 * Determines the texture binding type for the specified target.
 *
 * Return value: texture binding type for @target
 */
static GLenum
gl_get_texture_binding (GLenum target)
{
  GLenum binding;

  switch (target) {
    case GL_TEXTURE_1D:
      binding = GL_TEXTURE_BINDING_1D;
      break;
    case GL_TEXTURE_2D:
      binding = GL_TEXTURE_BINDING_2D;
      break;
    case GL_TEXTURE_3D:
      binding = GL_TEXTURE_BINDING_3D;
      break;
    case GL_TEXTURE_RECTANGLE_ARB:
      binding = GL_TEXTURE_BINDING_RECTANGLE_ARB;
      break;
    default:
      binding = 0;
      break;
  }
  return binding;
}

/**
 * gl_set_bgcolor:
 * @color: the requested RGB color
 *
 * Sets background color to the RGB @color. This basically is a
 * wrapper around glClearColor().
 */
void
gl_set_bgcolor (guint32 color)
{
  glClearColor (
      ((color >> 16) & 0xff) / 255.0f,
      ((color >> 8) & 0xff) / 255.0f, (color & 0xff) / 255.0f, 1.0f);
}

/**
 * gl_perspective:
 * @fovy: the field of view angle, in degrees, in the y direction
 * @aspect: the aspect ratio that determines the field of view in the
 *   x direction.  The aspect ratio is the ratio of x (width) to y
 *   (height)
 * @zNear: the distance from the viewer to the near clipping plane
 *   (always positive)
 * @zFar: the distance from the viewer to the far clipping plane
 *   (always positive)
 *
 * Specified a viewing frustum into the world coordinate system. This
 * basically is the Mesa implementation of gluPerspective().
 */
static void
gl_perspective (GLdouble fovy, GLdouble aspect, GLdouble near_val,
    GLdouble far_val)
{
  GLdouble left, right, top, bottom;

  /* Source (Q 9.085):
     <http://www.opengl.org/resources/faq/technical/transformations.htm> */
  top = tan (fovy * M_PI / 360.0) * near_val;
  bottom = -top;
  left = aspect * bottom;
  right = aspect * top;
  glFrustum (left, right, bottom, top, near_val, far_val);
}

/**
 * gl_resize:
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Resizes the OpenGL viewport to the specified dimensions, using an
 * orthogonal projection. (0,0) represents the top-left corner of the
 * window.
 */
void
gl_resize (guint width, guint height)
{
#define FOVY     60.0f
#define ASPECT   1.0f
#define Z_NEAR   0.1f
#define Z_FAR    100.0f
#define Z_CAMERA 0.869f

  glViewport (0, 0, width, height);
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  gl_perspective (FOVY, ASPECT, Z_NEAR, Z_FAR);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();

  glTranslatef (-0.5f, -0.5f, -Z_CAMERA);
  glScalef (1.0f / width, -1.0f / height, 1.0f / width);
  glTranslatef (0.0f, -1.0f * height, 0.0f);
}

/**
 * gl_create_context:
 * @dpy: an X11 #Display
 * @screen: the associated screen of @dpy
 * @parent: the parent #GLContextState, or %NULL if none is to be used
 *
 * Creates a GLX context sharing textures and displays lists with
 * @parent, if not %NULL.
 *
 * Return value: the newly created GLX context
 */
GLContextState *
gl_create_context (Display * dpy, int screen, GLContextState * parent)
{
  GLContextState *cs;
  GLXFBConfig *fbconfigs = NULL;
  int fbconfig_id, val, n, n_fbconfigs;
  Status status;

  static GLint fbconfig_attrs[] = {
    GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_DOUBLEBUFFER, True,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    None
  };

  const GLint rgba_colors[4] = {
    GLX_RED_SIZE,
    GLX_GREEN_SIZE,
    GLX_BLUE_SIZE,
    GLX_ALPHA_SIZE
  };

  cs = malloc (sizeof (*cs));
  if (!cs)
    goto error;

  if (parent) {
    cs->display = parent->display;
    cs->window = parent->window;
    screen = DefaultScreen (parent->display);
  } else {
    cs->display = dpy;
    cs->window = None;
  }
  cs->visual = NULL;
  cs->context = NULL;
  cs->swapped_buffers = FALSE;

  if (parent && parent->context) {
    status = glXQueryContext (parent->display,
        parent->context, GLX_FBCONFIG_ID, &fbconfig_id);
    if (status != Success)
      goto error;

    if (fbconfig_id == GLX_DONT_CARE)
      goto choose_fbconfig;

    fbconfigs = glXGetFBConfigs (parent->display, screen, &n_fbconfigs);
    if (!fbconfigs)
      goto error;

    /* Find out a 8 bit GLXFBConfig compatible with the parent context */
    for (n = 0; n < n_fbconfigs; n++) {
      gboolean sizes_correct = FALSE;
      int cn;

      status = glXGetFBConfigAttrib (parent->display,
          fbconfigs[n], GLX_FBCONFIG_ID, &val);
      if (status != Success)
        goto error;
      if (val != fbconfig_id)
        continue;

      /* Iterate over RGBA sizes in fbconfig */
      for (cn = 0; cn < 4; cn++) {
        int size = 0;

        status = glXGetFBConfigAttrib (parent->display,
            fbconfigs[n], rgba_colors[cn], &size);
        if (status != Success)
          goto error;

        /* Last check is for alpha
         * and alpha is optional */
        if (cn == 3) {
          if (size == 0 || size == 8) {
            sizes_correct = TRUE;
            break;
          }
        } else if (size != 8)
          break;
      }
      if (sizes_correct)
        break;
    }
    if (n == n_fbconfigs)
      goto error;
  } else {
  choose_fbconfig:
    fbconfigs = glXChooseFBConfig (cs->display,
        screen, fbconfig_attrs, &n_fbconfigs);
    if (!fbconfigs)
      goto error;

    /* Select the first one */
    n = 0;
  }

  cs->visual = glXGetVisualFromFBConfig (cs->display, fbconfigs[n]);
  cs->context = glXCreateNewContext (cs->display,
      fbconfigs[n], GLX_RGBA_TYPE, parent ? parent->context : NULL, True);
  if (!cs->context)
    goto error;

end:
  if (fbconfigs)
    XFree (fbconfigs);
  return cs;

  /* ERRORS */
error:
  {
    gl_destroy_context (cs);
    cs = NULL;
    goto end;
  }
}

/**
 * gl_destroy_context:
 * @cs: a #GLContextState
 *
 * Destroys the GLX context @cs
 */
void
gl_destroy_context (GLContextState * cs)
{
  if (!cs)
    return;

  if (cs->visual) {
    XFree (cs->visual);
    cs->visual = NULL;
  }

  if (cs->display && cs->context) {
    if (glXGetCurrentContext () == cs->context) {
      /* XXX: if buffers were never swapped, the application
         will crash later with the NVIDIA driver */
      if (!cs->swapped_buffers)
        gl_swap_buffers (cs);
      glXMakeCurrent (cs->display, None, NULL);
    }
    glXDestroyContext (cs->display, cs->context);
    cs->display = NULL;
    cs->context = NULL;
  }
  free (cs);
}

/**
 * gl_get_current_context:
 * @cs: return location to the current #GLContextState
 *
 * Retrieves the current GLX context, display and drawable packed into
 * the #GLContextState struct.
 */
void
gl_get_current_context (GLContextState * cs)
{
  cs->display = glXGetCurrentDisplay ();
  cs->window = glXGetCurrentDrawable ();
  cs->context = glXGetCurrentContext ();
}

/**
 * gl_set_current_context:
 * @new_cs: the requested new #GLContextState
 * @old_cs: return location to the context that was previously current
 *
 * Makes the @new_cs GLX context the current GLX rendering context of
 * the calling thread, replacing the previously current context if
 * there was one.
 *
 * If @old_cs is non %NULL, the previously current GLX context and
 * window are recorded.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_set_current_context (GLContextState * new_cs, GLContextState * old_cs)
{
  /* If display is NULL, this could be that new_cs was retrieved from
     gl_get_current_context() with none set previously. If that case,
     the other fields are also NULL and we don't return an error */
  if (!new_cs->display)
    return !new_cs->window && !new_cs->context;

  if (old_cs) {
    if (old_cs == new_cs)
      return TRUE;
    gl_get_current_context (old_cs);
    if (old_cs->display == new_cs->display &&
        old_cs->window == new_cs->window && old_cs->context == new_cs->context)
      return TRUE;
  }
  return glXMakeCurrent (new_cs->display, new_cs->window, new_cs->context);
}

/**
 * gl_swap_buffers:
 * @cs: a #GLContextState
 *
 * Promotes the contents of the back buffer of the @win window to
 * become the contents of the front buffer. This simply is wrapper
 * around glXSwapBuffers().
 */
void
gl_swap_buffers (GLContextState * cs)
{
  glXSwapBuffers (cs->display, cs->window);
  cs->swapped_buffers = TRUE;
}

static inline gboolean
_init_texture_state (GLTextureState * ts, GLenum target, GLuint texture,
    gboolean enabled)
{
  GLenum binding;

  ts->target = target;

  if (enabled) {
    binding = gl_get_texture_binding (target);
    if (!binding)
      return FALSE;
    if (!gl_get_param (binding, &ts->old_texture))
      return FALSE;
    ts->was_enabled = TRUE;
    ts->was_bound = texture == ts->old_texture;
  } else {
    ts->old_texture = 0;
    ts->was_enabled = FALSE;
    ts->was_bound = FALSE;
  }

  return TRUE;
}

static inline gboolean
_bind_enabled_texture (GLenum target, GLuint texture)
{
  gl_purge_errors ();
  glBindTexture (target, texture);
  if (gl_check_error ())
    return FALSE;
  return TRUE;
}

/**
 * gl_bind_texture:
 * @ts: a #GLTextureState
 * @target: the target to which the texture is bound
 * @texture: the name of a texture
 *
 * Binds @texture to the specified @target, while recording the
 * previous state in @ts.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_bind_texture (GLTextureState * ts, GLenum target, GLuint texture)
{
  gboolean enabled;

  enabled = (gboolean) glIsEnabled (target);
  if (!_init_texture_state (ts, target, texture, enabled))
    return FALSE;
  if (ts->was_bound)
    return TRUE;
  if (!enabled)
    glEnable (target);

  return _bind_enabled_texture (target, texture);
}

/**
 * gl3_bind_texture_2d:
 * @ts: a #GLTextureState
 * @target: the target to which the texture is bound
 * @texture: the name of a texture
 *
 * Binds @texture to the specified @target, while recording the
 * previous state in @ts.
 *
 * This function is for OpenGL3 API and for targets type GL_TEXTURE_2D.
 *
 * Return value: %TRUE on success
 */
gboolean
gl3_bind_texture_2d (GLTextureState * ts, GLenum target, GLuint texture)
{
  if (target != GL_TEXTURE_2D)
    return FALSE;

  if (!_init_texture_state (ts, target, texture, TRUE))
    return FALSE;
  if (ts->was_bound)
    return TRUE;

  return _bind_enabled_texture (target, texture);
}

/**
 * gl_unbind_texture:
 * @ts: a #GLTextureState
 *
 * Rebinds the texture that was previously bound and recorded in @ts.
 */
void
gl_unbind_texture (GLTextureState * ts)
{
  if (!ts->was_bound && ts->old_texture)
    glBindTexture (ts->target, ts->old_texture);
  if (!ts->was_enabled)
    glDisable (ts->target);
}

/**
 * gl_create_texture:
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates a texture with the specified dimensions and @format. The
 * internal format will be automatically derived from @format.
 *
 * Return value: the newly created texture name
 */
GLuint
gl_create_texture (GLenum target, GLenum format, guint width, guint height)
{
  GLenum internal_format;
  GLuint texture;
  GLTextureState ts;
  guint bytes_per_component;

  internal_format = format;
  switch (format) {
    case GL_LUMINANCE:
      bytes_per_component = 1;
      break;
    case GL_LUMINANCE_ALPHA:
      bytes_per_component = 2;
      break;
    case GL_RGBA:
    case GL_BGRA:
      internal_format = GL_RGBA;
      bytes_per_component = 4;
      break;
    default:
      bytes_per_component = 0;
      break;
  }
  g_assert (bytes_per_component > 0);

  glGenTextures (1, &texture);
  if (!gl_bind_texture (&ts, target, texture))
    return 0;
  glTexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei (GL_UNPACK_ALIGNMENT, bytes_per_component);
  glTexImage2D (target,
      0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, NULL);
  gl_unbind_texture (&ts);
  return texture;
}

/**
 * get_proc_address:
 * @name: the name of the OpenGL extension function to lookup
 *
 * Returns the specified OpenGL extension function
 *
 * Return value: the OpenGL extension matching @name, or %NULL if none
 *   was found
 */
typedef void (*GLFuncPtr) (void);
typedef GLFuncPtr (*GLXGetProcAddressProc) (const gchar *);

static GLFuncPtr
get_proc_address_default (const gchar * name)
{
  return NULL;
}

static GLXGetProcAddressProc
get_proc_address_func (void)
{
  GLXGetProcAddressProc get_proc_func;

  dlerror ();
  *(void **) (&get_proc_func) = dlsym (RTLD_DEFAULT, "glXGetProcAddress");
  if (!dlerror ())
    return get_proc_func;

  *(void **) (&get_proc_func) = dlsym (RTLD_DEFAULT, "glXGetProcAddressARB");
  if (!dlerror ())
    return get_proc_func;

  return get_proc_address_default;
}

static inline GLFuncPtr
get_proc_address (const gchar * name)
{
  static GLXGetProcAddressProc get_proc_func = NULL;
  if (!get_proc_func)
    get_proc_func = get_proc_address_func ();
  return get_proc_func (name);
}

/**
 * gl_init_vtable:
 *
 * Initializes the global #GLVTable.
 *
 * Return value: the #GLVTable filled in with OpenGL extensions, or
 *   %NULL on error.
 */
static GLVTable gl_vtable_static;

static GLVTable *
gl_init_vtable (void)
{
  GLVTable *const gl_vtable = &gl_vtable_static;
  const gchar *gl_extensions = (const gchar *) glGetString (GL_EXTENSIONS);
  gboolean has_extension;

  /* GLX_EXT_texture_from_pixmap */
  gl_vtable->glx_create_pixmap = (PFNGLXCREATEPIXMAPPROC)
      get_proc_address ("glXCreatePixmap");
  if (!gl_vtable->glx_create_pixmap)
    return NULL;
  gl_vtable->glx_destroy_pixmap = (PFNGLXDESTROYPIXMAPPROC)
      get_proc_address ("glXDestroyPixmap");
  if (!gl_vtable->glx_destroy_pixmap)
    return NULL;
  gl_vtable->glx_bind_tex_image = (PFNGLXBINDTEXIMAGEEXTPROC)
      get_proc_address ("glXBindTexImageEXT");
  if (!gl_vtable->glx_bind_tex_image)
    return NULL;
  gl_vtable->glx_release_tex_image = (PFNGLXRELEASETEXIMAGEEXTPROC)
      get_proc_address ("glXReleaseTexImageEXT");
  if (!gl_vtable->glx_release_tex_image)
    return NULL;

  /* GL_ARB_framebuffer_object */
  has_extension = (find_string ("GL_ARB_framebuffer_object", gl_extensions, " ")
      || find_string ("GL_EXT_framebuffer_object", gl_extensions, " ")
      );
  if (has_extension) {
    gl_vtable->gl_gen_framebuffers = (PFNGLGENFRAMEBUFFERSEXTPROC)
        get_proc_address ("glGenFramebuffersEXT");
    if (!gl_vtable->gl_gen_framebuffers)
      return NULL;
    gl_vtable->gl_delete_framebuffers = (PFNGLDELETEFRAMEBUFFERSEXTPROC)
        get_proc_address ("glDeleteFramebuffersEXT");
    if (!gl_vtable->gl_delete_framebuffers)
      return NULL;
    gl_vtable->gl_bind_framebuffer = (PFNGLBINDFRAMEBUFFEREXTPROC)
        get_proc_address ("glBindFramebufferEXT");
    if (!gl_vtable->gl_bind_framebuffer)
      return NULL;
    gl_vtable->gl_gen_renderbuffers = (PFNGLGENRENDERBUFFERSEXTPROC)
        get_proc_address ("glGenRenderbuffersEXT");
    if (!gl_vtable->gl_gen_renderbuffers)
      return NULL;
    gl_vtable->gl_delete_renderbuffers = (PFNGLDELETERENDERBUFFERSEXTPROC)
        get_proc_address ("glDeleteRenderbuffersEXT");
    if (!gl_vtable->gl_delete_renderbuffers)
      return NULL;
    gl_vtable->gl_bind_renderbuffer = (PFNGLBINDRENDERBUFFEREXTPROC)
        get_proc_address ("glBindRenderbufferEXT");
    if (!gl_vtable->gl_bind_renderbuffer)
      return NULL;
    gl_vtable->gl_renderbuffer_storage = (PFNGLRENDERBUFFERSTORAGEEXTPROC)
        get_proc_address ("glRenderbufferStorageEXT");
    if (!gl_vtable->gl_renderbuffer_storage)
      return NULL;
    gl_vtable->gl_framebuffer_renderbuffer =
        (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)
        get_proc_address ("glFramebufferRenderbufferEXT");
    if (!gl_vtable->gl_framebuffer_renderbuffer)
      return NULL;
    gl_vtable->gl_framebuffer_texture_2d = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)
        get_proc_address ("glFramebufferTexture2DEXT");
    if (!gl_vtable->gl_framebuffer_texture_2d)
      return NULL;
    gl_vtable->gl_check_framebuffer_status =
        (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)
        get_proc_address ("glCheckFramebufferStatusEXT");
    if (!gl_vtable->gl_check_framebuffer_status)
      return NULL;
    gl_vtable->has_framebuffer_object = TRUE;
  }
  return gl_vtable;
}

/**
 * gl_get_vtable:
 *
 * Retrieves a VTable for OpenGL extensions.
 *
 * Return value: VTable for OpenGL extensions
 */
GLVTable *
gl_get_vtable (void)
{
  static gsize gl_vtable_init = FALSE;
  static GLVTable *gl_vtable = NULL;

  if (g_once_init_enter (&gl_vtable_init)) {
    gl_vtable = gl_init_vtable ();
    g_once_init_leave (&gl_vtable_init, TRUE);
  }
  return gl_vtable;
}

/**
 * gl_create_pixmap_object:
 * @dpy: an X11 #Display
 * @width: the request width, in pixels
 * @height: the request height, in pixels
 *
 * Creates a #GLPixmapObject of the specified dimensions. This
 * requires the GLX_EXT_texture_from_pixmap extension.
 *
 * Return value: the newly created #GLPixmapObject object
 */
GLPixmapObject *
gl_create_pixmap_object (Display * dpy, guint width, guint height)
{
  GLVTable *const gl_vtable = gl_get_vtable ();
  GLPixmapObject *pixo;
  GLXFBConfig *fbconfig;
  int screen;
  Window rootwin;
  XWindowAttributes wattr;
  int *attr;
  int n_fbconfig_attrs;

  int fbconfig_attrs[32] = {
    GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
    GLX_DOUBLEBUFFER, GL_FALSE,
    GLX_RENDER_TYPE, GLX_RGBA_BIT,
    GLX_X_RENDERABLE, GL_TRUE,
    GLX_Y_INVERTED_EXT, GL_TRUE,
    GLX_RED_SIZE, 8,
    GLX_GREEN_SIZE, 8,
    GLX_BLUE_SIZE, 8,
    GLX_ALPHA_SIZE, 8,
    GL_NONE,
  };

  int pixmap_attrs[10] = {
    GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
    GLX_MIPMAP_TEXTURE_EXT, GL_FALSE,
    GL_NONE,
  };

  if (!gl_vtable)
    return NULL;

  screen = DefaultScreen (dpy);
  rootwin = RootWindow (dpy, screen);

  /* XXX: this won't work for different displays */
  if (!gl_vtable->has_texture_from_pixmap) {
    const gchar *glx_extensions = glXQueryExtensionsString (dpy, screen);
    if (!glx_extensions)
      return NULL;
    if (!find_string ("GLX_EXT_texture_from_pixmap", glx_extensions, " "))
      return NULL;
    gl_vtable->has_texture_from_pixmap = TRUE;
  }

  pixo = calloc (1, sizeof (*pixo));
  if (!pixo)
    return NULL;

  pixo->dpy = dpy;
  pixo->width = width;
  pixo->height = height;
  pixo->pixmap = None;
  pixo->glx_pixmap = None;
  pixo->is_bound = FALSE;

  XGetWindowAttributes (dpy, rootwin, &wattr);
  pixo->pixmap = XCreatePixmap (dpy, rootwin, width, height, wattr.depth);
  if (!pixo->pixmap)
    goto error;

  /* Initialize FBConfig attributes */
  for (attr = fbconfig_attrs; *attr != GL_NONE; attr += 2);
  if (wattr.depth == 32) {
    *attr++ = GLX_ALPHA_SIZE;
    *attr++ = 8;
    *attr++ = GLX_BIND_TO_TEXTURE_RGBA_EXT;
    *attr++ = GL_TRUE;
  } else {
    *attr++ = GLX_BIND_TO_TEXTURE_RGB_EXT;
    *attr++ = GL_TRUE;
  }
  *attr++ = GL_NONE;

  fbconfig = glXChooseFBConfig (dpy, screen, fbconfig_attrs, &n_fbconfig_attrs);
  if (!fbconfig)
    goto error;

  /* Initialize GLX Pixmap attributes */
  for (attr = pixmap_attrs; *attr != GL_NONE; attr += 2);
  *attr++ = GLX_TEXTURE_FORMAT_EXT;
  if (wattr.depth == 32)
    *attr++ = GLX_TEXTURE_FORMAT_RGBA_EXT;
  else
    *attr++ = GLX_TEXTURE_FORMAT_RGB_EXT;
  *attr++ = GL_NONE;

  x11_trap_errors ();
  pixo->glx_pixmap = gl_vtable->glx_create_pixmap (dpy,
      fbconfig[0], pixo->pixmap, pixmap_attrs);
  free (fbconfig);
  if (x11_untrap_errors () != 0)
    goto error;

  pixo->target = GL_TEXTURE_2D;
  glGenTextures (1, &pixo->texture);
  if (!gl_bind_texture (&pixo->old_texture, pixo->target, pixo->texture))
    goto error;
  glTexParameteri (pixo->target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (pixo->target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl_unbind_texture (&pixo->old_texture);
  return pixo;

  /* ERRORS */
error:
  {
    gl_destroy_pixmap_object (pixo);
    return NULL;
  }
}

/**
 * gl_destroy_pixmap_object:
 * @pixo: a #GLPixmapObject
 *
 * Destroys the #GLPixmapObject object.
 */
void
gl_destroy_pixmap_object (GLPixmapObject * pixo)
{
  GLVTable *const gl_vtable = gl_get_vtable ();

  if (!pixo)
    return;

  gl_unbind_pixmap_object (pixo);

  if (pixo->texture) {
    glDeleteTextures (1, &pixo->texture);
    pixo->texture = 0;
  }

  if (pixo->glx_pixmap) {
    gl_vtable->glx_destroy_pixmap (pixo->dpy, pixo->glx_pixmap);
    pixo->glx_pixmap = None;
  }

  if (pixo->pixmap) {
    XFreePixmap (pixo->dpy, pixo->pixmap);
    pixo->pixmap = None;
  }
  free (pixo);
}

/**
 * gl_bind_pixmap_object:
 * @pixo: a #GLPixmapObject
 *
 * Defines a two-dimensional texture image. The texture image is taken
 * from the @pixo pixmap and need not be copied. The texture target,
 * format and size are derived from attributes of the @pixo pixmap.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_bind_pixmap_object (GLPixmapObject * pixo)
{
  GLVTable *const gl_vtable = gl_get_vtable ();

  if (pixo->is_bound)
    return TRUE;

  if (!gl_bind_texture (&pixo->old_texture, pixo->target, pixo->texture))
    return FALSE;

  x11_trap_errors ();
  gl_vtable->glx_bind_tex_image (pixo->dpy,
      pixo->glx_pixmap, GLX_FRONT_LEFT_EXT, NULL);
  XSync (pixo->dpy, False);
  if (x11_untrap_errors () != 0) {
    GST_DEBUG ("failed to bind pixmap");
    return FALSE;
  }

  pixo->is_bound = TRUE;
  return TRUE;
}

/**
 * gl_unbind_pixmap_object:
 * @pixo: a #GLPixmapObject
 *
 * Releases a color buffers that is being used as a texture.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_unbind_pixmap_object (GLPixmapObject * pixo)
{
  GLVTable *const gl_vtable = gl_get_vtable ();

  if (!pixo->is_bound)
    return TRUE;

  x11_trap_errors ();
  gl_vtable->glx_release_tex_image (pixo->dpy,
      pixo->glx_pixmap, GLX_FRONT_LEFT_EXT);
  XSync (pixo->dpy, False);
  if (x11_untrap_errors () != 0) {
    GST_DEBUG ("failed to release pixmap");
    return FALSE;
  }

  gl_unbind_texture (&pixo->old_texture);

  pixo->is_bound = FALSE;
  return TRUE;
}

/**
 * gl_create_framebuffer_object:
 * @target: the target to which the texture is bound
 * @texture: the GL texture to hold the framebuffer
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates an FBO with the specified texture and size.
 *
 * Return value: the newly created #GLFramebufferObject, or %NULL if
 *   an error occurred
 */
GLFramebufferObject *
gl_create_framebuffer_object (GLenum target,
    GLuint texture, guint width, guint height)
{
  GLVTable *const gl_vtable = gl_get_vtable ();
  GLFramebufferObject *fbo;
  GLenum status;

  if (!gl_vtable || !gl_vtable->has_framebuffer_object)
    return NULL;

  /* XXX: we only support GL_TEXTURE_2D at this time */
  if (target != GL_TEXTURE_2D)
    return NULL;

  fbo = calloc (1, sizeof (*fbo));
  if (!fbo)
    return NULL;

  fbo->width = width;
  fbo->height = height;
  fbo->fbo = 0;
  fbo->old_fbo = 0;
  fbo->is_bound = FALSE;

  gl_get_param (GL_FRAMEBUFFER_BINDING, &fbo->old_fbo);
  gl_vtable->gl_gen_framebuffers (1, &fbo->fbo);
  gl_vtable->gl_bind_framebuffer (GL_FRAMEBUFFER_EXT, fbo->fbo);
  gl_vtable->gl_framebuffer_texture_2d (GL_FRAMEBUFFER_EXT,
      GL_COLOR_ATTACHMENT0_EXT, target, texture, 0);

  status = gl_vtable->gl_check_framebuffer_status (GL_DRAW_FRAMEBUFFER_EXT);
  gl_vtable->gl_bind_framebuffer (GL_FRAMEBUFFER_EXT, fbo->old_fbo);
  if (status != GL_FRAMEBUFFER_COMPLETE_EXT)
    goto error;
  return fbo;

  /* ERRORS */
error:
  {
    gl_destroy_framebuffer_object (fbo);
    return NULL;
  }
}

/**
 * gl_destroy_framebuffer_object:
 * @fbo: a #GLFramebufferObject
 *
 * Destroys the @fbo object.
 */
void
gl_destroy_framebuffer_object (GLFramebufferObject * fbo)
{
  GLVTable *const gl_vtable = gl_get_vtable ();

  if (!fbo)
    return;

  gl_unbind_framebuffer_object (fbo);

  if (fbo->fbo) {
    gl_vtable->gl_delete_framebuffers (1, &fbo->fbo);
    fbo->fbo = 0;
  }
  free (fbo);
}

/**
 * gl_bind_framebuffer_object:
 * @fbo: a #GLFramebufferObject
 *
 * Binds @fbo object.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_bind_framebuffer_object (GLFramebufferObject * fbo)
{
  GLVTable *const gl_vtable = gl_get_vtable ();
  const guint width = fbo->width;
  const guint height = fbo->height;

  const guint attribs = (GL_VIEWPORT_BIT |
      GL_CURRENT_BIT | GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT);

  if (fbo->is_bound)
    return TRUE;

  gl_get_param (GL_FRAMEBUFFER_BINDING, &fbo->old_fbo);
  gl_vtable->gl_bind_framebuffer (GL_FRAMEBUFFER_EXT, fbo->fbo);
  glPushAttrib (attribs);
  glMatrixMode (GL_PROJECTION);
  glPushMatrix ();
  glLoadIdentity ();
  glMatrixMode (GL_MODELVIEW);
  glPushMatrix ();
  glLoadIdentity ();
  glViewport (0, 0, width, height);
  glTranslatef (-1.0f, -1.0f, 0.0f);
  glScalef (2.0f / width, 2.0f / height, 1.0f);

  fbo->is_bound = TRUE;
  return TRUE;
}

/**
 * gl_unbind_framebuffer_object:
 * @fbo: a #GLFramebufferObject
 *
 * Releases @fbo object.
 *
 * Return value: %TRUE on success
 */
gboolean
gl_unbind_framebuffer_object (GLFramebufferObject * fbo)
{
  GLVTable *const gl_vtable = gl_get_vtable ();

  if (!fbo->is_bound)
    return TRUE;

  glPopAttrib ();
  glMatrixMode (GL_PROJECTION);
  glPopMatrix ();
  glMatrixMode (GL_MODELVIEW);
  glPopMatrix ();
  gl_vtable->gl_bind_framebuffer (GL_FRAMEBUFFER_EXT, fbo->old_fbo);

  fbo->is_bound = FALSE;
  return TRUE;
}

/**
 * gl_get_current_api:
 * @major: (out): (allow-none): the GL major version
 * @minor: (out): (allow-none): the GL minor version
 *
 * If an error occurs, @major and @minor aren't modified and
 * %GST_VAAPI_GL_API_NONE is returned.
 *
 * This is an adaptation of gst_gl_context_get_current_gl_api() from GstGL.
 *
 * Returns: The version supported by the OpenGL context current in the calling
 *          thread or %GST_VAAPI_GL_API_NONE
 */
GstVaapiGLApi
gl_get_current_api (guint * major, guint * minor)
{
  const gchar *version;
  gint maj, min, n, sret;
  GstVaapiGLApi ret = (1 << 31);

  while (ret != GST_VAAPI_GL_API_NONE) {
    version = (const gchar *) glGetString (GL_VERSION);
    if (!version)
      goto next;

    /* strlen (x.x) == 3 */
    n = strlen (version);
    if (n < 3)
      goto next;

    if (g_strstr_len (version, 9, "OpenGL ES")) {
      /* strlen (OpenGL ES x.x) == 13 */
      if (n < 13)
        goto next;

      sret = sscanf (&version[10], "%d.%d", &maj, &min);
      if (sret != 2)
        goto next;

      if (maj <= 0 || min < 0)
        goto next;

      if (maj == 1) {
        ret = GST_VAAPI_GL_API_GLES1;
        break;
      } else if (maj == 2 || maj == 3) {
        ret = GST_VAAPI_GL_API_GLES2;
        break;
      }

      goto next;
    } else {
      sret = sscanf (version, "%d.%d", &maj, &min);
      if (sret != 2)
        goto next;

      if (maj <= 0 || min < 0)
        goto next;

      if (maj > 3 || (maj == 3 && min > 1)) {
        GLuint context_flags = 0;

        ret = GST_VAAPI_GL_API_NONE;
        if (!gl_get_param (GL_CONTEXT_PROFILE_MASK, &context_flags))
          break;

        if (context_flags & GL_CONTEXT_CORE_PROFILE_BIT)
          ret |= GST_VAAPI_GL_API_OPENGL3;
        if (context_flags & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT)
          ret |= GST_VAAPI_GL_API_OPENGL;
        break;
      }

      ret = GST_VAAPI_GL_API_OPENGL;
      break;
    }

  next:
    /* iterate through the apis */
    ret >>= 1;
  }

  if (ret == GST_VAAPI_GL_API_NONE)
    return GST_VAAPI_GL_API_NONE;

  if (major)
    *major = maj;
  if (minor)
    *minor = min;

  return ret;
}
