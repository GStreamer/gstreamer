/*
 * gstvaapiutils_egl.h - EGL utilities
 *
 * Copyright (C) 2014 Intel Corporation
 *   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301
 */

#ifndef GST_VAAPI_UTILS_EGL_H
#define GST_VAAPI_UTILS_EGL_H

#include <gmodule.h>
#include <gst/gstinfo.h>
#include <gst/video/video-format.h>
#include "egl_compat.h"
#include "gstvaapiminiobject.h"

typedef union egl_handle_s                      EglHandle;
typedef struct egl_object_s                     EglObject;
typedef struct egl_object_class_s               EglObjectClass;
typedef struct egl_vtable_s                     EglVTable;
typedef struct egl_display_s                    EglDisplay;
typedef struct egl_config_s                     EglConfig;
typedef struct egl_context_state_s              EglContextState;
typedef struct egl_context_s                    EglContext;
typedef struct egl_surface_s                    EglSurface;
typedef struct egl_program_s                    EglProgram;
typedef struct egl_window_s                     EglWindow;

#define EGL_PROTO_BEGIN(NAME, TYPE, EXTENSION) \
  typedef TYPE (*GL_PROTO_GEN_CONCAT3(Egl,NAME,Proc))
#define EGL_PROTO_END()                         ;
#define GL_PROTO_BEGIN(NAME, TYPE, EXTENSION) \
  typedef TYPE (*GL_PROTO_GEN_CONCAT3(Gl,NAME,Proc))
#define GL_PROTO_ARG_LIST(...)                  (__VA_ARGS__)
#define GL_PROTO_ARG(NAME, TYPE)                TYPE NAME
#define GL_PROTO_END()                          ;
#include "egl_vtable.h"

enum {
  EGL_PLATFORM_UNKNOWN,
  EGL_PLATFORM_X11,
  EGL_PLATFORM_WAYLAND,
};

union egl_handle_s
{
  gpointer p;
  guintptr u;
  gintptr i;
};

struct egl_object_s
{
  /*< private >*/
  GstVaapiMiniObject parent_instance;

  EglHandle handle;
  guint is_wrapped:1;
  guint is_valid:1;
};

struct egl_object_class_s
{
  /*< private >*/
  GstVaapiMiniObjectClass parent_class;
};

struct egl_vtable_s
{
  EglObject base;

  gchar **egl_extensions;
  guint num_egl_symbols;
  gchar **gl_extensions;
  guint num_gl_symbols;
  guint gles_version;

#define EGL_PROTO_BEGIN(NAME, TYPE, EXTENSION) \
  GL_PROTO_BEGIN_I(NAME, TYPE, EXTENSION, Egl, egl)
#define GL_PROTO_BEGIN(NAME, TYPE, EXTENSION) \
  GL_PROTO_BEGIN_I(NAME, TYPE, EXTENSION,  Gl,  gl)
#define GL_PROTO_BEGIN_I(NAME, TYPE, EXTENSION, Prefix, prefix) \
  GL_PROTO_GEN_CONCAT3(Prefix,NAME,Proc) GL_PROTO_GEN_CONCAT(prefix,NAME);
#include "egl_vtable.h"

#define EGL_DEFINE_EXTENSION(EXTENSION) \
  GL_DEFINE_EXTENSION_I(EXTENSION, EGL)
#define GL_DEFINE_EXTENSION(EXTENSION) \
  GL_DEFINE_EXTENSION_I(EXTENSION,  GL)
#define GL_DEFINE_EXTENSION_I(EXTENSION, PREFIX) \
  guint GL_PROTO_GEN_CONCAT4(has_,PREFIX,_,EXTENSION);
#include "egl_vtable.h"
};

struct egl_display_s
{
  EglObject base;

  gchar *gl_vendor_string;
  gchar *gl_version_string;
  gchar *gl_apis_string;
  guint gl_apis;                /* EGL_*_BIT mask */
  guint gl_platform;

  GMutex mutex;
  GThread *gl_thread;
  GCond gl_thread_ready;
  gboolean gl_thread_cancel;
  GAsyncQueue *gl_queue;
  gboolean created;
};

struct egl_config_s
{
  EglObject base;

  EglDisplay *display;
  guint gl_api;                 /* EGL_*_API value */
  guint gles_version;
  gint config_id;
  gint visual_id;
};

typedef void (*EglContextRunFunc) (gpointer args);

struct egl_context_state_s
{
  EGLDisplay display;
  EGLContext context;
  EGLSurface read_surface;
  EGLSurface draw_surface;
};

struct egl_context_s
{
  EglObject base;

  EglVTable *vtable;
  EglDisplay *display;
  EglConfig *config;
  EglSurface *read_surface;
  EglSurface *draw_surface;
};

struct egl_surface_s
{
  EglObject base;

  EglDisplay *display;
};

/* Defined to the maximum number of uniforms for a shader program */
#define EGL_MAX_UNIFORMS 16

struct egl_program_s
{
  EglObject base;

  EglVTable *vtable;
  guint frag_shader;
  guint vert_shader;
  gint uniforms[EGL_MAX_UNIFORMS];
};

struct egl_window_s
{
  EglObject base;

  EglContext *context;
  EglSurface *surface;
};

#define egl_object_ref(obj) \
  ((gpointer)gst_vaapi_mini_object_ref ((GstVaapiMiniObject *)(obj)))
#define egl_object_unref(obj) \
  gst_vaapi_mini_object_unref ((GstVaapiMiniObject *)(obj))
#define egl_object_replace(old_obj_ptr, new_obj) \
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **)(old_obj_ptr), \
      (GstVaapiMiniObject *)(new_obj))

G_GNUC_INTERNAL
EglDisplay *
egl_display_new (gpointer native_display, guint gl_platform);

G_GNUC_INTERNAL
EglDisplay *
egl_display_new_wrapped (EGLDisplay gl_display);

G_GNUC_INTERNAL
EglConfig *
egl_config_new (EglDisplay * display, guint gles_version,
    GstVideoFormat format);

G_GNUC_INTERNAL
EglConfig *
egl_config_new_with_attribs (EglDisplay * display, const EGLint * attribs);

G_GNUC_INTERNAL
EglContext *
egl_context_new (EglDisplay * display, EglConfig * config, EglContext * parent);

G_GNUC_INTERNAL
EglContext *
egl_context_new_wrapped (EglDisplay * display, EGLContext gl_context);

G_GNUC_INTERNAL
EglVTable *
egl_context_get_vtable (EglContext * ctx, gboolean need_gl_symbols);

G_GNUC_INTERNAL
gboolean
egl_context_set_current (EglContext * ctx, gboolean activate,
    EglContextState * old_cs);

G_GNUC_INTERNAL
gboolean
egl_context_run (EglContext * ctx, EglContextRunFunc func, gpointer args);

G_GNUC_INTERNAL
EglProgram *
egl_program_new (EglContext * ctx, const gchar * frag_shader_text,
    const gchar * vert_shader_text);

G_GNUC_INTERNAL
EglWindow *
egl_window_new (EglContext * ctx, gpointer native_window);

G_GNUC_INTERNAL
guint
egl_create_texture (EglContext * ctx, guint target, guint format,
    guint width, guint height);

G_GNUC_INTERNAL
void
egl_destroy_texture (EglContext * ctx, guint texture);

G_GNUC_INTERNAL
void
egl_matrix_set_identity (gfloat m[16]);

#endif /* GST_VAAPI_UTILS_EGL_H */
