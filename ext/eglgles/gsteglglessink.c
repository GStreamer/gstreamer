/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-eglglessink
 *
 * This is a vout sink using EGL/GLES.
 *
 * <refsect2>
 * <title>Rationale on OpenGL ES version</title>
 * <para>
 * This Sink uses GLESv2
 * </para>
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line with forced slow path rendering</title>
 * <para>
 * The sink will chose a buffer copy-over slow rendering path even
 * if needed EGL/GLES extensions to use a fast rendering path are
 * available.
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink force_rendering_slow=TRUE
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line with internal window creation disabled</title>
 * <para>
 * The sink will wait for a window handle through it's xOverlay interface
 * even if internal window creation is supported by the platform and
 * implemented.
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink can_create_window=FALSE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideosink.h>
#include <gst/interfaces/xoverlay.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "video_platform_wrapper.h"

#include "gsteglglessink.h"

GST_DEBUG_CATEGORY_STATIC (gst_eglglessink_debug);
#define GST_CAT_DEFAULT gst_eglglessink_debug

/* XXX: These should be defined per model someway
 * but the Galaxy Nexus's were taken as a reference
 * for now on:
 */
#define EGLGLESSINK_MAX_FRAME_WIDTH 1280
#define EGLGLESSINK_MAX_FRAME_HEIGHT 720

/* These are only needed for the fast rendering path */
#ifdef EGL_KHR_image
static PFNEGLCREATEIMAGEKHRPROC my_eglCreateImageKHR;
static PFNEGLDESTROYIMAGEKHRPROC my_eglDestroyImageKHR;

#ifdef EGL_KHR_lock_surface
static PFNEGLLOCKSURFACEKHRPROC my_eglLockSurfaceKHR;
static PFNEGLUNLOCKSURFACEKHRPROC my_eglUnlockSurfaceKHR;

static EGLint lock_attribs[] = {
  EGL_MAP_PRESERVE_PIXELS_KHR, EGL_TRUE,
  EGL_LOCK_USAGE_HINT_KHR, EGL_READ_SURFACE_BIT_KHR | EGL_WRITE_SURFACE_BIT_KHR,
  EGL_NONE
};

#ifdef GL_OES_EGL_image
static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC my_glEGLImageTargetTexture2DOES;
#define EGL_FAST_RENDERING_POSSIBLE 1
#endif
#endif
#endif

/* *INDENT-OFF* */
static const char *vert_COPY_prog = {
      "attribute vec3 position;"
      "attribute vec2 texpos;"
      "varying vec2 opos;"
      "void main(void)"
      "{"
      " opos = texpos;"
      " gl_Position = vec4(position, 1.0);"
      "}"
};

static const char *frag_COPY_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "void main(void)"
      "{"
      " vec4 t = texture2D(tex, opos);"
      " gl_FragColor = vec4(t.xyz, 1.0);"
      "}"
};

static const char *frag_REORDER_prog = {
  "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "void main(void)"
      "{"
      " float r, g, b;"
      " r = texture2D(tex, opos).%c;"
      " g = texture2D(tex, opos).%c;"
      " b = texture2D(tex, opos).%c;"
      " gl_FragColor = vec4(r, g, b, 1.0);"
      "}"
};

/* From gst-plugins-gl */
static const char *frag_AYUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D tex;"
      "void main(void) {"
      "  float r,g,b,y,u,v;"
      "  vec2 nxy = opos.xy;"
      "  y=texture2D(tex,nxy).g;"
      "  u=texture2D(tex,nxy).b;"
      "  v=texture2D(tex,nxy).a;"
      "  y=1.1643*(y-0.0625);"
      "  u=u-0.5;"
      "  v=v-0.5;"
      "  r=y+1.5958*v;"
      "  g=y-0.39173*u-0.81290*v;"
      "  b=y+2.017*u;"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

static const char *frag_PLANAR_YUV_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,Utex,Vtex;"
      "void main(void) {"
      "  float r,g,b,y,u,v;"
      "  vec2 nxy = opos.xy;"
      "  y=texture2D(Ytex,nxy).r;"
      "  u=texture2D(Utex,nxy).r;"
      "  v=texture2D(Vtex,nxy).r;"
      "  y=1.1643*(y-0.0625);"
      "  u=u-0.5;"
      "  v=v-0.5;"
      "  r=y+1.5958*v;"
      "  g=y-0.39173*u-0.81290*v;"
      "  b=y+2.017*u;"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

static const char *frag_NV12_NV21_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex,UVtex;"
      "void main(void) {"
      "  float r,g,b,y,u,v;"
      "  vec2 nxy = opos.xy;"
      "  y=texture2D(Ytex,nxy).r;"
      "  u=texture2D(UVtex,nxy).%c;"
      "  v=texture2D(UVtex,nxy).%c;"
      "  y=1.1643*(y-0.0625);"
      "  u=u-0.5;"
      "  v=v-0.5;"
      "  r=y+1.5958*v;"
      "  g=y-0.39173*u-0.81290*v;"
      "  b=y+2.017*u;"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};

static const char *frag_YUY2_UYVY_prog = {
      "precision mediump float;"
      "varying vec2 opos;"
      "uniform sampler2D Ytex, UVtex;"
      "void main(void) {"
      "  float fx, fy, y, u, v, r, g, b;"
      "  fx = opos.x;"
      "  fy = opos.y;"
      "  y = texture2D(Ytex,vec2(fx,fy)).%c;"
      "  u = texture2D(UVtex,vec2(fx,fy)).%c;"
      "  v = texture2D(UVtex,vec2(fx,fy)).%c;"
      "  y=1.1643*(y-0.0625);"
      "  u=u-0.5;"
      "  v=v-0.5;"
      "  r=y+1.5958*v;"
      "  g=y-0.39173*u-0.81290*v;"
      "  b=y+2.017*u;"
      "  gl_FragColor=vec4(r,g,b,1.0);"
      "}"
};


/* *INDENT-ON* */

/* Input capabilities.
 *
 * Note: OpenGL ES Standard does not mandate YUV support.
 */
static GstStaticPadTemplate gst_eglglessink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_YUV
        ("{ AYUV, Y444, I420, YV12, NV12, NV21, YUY2, UYVY, Y42B, Y41B }") ";"
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";" GST_VIDEO_CAPS_RGB_16));

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_SILENT,
  PROP_CREATE_WINDOW,
  PROP_FORCE_ASPECT_RATIO,
  PROP_DEFAULT_HEIGHT,
  PROP_DEFAULT_WIDTH,
  PROP_FORCE_RENDERING_SLOW
};

/* will probably move elsewhere */
static const EGLint eglglessink_RGBA8888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_ALPHA_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

static const EGLint eglglessink_RGB888_attribs[] = {
  EGL_RED_SIZE, 8,
  EGL_GREEN_SIZE, 8,
  EGL_BLUE_SIZE, 8,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

static const EGLint eglglessink_RGB565_attribs[] = {
  EGL_RED_SIZE, 5,
  EGL_GREEN_SIZE, 6,
  EGL_BLUE_SIZE, 5,
  EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  EGL_NONE
};

static void gst_eglglessink_finalize (GObject * object);
static void gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_eglglessink_change_state (GstElement * element,
    GstStateChange transition);
static GstFlowReturn gst_eglglessink_show_frame (GstVideoSink * vsink,
    GstBuffer * buf);
static gboolean gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static gboolean gst_eglglessink_start (GstBaseSink * sink);
static gboolean gst_eglglessink_stop (GstBaseSink * sink);
static GstFlowReturn gst_eglglessink_buffer_alloc (GstBaseSink * sink,
    guint64 offset, guint size, GstCaps * caps, GstBuffer ** buf);
static GstCaps *gst_eglglessink_getcaps (GstBaseSink * bsink);

/* XOverlay interface cruft */
static gboolean gst_eglglessink_interface_supported
    (GstImplementsInterface * iface, GType type);
static void gst_eglglessink_implements_init
    (GstImplementsInterfaceClass * klass);
static void gst_eglglessink_xoverlay_init (GstXOverlayClass * iface);
static void gst_eglglessink_init_interfaces (GType type);

/* Actual XOverlay interface funcs */
static void gst_eglglessink_expose (GstXOverlay * overlay);
static void gst_eglglessink_set_window_handle (GstXOverlay * overlay,
    guintptr id);
static void gst_eglglessink_set_render_rectangle (GstXOverlay * overlay, gint x,
    gint y, gint width, gint height);

/* Custom Buffer funcs */
static void gst_eglglesbuffer_destroy (GstEglGlesBuffer * eglglessink);
static void gst_eglglesbuffer_init (GstEglGlesBuffer * eglglessink,
    gpointer g_class);
static GType gst_eglglesbuffer_get_type (void);
static GstEglGlesImageFmt *gst_eglglessink_get_compat_format_from_caps
    (GstEglGlesSink * eglglessink, GstCaps * caps);
static void gst_eglglesbuffer_finalize (GstEglGlesBuffer * eglglessink);
static void gst_eglglesbuffer_class_init (gpointer g_class,
    gpointer class_data);
static void gst_eglglesbuffer_free (GstEglGlesBuffer * eglglesbuffer);
static GstEglGlesBuffer *gst_eglglesbuffer_new (GstEglGlesSink * eglglessink,
    GstCaps * caps);
static EGLint *gst_eglglesbuffer_create_native (EGLNativeWindowType win,
    EGLConfig config, EGLNativeDisplayType display, const EGLint * egl_attribs);

/* Utility */
static EGLNativeWindowType gst_eglglessink_create_window (GstEglGlesSink *
    eglglessink, gint width, gint height);
static inline gint
gst_eglglessink_fill_supported_fbuffer_configs (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_init_egl_display (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_choose_config (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_init_egl_surface (GstEglGlesSink * eglglessink);
static void gst_eglglessink_init_egl_exts (GstEglGlesSink * eglglessink);
static gboolean gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink,
    gboolean reset);
static GstFlowReturn gst_eglglessink_render_and_display (GstEglGlesSink * sink,
    GstBuffer * buf);
static inline gboolean got_gl_error (const char *wtf);
static inline void show_egl_error (const char *wtf);
static void gst_eglglessink_wipe_fmt (gpointer data);
static inline gboolean egl_init (GstEglGlesSink * eglglessink);

static GstBufferClass *gsteglglessink_buffer_parent_class = NULL;
#define GST_TYPE_EGLGLESBUFFER (gst_eglglesbuffer_get_type())
#define GST_IS_EGLGLESBUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_EGLGLESBUFFER))
#define GST_EGLGLESBUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_EGLGLESBUFFER, GstEglGlesBuffer))
#define GST_EGLGLESBUFFER_CAST(obj) ((GstEglGlesBuffer *)(obj))


GST_BOILERPLATE_FULL (GstEglGlesSink, gst_eglglessink, GstVideoSink,
    GST_TYPE_VIDEO_SINK, gst_eglglessink_init_interfaces);

/* Custom Buffer Funcs */
/* XXX: Drafted implementation */
static EGLint *
gst_eglglesbuffer_create_native (EGLNativeWindowType win,
    EGLConfig config, EGLNativeDisplayType display, const EGLint * egl_attribs)
{
  EGLNativePixmapType pix = 0;
  EGLSurface pix_surface;
  EGLint *buffer = NULL;

  /* XXX: Need to figure out how to create an egl_native_pixmap_t to
   * feed to eglCreatePixmapSurface. An option on android: create an
   * android_native_buffer_t to pass straight to eglCreateImageKHR.
   */

  pix_surface = eglCreatePixmapSurface (display, config, pix, egl_attribs);

  if (pix_surface == EGL_NO_SURFACE) {
    show_egl_error ("eglCreatePixmapSurface");
    GST_CAT_ERROR (GST_CAT_DEFAULT, "Unable to create pixmap surface");
    goto EGL_ERROR;
  }

  if (my_eglLockSurfaceKHR (display, pix_surface, lock_attribs) == EGL_FALSE) {
    show_egl_error ("eglLockSurfaceKHR");
    GST_CAT_ERROR (GST_CAT_DEFAULT, "Unable to lock surface");
    goto EGL_ERROR;
  }

  if (eglQuerySurface (display, pix_surface, EGL_BITMAP_POINTER_KHR, buffer)
      == EGL_FALSE) {
    show_egl_error ("eglQuerySurface");
    GST_CAT_ERROR (GST_CAT_DEFAULT,
        "Unable to query surface for bitmap pointer");
    goto EGL_ERROR_LOCKED;
  }

  return buffer;

EGL_ERROR_LOCKED:
  my_eglUnlockSurfaceKHR (display, pix_surface);
EGL_ERROR:
  GST_CAT_ERROR (GST_CAT_DEFAULT, "EGL call returned error %x", eglGetError ());
  if (!eglDestroySurface (display, pix_surface)) {
    show_egl_error ("eglDestroySurface");
    GST_CAT_ERROR (GST_CAT_DEFAULT, "Couldn't destroy surface");
  }
  return NULL;
}

static GstEglGlesBuffer *
gst_eglglesbuffer_new (GstEglGlesSink * eglglessink, GstCaps * caps)
{
  GstEglGlesBuffer *eglglesbuffer = NULL;
  GstStructure *structure = NULL;
  GstEglGlesImageFmt *format;

  g_return_val_if_fail (GST_IS_EGLGLESSINK (eglglessink), NULL);
  g_return_val_if_fail (caps, NULL);

  eglglesbuffer =
      (GstEglGlesBuffer *) gst_mini_object_new (GST_TYPE_EGLGLESBUFFER);
  GST_DEBUG_OBJECT (eglglesbuffer, "Creating new GstEglGlesBuffer");

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "width", &eglglesbuffer->width) ||
      !gst_structure_get_int (structure, "height", &eglglesbuffer->height)) {
    GST_WARNING ("Failed getting geometry from caps %" GST_PTR_FORMAT, caps);
  }

  GST_LOG_OBJECT (eglglessink, "creating %dx%d", eglglesbuffer->width,
      eglglesbuffer->height);

  format = gst_eglglessink_get_compat_format_from_caps (eglglessink, caps);

  if (!format) {
    GST_WARNING_OBJECT (eglglessink,
        "Failed to get format from caps %" GST_PTR_FORMAT, caps);
    GST_ERROR_OBJECT (eglglessink,
        "Invalid input caps. Failed to create  %dx%d buffer",
        eglglesbuffer->width, eglglesbuffer->height);
    goto BEACH_UNLOCKED;
  }

  eglglesbuffer->format = format->fmt;
  eglglesbuffer->eglglessink = gst_object_ref (eglglessink);

  eglglesbuffer->image = gst_eglglesbuffer_create_native
      (eglglessink->window, eglglessink->config, eglglessink->display, NULL);
  if (!eglglesbuffer->image) {
    GST_ERROR_OBJECT (eglglessink,
        "Failed to create native %dx%d image buffer", eglglesbuffer->width,
        eglglesbuffer->height);
    goto BEACH_UNLOCKED;
  }

  GST_BUFFER_DATA (eglglesbuffer) = (guchar *) eglglesbuffer->image;
  GST_BUFFER_SIZE (eglglesbuffer) = eglglesbuffer->size;

  return eglglesbuffer;

BEACH_UNLOCKED:
  gst_eglglesbuffer_free (eglglesbuffer);
  eglglesbuffer = NULL;
  return NULL;
}

static void
gst_eglglesbuffer_destroy (GstEglGlesBuffer * eglglesbuffer)
{

  GstEglGlesSink *eglglessink;

  GST_DEBUG_OBJECT (eglglesbuffer, "Destroying buffer");

  eglglessink = eglglesbuffer->eglglessink;
  if (G_UNLIKELY (eglglessink == NULL))
    goto NO_SINK;

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));

  GST_OBJECT_LOCK (eglglessink);
  GST_DEBUG_OBJECT (eglglessink, "Destroying image");

  if (eglglesbuffer->image) {
    if (GST_BUFFER_DATA (eglglesbuffer)) {
      g_free (GST_BUFFER_DATA (eglglesbuffer));
    }
    eglglesbuffer->image = NULL;
    /* XXX: Unallocate EGL/GL especific resources asociated with this
     * Image here
     */
  }

  GST_OBJECT_UNLOCK (eglglessink);
  eglglesbuffer->eglglessink = NULL;
  gst_object_unref (eglglessink);

  GST_MINI_OBJECT_CLASS (gsteglglessink_buffer_parent_class)->finalize
      (GST_MINI_OBJECT (eglglesbuffer));

  return;

NO_SINK:
  GST_WARNING ("No sink found");
  return;
}

/* XXX: Missing implementation.
 * This function will have the code for maintaing the pool. readding or
 * destroying the buffers on size or runing/status change. Right now all
 * it does is to call _destroy.
 * for a proper implementation take a look at xvimagesink's image buffer
 * destroy func.
 */
static void
gst_eglglesbuffer_finalize (GstEglGlesBuffer * eglglesbuffer)
{
  GstEglGlesSink *eglglessink;

  eglglessink = eglglesbuffer->eglglessink;
  if (G_UNLIKELY (eglglessink == NULL))
    goto NO_SINK;

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));

  gst_eglglesbuffer_destroy (eglglesbuffer);

  return;

NO_SINK:
  GST_WARNING ("No sink found");
  return;
}

static void
gst_eglglesbuffer_free (GstEglGlesBuffer * eglglesbuffer)
{
  /* Make sure it is not recycled. This is meaningless without
   * a pool but was left here as a reference
   */
  eglglesbuffer->width = -1;
  eglglesbuffer->height = -1;
  gst_buffer_unref (GST_BUFFER (eglglesbuffer));
}

static void
gst_eglglesbuffer_init (GstEglGlesBuffer * eglglesbuffer, gpointer g_class)
{
  eglglesbuffer->width = 0;
  eglglesbuffer->height = 0;
  eglglesbuffer->size = 0;
  eglglesbuffer->image = NULL;
  eglglesbuffer->format = GST_EGLGLESSINK_IMAGE_NOFMT;
}

static void
gst_eglglesbuffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gsteglglessink_buffer_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_eglglesbuffer_finalize;
}

static GType
gst_eglglesbuffer_get_type (void)
{
  static GType _gst_eglglessink_buffer_type;

  if (G_UNLIKELY (_gst_eglglessink_buffer_type == 0)) {
    static const GTypeInfo eglglessink_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_eglglesbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstEglGlesBuffer),
      0,
      (GInstanceInitFunc) gst_eglglesbuffer_init,
      NULL
    };
    _gst_eglglessink_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstEglGlesBuffer", &eglglessink_buffer_info, 0);
  }
  return _gst_eglglessink_buffer_type;
}

static GstEglGlesImageFmt *
gst_eglglessink_get_compat_format_from_caps (GstEglGlesSink * eglglessink,
    GstCaps * caps)
{

  GList *list;
  GstEglGlesImageFmt *format;

  g_return_val_if_fail (GST_IS_EGLGLESSINK (eglglessink), 0);

  list = eglglessink->supported_fmts;

  /* Traverse the list trying to find a compatible format */
  while (list) {
    format = list->data;
    GST_DEBUG_OBJECT (eglglessink, "Checking compatibility between listed %"
        GST_PTR_FORMAT " and %" GST_PTR_FORMAT, format->caps, caps);
    if (format) {
      if (gst_caps_can_intersect (caps, format->caps)) {
        GST_INFO_OBJECT (eglglessink, "Found compatible format %d",
            format->fmt);
        GST_DEBUG_OBJECT (eglglessink,
            "Got caps %" GST_PTR_FORMAT " and this format can do %"
            GST_PTR_FORMAT, caps, format->caps);
        return format;
      }
    }
    list = g_list_next (list);
  }

  return NULL;
}

static GstCaps *
gst_eglglessink_different_size_suggestion (GstEglGlesSink * eglglessink,
    GstCaps * caps)
{
  GstCaps *intersection;
  GstCaps *new_caps;
  GstStructure *s;
  gint width, height;
  gint par_n = 1, par_d = 1;
  gint dar_n, dar_d;
  gint w, h;

  new_caps = gst_caps_copy (caps);

  s = gst_caps_get_structure (new_caps, 0);

  gst_structure_get_int (s, "width", &width);
  gst_structure_get_int (s, "height", &height);
  gst_structure_get_fraction (s, "pixel-aspect-ratio", &par_n, &par_d);

  gst_structure_remove_field (s, "width");
  gst_structure_remove_field (s, "height");
  gst_structure_remove_field (s, "pixel-aspect-ratio");

  intersection = gst_caps_intersect (eglglessink->current_caps, new_caps);
  gst_caps_unref (new_caps);

  if (gst_caps_is_empty (intersection))
    return intersection;

  s = gst_caps_get_structure (intersection, 0);

  gst_util_fraction_multiply (width, height, par_n, par_d, &dar_n, &dar_d);

  /* XXX: xvimagesink supports all PARs not sure about our eglglessink
   * though, need to review this afterwards.
   */

  gst_structure_fixate_field_nearest_int (s, "width", width);
  gst_structure_fixate_field_nearest_int (s, "height", height);
  gst_structure_get_int (s, "width", &w);
  gst_structure_get_int (s, "height", &h);

  gst_util_fraction_multiply (h, w, dar_n, dar_d, &par_n, &par_d);
  gst_structure_set (s, "pixel-aspect-ratio", GST_TYPE_FRACTION, par_n, par_d,
      NULL);

  return intersection;
}

static GstFlowReturn
gst_eglglessink_buffer_alloc (GstBaseSink * bsink, guint64 offset,
    guint size, GstCaps * caps, GstBuffer ** buf)
{

  GstEglGlesSink *eglglessink;
  GstFlowReturn ret = GST_FLOW_OK;
  GstEglGlesBuffer *eglglesbuffer = NULL;
  GstCaps *intersection = NULL;
  GstStructure *structure = NULL;
  gint width, height;
  GstEglGlesImageFmt *format;

  eglglessink = GST_EGLGLESSINK (bsink);

  /* No custom alloc for the slow rendering path */
  if (eglglessink->rendering_path == GST_EGLGLESSINK_RENDER_SLOW) {
    GST_INFO_OBJECT (eglglessink, "No custom alloc for slow rendering path");
    *buf = NULL;
    return GST_FLOW_OK;
  }

  if (G_UNLIKELY (!caps))
    goto NO_CAPS;

  if (G_LIKELY (gst_caps_is_equal (caps, eglglessink->current_caps))) {
    GST_LOG_OBJECT (eglglessink,
        "Buffer alloc for same last_caps, reusing caps");
    intersection = gst_caps_ref (caps);
    width = GST_VIDEO_SINK_WIDTH (eglglessink);
    height = GST_VIDEO_SINK_HEIGHT (eglglessink);

    goto REUSE_LAST_CAPS;
  }

  GST_DEBUG_OBJECT (eglglessink, "Buffer alloc requested size %d with caps %"
      GST_PTR_FORMAT ", intersecting with our caps %" GST_PTR_FORMAT, size,
      caps, eglglessink->current_caps);

  /* Check the caps against our current caps */
  intersection = gst_caps_intersect (eglglessink->current_caps, caps);

  GST_DEBUG_OBJECT (eglglessink, "Intersection in buffer alloc returned %"
      GST_PTR_FORMAT, intersection);

  if (gst_caps_is_empty (intersection)) {
    GstCaps *new_caps;

    gst_caps_unref (intersection);

    /* So we don't support this kind of buffer, let's define one we'd like */
    new_caps = gst_caps_copy (caps);

    structure = gst_caps_get_structure (new_caps, 0);
    if (!gst_structure_has_field (structure, "width") ||
        !gst_structure_has_field (structure, "height")) {
      gst_caps_unref (new_caps);
      goto INVALID;
    }

    /* Try different dimensions */
    intersection =
        gst_eglglessink_different_size_suggestion (eglglessink, new_caps);

    /* YUV not implemented yet */
    if (gst_caps_is_empty (intersection)) {

      gst_structure_set_name (structure, "video/x-raw-rgb");

      /* Remove format specific fields */
      gst_structure_remove_field (structure, "format");
      gst_structure_remove_field (structure, "endianness");
      gst_structure_remove_field (structure, "depth");
      gst_structure_remove_field (structure, "bpp");
      gst_structure_remove_field (structure, "red_mask");
      gst_structure_remove_field (structure, "green_mask");
      gst_structure_remove_field (structure, "blue_mask");
      gst_structure_remove_field (structure, "alpha_mask");

      /* Reuse intersection with current_caps */
      intersection = gst_caps_intersect (eglglessink->current_caps, new_caps);
    }

    /* Try with different dimensions and RGB formats */
    if (gst_caps_is_empty (intersection))
      intersection =
          gst_eglglessink_different_size_suggestion (eglglessink, new_caps);

    /* Clean this copy */
    gst_caps_unref (new_caps);

    if (gst_caps_is_empty (intersection))
      goto INCOMPATIBLE;
  }

  /* Ensure the returned caps are fixed */
  gst_caps_truncate (intersection);

  GST_DEBUG_OBJECT (eglglessink, "Allocating a buffer with caps %"
      GST_PTR_FORMAT, intersection);
  if (gst_caps_is_equal (intersection, caps)) {
    /* Things work better if we return a buffer with the same caps ptr
     * as was asked for when we can */
    gst_caps_replace (&intersection, caps);
  }

  /* Get image format from caps */
  format = gst_eglglessink_get_compat_format_from_caps (eglglessink,
      intersection);

  if (!format)
    GST_WARNING_OBJECT (eglglessink, "Can't get a compatible format from caps");

  /* Get geometry from caps */
  structure = gst_caps_get_structure (intersection, 0);
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height) || !format)
    goto INVALID_CAPS;

REUSE_LAST_CAPS:

  GST_DEBUG_OBJECT (eglglessink, "Creating eglglesbuffer");
  eglglesbuffer = gst_eglglesbuffer_new (eglglessink, intersection);

  if (eglglesbuffer) {
    /* Make sure the buffer is cleared of any previously used flags */
    GST_MINI_OBJECT_CAST (eglglesbuffer)->flags = 0;
    gst_buffer_set_caps (GST_BUFFER_CAST (eglglesbuffer), intersection);
  }

  *buf = GST_BUFFER_CAST (eglglesbuffer);

BEACH:
  if (intersection) {
    gst_caps_unref (intersection);
  }

  return ret;

  /* ERRORS */
INVALID:
  {
    GST_DEBUG_OBJECT (eglglessink, "No width/height on caps!?");
    ret = GST_FLOW_WRONG_STATE;
    goto BEACH;
  }
INCOMPATIBLE:
  {
    GST_WARNING_OBJECT (eglglessink, "We were requested a buffer with "
        "caps %" GST_PTR_FORMAT ", but our current caps %" GST_PTR_FORMAT
        " are completely incompatible!", caps, eglglessink->current_caps);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto BEACH;
  }
INVALID_CAPS:
  {
    GST_WARNING_OBJECT (eglglessink, "Invalid caps for buffer allocation %"
        GST_PTR_FORMAT, intersection);
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto BEACH;
  }
NO_CAPS:
  {
    GST_WARNING_OBJECT (eglglessink, "Have no caps, doing fallback allocation");
    *buf = NULL;
    ret = GST_FLOW_OK;
    goto BEACH;
  }
}

static inline gint
gst_eglglessink_fill_supported_fbuffer_configs (GstEglGlesSink * eglglessink)
{
  gint ret = 0;
  EGLint cfg_number;
  GstEglGlesImageFmt *format;
  GstCaps *caps;

  GST_DEBUG_OBJECT (eglglessink,
      "Building initial list of wanted eglattribs per format");

  /* Init supported format/caps list */
  g_mutex_lock (eglglessink->flow_lock);

  caps = gst_caps_new_empty ();

  if (eglChooseConfig (eglglessink->display, eglglessink_RGBA8888_attribs,
          NULL, 1, &cfg_number) != EGL_FALSE) {
    format = g_new0 (GstEglGlesImageFmt, 1);
    format->fmt = GST_EGLGLESSINK_IMAGE_RGBA8888;
    format->attribs = eglglessink_RGBA8888_attribs;
    format->caps = gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBA);
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRA));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ARGB));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_ABGR));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGBx));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGRx));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xRGB));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_xBGR));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_AYUV));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y444));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_I420));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YV12));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV12));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_NV21));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_YUY2));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_UYVY));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y42B));
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_Y41B));
    eglglessink->supported_fmts =
        g_list_append (eglglessink->supported_fmts, format);
    ret++;
    gst_caps_append (caps, gst_caps_ref (format->caps));
  } else {
    GST_INFO_OBJECT (eglglessink,
        "EGL display doesn't support RGBA8888 config");
  }

  if (eglChooseConfig (eglglessink->display, eglglessink_RGB888_attribs,
          NULL, 1, &cfg_number) != EGL_FALSE) {
    format = g_new0 (GstEglGlesImageFmt, 1);
    format->fmt = GST_EGLGLESSINK_IMAGE_RGB888;
    format->attribs = eglglessink_RGB888_attribs;
    format->caps = gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB);
    gst_caps_append (format->caps,
        gst_video_format_new_template_caps (GST_VIDEO_FORMAT_BGR));
    eglglessink->supported_fmts =
        g_list_append (eglglessink->supported_fmts, format);
    ret++;
    gst_caps_append (caps, gst_caps_ref (format->caps));
  } else {
    GST_INFO_OBJECT (eglglessink, "EGL display doesn't support RGB888 config");
  }

  if (eglChooseConfig (eglglessink->display, eglglessink_RGB565_attribs,
          NULL, 1, &cfg_number) != EGL_FALSE) {
    format = g_new0 (GstEglGlesImageFmt, 1);
    format->fmt = GST_EGLGLESSINK_IMAGE_RGB565;
    format->attribs = eglglessink_RGB565_attribs;
    format->caps = gst_video_format_new_template_caps (GST_VIDEO_FORMAT_RGB16);
    eglglessink->supported_fmts =
        g_list_append (eglglessink->supported_fmts, format);
    ret++;
    gst_caps_append (caps, gst_caps_ref (format->caps));
  } else {
    GST_INFO_OBJECT (eglglessink, "EGL display doesn't support RGB565 config");
  }

  gst_caps_replace (&eglglessink->sinkcaps, caps);
  gst_caps_unref (caps);

  g_mutex_unlock (eglglessink->flow_lock);

  return ret;
}

static inline gboolean
egl_init (GstEglGlesSink * eglglessink)
{
  if (!platform_wrapper_init ()) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL platform wrapper");
    goto HANDLE_ERROR;
  }

  if (!gst_eglglessink_init_egl_display (eglglessink)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL display");
    goto HANDLE_ERROR;
  }

  gst_eglglessink_init_egl_exts (eglglessink);

  if (!gst_eglglessink_fill_supported_fbuffer_configs (eglglessink)) {
    GST_ERROR_OBJECT (eglglessink, "Display support NONE of our configs");
    goto HANDLE_ERROR;
  }

  g_mutex_lock (eglglessink->flow_lock);
  eglglessink->egl_started = TRUE;
  g_mutex_unlock (eglglessink->flow_lock);

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Failed to perform EGL init");
  return FALSE;
}

gboolean
gst_eglglessink_start (GstBaseSink * sink)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (sink);

  if (!eglglessink->egl_started) {
    GST_ERROR_OBJECT (eglglessink, "EGL uninitialized. Bailing out");
    goto HANDLE_ERROR;
  }

  /* Ask for a window to render to */
  if (!eglglessink->have_window)
    gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (eglglessink));

  if (!eglglessink->have_window && !eglglessink->can_create_window) {
    GST_ERROR_OBJECT (eglglessink, "Window handle unavailable and we "
        "were instructed not to create an internal one. Bailing out.");
    goto HANDLE_ERROR;
  }

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't start");
  return FALSE;
}

/* Drafted */
gboolean
gst_eglglessink_stop (GstBaseSink * sink)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (sink);

  /* EGL/GLES2 cleanup */

  if (eglglessink->rendering_path == GST_EGLGLESSINK_RENDER_SLOW) {
    glDeleteBuffers (1, &eglglessink->vdata);
    glDeleteBuffers (1, &eglglessink->tdata);
    glDeleteBuffers (1, &eglglessink->idata);
    eglglessink->have_vbo = FALSE;

    glDeleteShader (eglglessink->fragshader);
    glDeleteShader (eglglessink->vertshader);

    glDeleteTextures (eglglessink->n_textures, eglglessink->texture);
    eglglessink->have_texture = FALSE;
    eglglessink->n_textures = 0;

    glDeleteProgram (eglglessink->program);
  }

  if (eglglessink->surface) {
    eglDestroySurface (eglglessink->display, eglglessink->surface);
    eglglessink->surface = NULL;
    eglglessink->have_surface = FALSE;
  }

  if (eglglessink->context) {
    eglDestroyContext (eglglessink->display, eglglessink->context);
    eglglessink->context = NULL;
  }

  if (eglglessink->using_own_window) {
    platform_destroy_native_window (eglglessink->display, eglglessink->window);
    eglglessink->window = NULL;
    eglglessink->have_window = FALSE;
  }

  return TRUE;
}

static void
gst_eglglessink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_window_handle = gst_eglglessink_set_window_handle;
  iface->expose = gst_eglglessink_expose;
  iface->set_render_rectangle = gst_eglglessink_set_render_rectangle;
}

static gboolean
gst_eglglessink_interface_supported (GstImplementsInterface * iface, GType type)
{
  return (type == GST_TYPE_X_OVERLAY);
}

static void
gst_eglglessink_implements_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_eglglessink_interface_supported;
}

static inline gboolean
got_gl_error (const char *wtf)
{
  GLuint error = GL_NO_ERROR;

  if ((error = glGetError ()) != GL_NO_ERROR) {
    GST_CAT_ERROR (GST_CAT_DEFAULT, "GL ERROR: %s returned %x", wtf, error);
    return TRUE;
  }
  return FALSE;
}

static inline void
show_egl_error (const char *wtf)
{
  EGLint error;

  if ((error = eglGetError ()) != EGL_SUCCESS)
    GST_CAT_DEBUG (GST_CAT_DEFAULT, "EGL ERROR: %s returned %x", wtf, error);
}

static EGLNativeWindowType
gst_eglglessink_create_window (GstEglGlesSink * eglglessink, gint width,
    gint height)
{
  EGLNativeWindowType window = 0;

  if (!eglglessink->can_create_window) {
    GST_ERROR_OBJECT (eglglessink, "This sink can't create a window by itself");
    return window;
  } else
    GST_INFO_OBJECT (eglglessink, "Attempting internal window creation");

  if (!width && !height) {      /* Create a default size window */
    width = eglglessink->window_default_width;
    height = eglglessink->window_default_height;
  }

  window = platform_create_native_window (width, height);
  if (!window) {
    GST_ERROR_OBJECT (eglglessink, "Could not create window");
    return window;
  }
  gst_x_overlay_got_window_handle (GST_X_OVERLAY (eglglessink),
      (guintptr) window);
  return window;
}

/* XXX: Should implement (redisplay)
 * We need at least the last buffer stored for this to work
 */
static void
gst_eglglessink_expose (GstXOverlay * overlay)
{
  GstEglGlesSink *eglglessink;
  GstFlowReturn ret;

  eglglessink = GST_EGLGLESSINK (overlay);
  GST_DEBUG_OBJECT (eglglessink, "Expose catched, redisplay");

  /* Logic would be to get _render_and_display() to use
   * last seen buffer to render from when NULL it's
   * passed on */
  GST_WARNING_OBJECT (eglglessink, "_expose() not implemented");
  ret = gst_eglglessink_render_and_display (eglglessink, NULL);
  if (ret == GST_FLOW_ERROR)
    GST_ERROR_OBJECT (eglglessink, "Redisplay failed");
}

/* Checks available egl/gles extensions and chooses
 * a suitable rendering path from GstEglGlesSinkRenderingPath
 * accordingly. This function can only be called after an
 * EGL context has been made current.
 */
static void
gst_eglglessink_init_egl_exts (GstEglGlesSink * eglglessink)
{
  const char *eglexts;
  unsigned const char *glexts;

  eglexts = eglQueryString (eglglessink->display, EGL_EXTENSIONS);
  glexts = glGetString (GL_EXTENSIONS);

  GST_DEBUG_OBJECT (eglglessink, "Available EGL extensions: %s\n", eglexts);
  GST_DEBUG_OBJECT (eglglessink, "Available GLES extensions: %s\n", glexts);

#ifdef EGL_FAST_RENDERING_POSSIBLE
  /* OK Fast rendering should be possible from the declared
   * extensions on the eglexts/glexts.h headers
   */

  /* Check for support from claimed EGL/GLES extensions */

  if (!strstr (eglexts, "EGL_KHR_image"))
    goto KHR_IMAGE_NA;
  if (!strstr (eglexts, "EGL_KHR_lock_surface"))
    goto SURFACE_LOCK_NA;
  if (!strstr ((char *) glexts, "GL_OES_EGL_image"))
    goto TEXTURE_2DOES_NA;

  /* Check for actual extension proc addresses */

  my_eglCreateImageKHR =
      (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress ("eglCreateImageKHR");
  my_eglDestroyImageKHR =
      (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress ("eglDestroyImageKHR");

  if (!my_eglCreateImageKHR || !my_eglDestroyImageKHR) {
  KHR_IMAGE_NA:
    GST_INFO_OBJECT (eglglessink, "Extension missing: EGL_KHR_image");
    goto MISSING_EXTS;
  }

  my_eglLockSurfaceKHR =
      (PFNEGLLOCKSURFACEKHRPROC) eglGetProcAddress ("eglLockSurfaceKHR");
  my_eglUnlockSurfaceKHR =
      (PFNEGLUNLOCKSURFACEKHRPROC) eglGetProcAddress ("eglUnlockSurfaceKHR");

  if (!my_eglLockSurfaceKHR || !my_eglUnlockSurfaceKHR) {
  SURFACE_LOCK_NA:
    GST_INFO_OBJECT (eglglessink, "Extension missing: EGL_KHR_lock_surface");
    goto MISSING_EXTS;
  }

  my_glEGLImageTargetTexture2DOES =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress
      ("glEGLImageTargetTexture2DOES");

  if (!my_glEGLImageTargetTexture2DOES) {
  TEXTURE_2DOES_NA:
    GST_INFO_OBJECT (eglglessink, "Extension missing: GL_OES_EGL_image");
    goto MISSING_EXTS;
  }

  if (!eglglessink->force_rendering_slow) {
    GST_INFO_OBJECT (eglglessink,
        "Have needed extensions for fast rendering path");
  } else {
    GST_WARNING_OBJECT (eglglessink,
        "Extension check passed but slow rendering path being forced");
    goto SLOW_PATH_SELECTED;
  }

  /* Extension check passed. Enable fast rendering path */
  eglglessink->rendering_path = GST_EGLGLESSINK_RENDER_FAST;
  GST_INFO_OBJECT (eglglessink, "Using fast rendering path");
  return;
#endif

MISSING_EXTS:
  GST_WARNING_OBJECT (eglglessink,
      "Extensions missing. Can't use fast rendering path");
SLOW_PATH_SELECTED:
  eglglessink->rendering_path = GST_EGLGLESSINK_RENDER_SLOW;
  GST_INFO_OBJECT (eglglessink, "Using slow rendering path");
  return;
}

static gboolean
gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink, gboolean reset)
{

  g_mutex_lock (eglglessink->flow_lock);

  GST_INFO_OBJECT (eglglessink, "VBO setup. have_vbo:%d, should reset %d",
      eglglessink->have_vbo, reset);

  if (eglglessink->have_vbo && reset) {
    glDeleteBuffers (1, &eglglessink->vdata);
    glDeleteBuffers (1, &eglglessink->tdata);
    glDeleteBuffers (1, &eglglessink->idata);
    eglglessink->have_vbo = FALSE;
  }

  if (!eglglessink->have_vbo) {
    GST_DEBUG_OBJECT (eglglessink, "Performing VBO setup");
    eglglessink->coordarray[0].x = 1;
    eglglessink->coordarray[0].y = 1;
    eglglessink->coordarray[0].z = 0;

    eglglessink->coordarray[1].x = 1;
    eglglessink->coordarray[1].y = -1;
    eglglessink->coordarray[1].z = 0;

    eglglessink->coordarray[2].x = -1;
    eglglessink->coordarray[2].y = 1;
    eglglessink->coordarray[2].z = 0;

    eglglessink->coordarray[3].x = -1;
    eglglessink->coordarray[3].y = -1;
    eglglessink->coordarray[3].z = 0;

    eglglessink->texarray[0].x = 1;
    eglglessink->texarray[0].y = 0;

    eglglessink->texarray[1].x = 1;
    eglglessink->texarray[1].y = 1;

    eglglessink->texarray[2].x = 0;
    eglglessink->texarray[2].y = 0;

    eglglessink->texarray[3].x = 0;
    eglglessink->texarray[3].y = 1;

    eglglessink->indexarray[0] = 0;
    eglglessink->indexarray[1] = 1;
    eglglessink->indexarray[2] = 2;
    eglglessink->indexarray[3] = 3;

    glGenBuffers (1, &eglglessink->vdata);
    glGenBuffers (1, &eglglessink->tdata);
    glGenBuffers (1, &eglglessink->idata);
    if (got_gl_error ("glGenBuffers"))
      goto HANDLE_ERROR_LOCKED;

    glBindBuffer (GL_ARRAY_BUFFER, eglglessink->vdata);
    if (got_gl_error ("glBindBuffer vdata"))
      goto HANDLE_ERROR_LOCKED;

    glBufferData (GL_ARRAY_BUFFER, sizeof (eglglessink->coordarray),
        eglglessink->coordarray, GL_STATIC_DRAW);
    if (got_gl_error ("glBufferData vdata"))
      goto HANDLE_ERROR_LOCKED;

    glVertexAttribPointer (eglglessink->coord_pos, 3, GL_FLOAT, GL_FALSE, 0, 0);
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR_LOCKED;

    glEnableVertexAttribArray (eglglessink->coord_pos);
    if (got_gl_error ("glEnableVertexAttribArray"))
      goto HANDLE_ERROR_LOCKED;

    glBindBuffer (GL_ARRAY_BUFFER, eglglessink->tdata);
    if (got_gl_error ("glBindBuffer tdata"))
      goto HANDLE_ERROR_LOCKED;

    glBufferData (GL_ARRAY_BUFFER, sizeof (eglglessink->texarray),
        eglglessink->texarray, GL_STATIC_DRAW);
    if (got_gl_error ("glBufferData tdata"))
      goto HANDLE_ERROR_LOCKED;

    glVertexAttribPointer (eglglessink->tex_pos, 2, GL_FLOAT, GL_FALSE, 0, 0);
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR_LOCKED;

    glEnableVertexAttribArray (eglglessink->tex_pos);
    if (got_gl_error ("glEnableVertexAttribArray"))
      goto HANDLE_ERROR_LOCKED;

    glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, eglglessink->idata);
    if (got_gl_error ("glBindBuffer idata"))
      goto HANDLE_ERROR_LOCKED;

    glBufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (eglglessink->indexarray),
        eglglessink->indexarray, GL_STATIC_DRAW);
    if (got_gl_error ("glBufferData idata"))
      goto HANDLE_ERROR_LOCKED;

    eglglessink->have_vbo = TRUE;
  } else {
    GST_INFO_OBJECT (eglglessink, "Won't perform VBO setup");
  }

  g_mutex_unlock (eglglessink->flow_lock);
  return TRUE;

HANDLE_ERROR_LOCKED:
  g_mutex_unlock (eglglessink->flow_lock);
  GST_ERROR_OBJECT (eglglessink, "Unable to perform VBO setup");
  return FALSE;
}

static gboolean
gst_eglglessink_init_egl_surface (GstEglGlesSink * eglglessink)
{
  GLint test;
  GLboolean ret;
  GLchar *info_log;
  const gchar *texnames[3] = { NULL, };
  gchar *tmp_prog = NULL;

  GST_DEBUG_OBJECT (eglglessink, "Enter EGL surface setup");

  g_mutex_lock (eglglessink->flow_lock);

  eglglessink->surface = eglCreateWindowSurface (eglglessink->display,
      eglglessink->config, eglglessink->window, NULL);

  if (eglglessink->surface == EGL_NO_SURFACE) {
    show_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (eglglessink, "Can't create surface");
    goto HANDLE_EGL_ERROR_LOCKED;
  }

  if (!eglMakeCurrent (eglglessink->display, eglglessink->surface,
          eglglessink->surface, eglglessink->context)) {
    show_egl_error ("eglCreateWindowSurface");
    GST_ERROR_OBJECT (eglglessink, "Couldn't bind surface/context");
    goto HANDLE_EGL_ERROR_LOCKED;
  }

  /* Save surface dims */
  eglQuerySurface (eglglessink->display, eglglessink->surface, EGL_WIDTH,
      &eglglessink->surface_width);
  eglQuerySurface (eglglessink->display, eglglessink->surface, EGL_HEIGHT,
      &eglglessink->surface_height);

  GST_INFO_OBJECT (eglglessink, "Got surface of %dx%d pixels",
      eglglessink->surface_width, eglglessink->surface_height);

  /* We have a surface! */
  eglglessink->have_surface = TRUE;
  g_mutex_unlock (eglglessink->flow_lock);

  /* Init vertex and fragment progs.
   * XXX: Need to be runtime conditional or ifdefed
   */

  /* Shader compiler support it's optional byt we
   * currently rely on it.
   */

  glGetBooleanv (GL_SHADER_COMPILER, &ret);
  if (ret == GL_FALSE) {
    GST_ERROR_OBJECT (eglglessink, "Shader compiler support is unavailable!");
    goto HANDLE_ERROR;
  }

  eglglessink->vertshader = glCreateShader (GL_VERTEX_SHADER);
  GST_DEBUG_OBJECT (eglglessink, "Sending %s to handle %d", vert_COPY_prog,
      eglglessink->vertshader);
  glShaderSource (eglglessink->vertshader, 1, &vert_COPY_prog, NULL);
  if (got_gl_error ("glShaderSource vertex"))
    goto HANDLE_ERROR;

  glCompileShader (eglglessink->vertshader);
  if (got_gl_error ("glCompileShader vertex"))
    goto HANDLE_ERROR;

  glGetShaderiv (eglglessink->vertshader, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (eglglessink, "Successfully compiled vertex shader");
  else {
    GST_ERROR_OBJECT (eglglessink, "Couldn't compile vertex shader");
    glGetShaderiv (eglglessink->vertshader, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (eglglessink->vertshader, test, NULL, info_log);
    GST_INFO_OBJECT (eglglessink, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  eglglessink->fragshader = glCreateShader (GL_FRAGMENT_SHADER);
  switch (eglglessink->format) {
    case GST_VIDEO_FORMAT_AYUV:
      glShaderSource (eglglessink->fragshader, 1, &frag_AYUV_prog, NULL);
      eglglessink->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      glShaderSource (eglglessink->fragshader, 1, &frag_PLANAR_YUV_prog, NULL);
      eglglessink->n_textures = 3;
      texnames[0] = "Ytex";
      texnames[1] = "Utex";
      texnames[2] = "Vtex";
      break;
    case GST_VIDEO_FORMAT_YUY2:
      tmp_prog = g_strdup_printf (frag_YUY2_UYVY_prog, 'r', 'g', 'a');
      glShaderSource (eglglessink->fragshader, 1, (const GLchar **) &tmp_prog,
          NULL);
      eglglessink->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_UYVY:
      tmp_prog = g_strdup_printf (frag_YUY2_UYVY_prog, 'a', 'r', 'b');
      glShaderSource (eglglessink->fragshader, 1, (const GLchar **) &tmp_prog,
          NULL);
      eglglessink->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV12:
      tmp_prog = g_strdup_printf (frag_NV12_NV21_prog, 'r', 'a');
      glShaderSource (eglglessink->fragshader, 1, (const GLchar **) &tmp_prog,
          NULL);
      eglglessink->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_NV21:
      tmp_prog = g_strdup_printf (frag_NV12_NV21_prog, 'a', 'r');
      glShaderSource (eglglessink->fragshader, 1, (const GLchar **) &tmp_prog,
          NULL);
      eglglessink->n_textures = 2;
      texnames[0] = "Ytex";
      texnames[1] = "UVtex";
      break;
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_BGRA:
      tmp_prog = g_strdup_printf (frag_REORDER_prog, 'b', 'g', 'r');
      glShaderSource (eglglessink->fragshader, 1, (const GLchar **) &tmp_prog,
          NULL);
      eglglessink->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_ARGB:
      tmp_prog = g_strdup_printf (frag_REORDER_prog, 'g', 'b', 'a');
      glShaderSource (eglglessink->fragshader, 1, (const GLchar **) &tmp_prog,
          NULL);
      eglglessink->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_ABGR:
      tmp_prog = g_strdup_printf (frag_REORDER_prog, 'a', 'b', 'g');
      glShaderSource (eglglessink->fragshader, 1, (const GLchar **) &tmp_prog,
          NULL);
      eglglessink->n_textures = 1;
      texnames[0] = "tex";
      break;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGB16:
      glShaderSource (eglglessink->fragshader, 1, &frag_COPY_prog, NULL);
      eglglessink->n_textures = 1;
      texnames[0] = "tex";
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (got_gl_error ("glShaderSource fragment"))
    goto HANDLE_ERROR;

  glCompileShader (eglglessink->fragshader);
  if (got_gl_error ("glCompileShader fragment"))
    goto HANDLE_ERROR;

  glGetShaderiv (eglglessink->fragshader, GL_COMPILE_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (eglglessink, "Successfully compiled fragment shader");
  else {
    GST_ERROR_OBJECT (eglglessink, "Couldn't compile fragment shader");
    glGetShaderiv (eglglessink->fragshader, GL_INFO_LOG_LENGTH, &test);
    info_log = g_new0 (GLchar, test);
    glGetShaderInfoLog (eglglessink->fragshader, test, NULL, info_log);
    GST_INFO_OBJECT (eglglessink, "Compilation info log:\n%s", info_log);
    g_free (info_log);
    goto HANDLE_ERROR;
  }

  eglglessink->program = glCreateProgram ();
  if (got_gl_error ("glCreateProgram"))
    goto HANDLE_ERROR;
  glAttachShader (eglglessink->program, eglglessink->vertshader);
  if (got_gl_error ("glAttachShader vertices"))
    goto HANDLE_ERROR;
  glAttachShader (eglglessink->program, eglglessink->fragshader);
  if (got_gl_error ("glAttachShader fragments"))
    goto HANDLE_ERROR;
  glLinkProgram (eglglessink->program);
  glGetProgramiv (eglglessink->program, GL_LINK_STATUS, &test);
  if (test != GL_FALSE)
    GST_DEBUG_OBJECT (eglglessink, "GLES: Successfully linked program");
  else {
    GST_ERROR_OBJECT (eglglessink, "Couldn't link program");
    goto HANDLE_ERROR;
  }

  glUseProgram (eglglessink->program);
  if (got_gl_error ("glUseProgram"))
    goto HANDLE_ERROR;

  eglglessink->coord_pos =
      glGetAttribLocation (eglglessink->program, "position");
  eglglessink->tex_pos = glGetAttribLocation (eglglessink->program, "texpos");

  /* Generate and bind texture */
  if (!eglglessink->have_texture) {
    gint i;

    GST_INFO_OBJECT (eglglessink, "Performing initial texture setup");

    g_mutex_lock (eglglessink->flow_lock);

    for (i = 0; i < eglglessink->n_textures; i++) {
      if (i == 0)
        glActiveTexture (GL_TEXTURE0);
      else if (i == 1)
        glActiveTexture (GL_TEXTURE1);
      else if (i == 2)
        glActiveTexture (GL_TEXTURE2);

      glGenTextures (1, &eglglessink->texture[i]);
      if (got_gl_error ("glGenTextures"))
        goto HANDLE_ERROR_LOCKED;

      glBindTexture (GL_TEXTURE_2D, eglglessink->texture[i]);
      if (got_gl_error ("glBindTexture"))
        goto HANDLE_ERROR_LOCKED;

      eglglessink->tex_uniform[i] = glGetUniformLocation (eglglessink->program,
          texnames[i]);
      glUniform1i (eglglessink->tex_uniform[i], i);

      /* Set 2D resizing params */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      /* The following two calls are for non-POT width/height cases. If these
       * are not set the texture image unit returns (R, G, B, A) = black
       * on glTexImage2D. For a deeper explanation take a look at
       * the OpenGl ES docs for glTexParameter */
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      if (got_gl_error ("glTexParameteri"))
        goto HANDLE_ERROR_LOCKED;
    }

    eglglessink->have_texture = TRUE;
    g_mutex_unlock (eglglessink->flow_lock);
  }

  g_free (tmp_prog);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR_LOCKED:
  GST_ERROR_OBJECT (eglglessink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR_LOCKED:
  g_mutex_unlock (eglglessink->flow_lock);
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't setup EGL surface");
  g_free (tmp_prog);
  return FALSE;
}

static gboolean
gst_eglglessink_init_egl_display (GstEglGlesSink * eglglessink)
{
  EGLint egl_major, egl_minor;

  GST_DEBUG_OBJECT (eglglessink, "Enter EGL initial configuration");

  eglglessink->display = eglGetDisplay (EGL_DEFAULT_DISPLAY);
  if (eglglessink->display == EGL_NO_DISPLAY) {
    GST_ERROR_OBJECT (eglglessink, "Could not get EGL display connection");
    goto HANDLE_ERROR;          /* No EGL error is set by eglGetDisplay() */
  }

  if (!eglInitialize (eglglessink->display, &egl_major, &egl_minor)) {
    show_egl_error ("eglInitialize");
    GST_ERROR_OBJECT (eglglessink, "Could not init EGL display connection");
    goto HANDLE_EGL_ERROR;
  }

  /* Check against required EGL version */
  if (egl_major < GST_EGLGLESSINK_EGL_MIN_VERSION) {
    GST_ERROR_OBJECT (eglglessink, "EGL v%d\n needed, but you only have v%d.%d",
        GST_EGLGLESSINK_EGL_MIN_VERSION, egl_major, egl_minor);
    goto HANDLE_ERROR;
  }

  GST_INFO_OBJECT (eglglessink, "System reports supported EGL version v%d.%d",
      egl_major, egl_minor);

  eglBindAPI (EGL_OPENGL_ES_API);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (eglglessink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't setup window/surface from handle");
  return FALSE;
}

static gboolean
gst_eglglessink_choose_config (GstEglGlesSink * eglglessink)
{
  EGLint con_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
  GLint egl_configs;

  if ((eglChooseConfig (eglglessink->display,
              eglglessink->selected_fmt->attribs, &eglglessink->config, 1,
              &egl_configs)) == EGL_FALSE) {
    show_egl_error ("eglChooseConfig");
    GST_ERROR_OBJECT (eglglessink, "eglChooseConfig failed");
    goto HANDLE_EGL_ERROR;
  }

  if (egl_configs < 1) {
    GST_ERROR_OBJECT (eglglessink,
        "Could not find matching framebuffer config");
    goto HANDLE_ERROR;
  }

  eglglessink->context = eglCreateContext (eglglessink->display,
      eglglessink->config, EGL_NO_CONTEXT, con_attribs);

  if (eglglessink->context == EGL_NO_CONTEXT) {
    GST_ERROR_OBJECT (eglglessink, "Error getting context, eglCreateContext");
    goto HANDLE_EGL_ERROR;
  }

  GST_DEBUG_OBJECT (eglglessink, "EGL Context: %p", eglglessink->context);

  return TRUE;

  /* Errors */
HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (eglglessink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't choose an usable config");
  return FALSE;
}

static void
gst_eglglessink_set_window_handle (GstXOverlay * overlay, guintptr id)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));
  GST_DEBUG_OBJECT (eglglessink, "We got a window handle!");

  if (eglglessink->have_window) {
    GST_WARNING_OBJECT (eglglessink,
        "We already have a window. Ignoring request");
    return;
  }

  if (!id) {
    GST_ERROR_OBJECT (eglglessink, "Window handle is invalid");
    goto HANDLE_ERROR;
  }

  /* OK, we have a new window */
  g_mutex_lock (eglglessink->flow_lock);
  eglglessink->window = (EGLNativeWindowType) id;
  eglglessink->have_window = TRUE;
  g_mutex_unlock (eglglessink->flow_lock);

  return;

  /* Errors */
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't setup window/surface from handle");
  return;
}

/* Drafted */
static void
gst_eglglessink_set_render_rectangle (GstXOverlay * overlay, gint x, gint y,
    gint width, gint height)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));

  g_mutex_lock (eglglessink->flow_lock);

  if (width == -1 && height == -1) {
    /* This is the set_defaults condition according to
     * the xOverlay interface docs
     */
    eglglessink->display_region.w = 0;
    eglglessink->display_region.h = 0;
  } else {
    g_mutex_lock (eglglessink->flow_lock);
    eglglessink->display_region.x = x;
    eglglessink->display_region.y = y;
    eglglessink->display_region.w = width;
    eglglessink->display_region.h = height;
  }

  g_mutex_unlock (eglglessink->flow_lock);

  return;
}

/* Rendering and display */
static GstFlowReturn
gst_eglglessink_render_and_display (GstEglGlesSink * eglglessink,
    GstBuffer * buf)
{
  GstVideoRectangle frame, surface;
  gint w, h;

#ifdef EGL_FAST_RENDERING_POSSIBLE
  EGLImageKHR img = EGL_NO_IMAGE_KHR;
  EGLint attrs[] = { EGL_IMAGE_PRESERVED_KHR,
    EGL_FALSE, EGL_NONE, EGL_NONE
  };
#endif

  if (!buf) {
    GST_ERROR_OBJECT (eglglessink, "Null buffer, no past queue implemented");
    goto HANDLE_ERROR;
  }

  w = GST_VIDEO_SINK_WIDTH (eglglessink);
  h = GST_VIDEO_SINK_HEIGHT (eglglessink);

  GST_DEBUG_OBJECT (eglglessink,
      "Got good buffer %p. Sink geometry is %dx%d size %d", buf, w, h,
      GST_BUFFER_SIZE (buf));

  switch (eglglessink->rendering_path) {
#ifdef EGL_FAST_RENDERING_POSSIBLE
    case GST_EGLGLESSINK_RENDER_FAST:
      /* XXX: Not Fully implemented */
      img = my_eglCreateImageKHR (eglglessink->display, EGL_NO_CONTEXT,
          EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer) GST_BUFFER_DATA (buf),
          attrs);

      if (img == EGL_NO_IMAGE_KHR) {
        GST_ERROR_OBJECT (eglglessink, "my_eglCreateImageKHR failed");
        goto HANDLE_EGL_ERROR;
      }

      my_glEGLImageTargetTexture2DOES (GL_TEXTURE_2D, img);

      break;
#endif
    default:                   /* case GST_EGLGLESSINK_RENDER_SLOW */

      switch (eglglessink->selected_fmt->fmt) {
        case GST_EGLGLESSINK_IMAGE_RGB888:
          glActiveTexture (GL_TEXTURE0);
          glBindTexture (GL_TEXTURE_2D, eglglessink->texture[0]);
          glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB,
              GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
          break;
        case GST_EGLGLESSINK_IMAGE_RGB565:
          glActiveTexture (GL_TEXTURE0);
          glBindTexture (GL_TEXTURE_2D, eglglessink->texture[0]);
          glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB,
              GL_UNSIGNED_SHORT_5_6_5, GST_BUFFER_DATA (buf));
          break;
        case GST_EGLGLESSINK_IMAGE_RGBA8888:
          switch (eglglessink->format) {
            case GST_VIDEO_FORMAT_RGBA:
            case GST_VIDEO_FORMAT_BGRA:
            case GST_VIDEO_FORMAT_ARGB:
            case GST_VIDEO_FORMAT_ABGR:
            case GST_VIDEO_FORMAT_RGBx:
            case GST_VIDEO_FORMAT_BGRx:
            case GST_VIDEO_FORMAT_xRGB:
            case GST_VIDEO_FORMAT_xBGR:
              glActiveTexture (GL_TEXTURE0);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[0]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
              break;
            case GST_VIDEO_FORMAT_AYUV:
              glActiveTexture (GL_TEXTURE0);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[0]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
              break;
            case GST_VIDEO_FORMAT_Y444:
            case GST_VIDEO_FORMAT_I420:
            case GST_VIDEO_FORMAT_YV12:
            case GST_VIDEO_FORMAT_Y42B:
            case GST_VIDEO_FORMAT_Y41B:{
              gint coffset, cw, ch;

              coffset =
                  gst_video_format_get_component_offset (eglglessink->format, 0,
                  w, h);
              cw = gst_video_format_get_component_width (eglglessink->format, 0,
                  w);
              ch = gst_video_format_get_component_height (eglglessink->format,
                  0, h);
              glActiveTexture (GL_TEXTURE0);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[0]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                  cw, ch, 0, GL_LUMINANCE,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);
              coffset =
                  gst_video_format_get_component_offset (eglglessink->format, 1,
                  w, h);
              cw = gst_video_format_get_component_width (eglglessink->format, 1,
                  w);
              ch = gst_video_format_get_component_height (eglglessink->format,
                  1, h);
              glActiveTexture (GL_TEXTURE1);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[1]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                  cw, ch, 0, GL_LUMINANCE,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);
              coffset =
                  gst_video_format_get_component_offset (eglglessink->format, 2,
                  w, h);
              cw = gst_video_format_get_component_width (eglglessink->format, 2,
                  w);
              ch = gst_video_format_get_component_height (eglglessink->format,
                  2, h);
              glActiveTexture (GL_TEXTURE2);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[2]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                  cw, ch, 0, GL_LUMINANCE,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);
              break;
            }
            case GST_VIDEO_FORMAT_YUY2:
            case GST_VIDEO_FORMAT_UYVY:
              glActiveTexture (GL_TEXTURE0);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[0]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                  w, h, 0, GL_LUMINANCE_ALPHA,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
              glActiveTexture (GL_TEXTURE1);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[1]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
                  GST_ROUND_UP_2 (w) / 2, h, 0, GL_RGBA,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf));
              break;
            case GST_VIDEO_FORMAT_NV12:
            case GST_VIDEO_FORMAT_NV21:{
              gint coffset, cw, ch;

              coffset =
                  gst_video_format_get_component_offset (eglglessink->format, 0,
                  w, h);
              cw = gst_video_format_get_component_width (eglglessink->format, 0,
                  w);
              ch = gst_video_format_get_component_height (eglglessink->format,
                  0, h);
              glActiveTexture (GL_TEXTURE0);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[0]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
                  cw, ch, 0, GL_LUMINANCE,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);

              coffset =
                  gst_video_format_get_component_offset (eglglessink->format,
                  (eglglessink->format == GST_VIDEO_FORMAT_NV12 ? 1 : 2), w, h);
              cw = gst_video_format_get_component_width (eglglessink->format, 1,
                  w);
              ch = gst_video_format_get_component_height (eglglessink->format,
                  1, h);
              glActiveTexture (GL_TEXTURE1);
              glBindTexture (GL_TEXTURE_2D, eglglessink->texture[1]);
              glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
                  cw, ch, 0, GL_LUMINANCE_ALPHA,
                  GL_UNSIGNED_BYTE, GST_BUFFER_DATA (buf) + coffset);
              break;
            }
            default:
              g_assert_not_reached ();
              break;
          }
      }

      if (got_gl_error ("glTexImage2D"))
        goto HANDLE_ERROR;

      /* If no one has set a display rectangle on us initialize
       * a sane default. According to the docs on the xOverlay
       * interface we are supposed to fill the overlay 100%
       */
      if (!eglglessink->display_region.w || !eglglessink->display_region.h) {
        g_mutex_lock (eglglessink->flow_lock);
        if (!eglglessink->keep_aspect_ratio) {
          eglglessink->display_region.x = 0;
          eglglessink->display_region.y = 0;
          eglglessink->display_region.w = eglglessink->surface_width;
          eglglessink->display_region.h = eglglessink->surface_height;
        } else {
          /* XXX: Proly consider display pixel aspect ratio too? */
          frame.w = w;
          frame.h = h;
          surface.w = eglglessink->surface_width;
          surface.h = eglglessink->surface_height;
          gst_video_sink_center_rect (frame, surface,
              &eglglessink->display_region, TRUE);
        }
        g_mutex_unlock (eglglessink->flow_lock);
        glViewport (eglglessink->display_region.x,
            eglglessink->display_region.y, eglglessink->display_region.w,
            eglglessink->display_region.h);
      }

      glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
      if (got_gl_error ("glDrawElements"))
        goto HANDLE_ERROR;

      if ((eglSwapBuffers (eglglessink->display, eglglessink->surface))
          == EGL_FALSE) {
        show_egl_error ("eglSwapBuffers");
        goto HANDLE_ERROR;
      }
  }

  GST_DEBUG_OBJECT (eglglessink, "Succesfully rendered 1 frame");
  return GST_FLOW_OK;

HANDLE_EGL_ERROR:
  GST_ERROR_OBJECT (eglglessink, "EGL call returned error %x", eglGetError ());
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Rendering disabled for this frame");
  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_eglglessink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (vsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  if (!eglglessink->have_window) {
    GST_ERROR_OBJECT (eglglessink, "I don't have a window to render to");
    return GST_FLOW_ERROR;
  }

  if (!eglglessink->have_surface) {
    GST_ERROR_OBJECT (eglglessink, "I don't have a surface to render to");
    return GST_FLOW_ERROR;
  }
#ifndef EGL_ANDROID_image_native_buffer
  GST_WARNING_OBJECT (eglglessink, "EGL_ANDROID_image_native_buffer not "
      "available");
#endif

  return gst_eglglessink_render_and_display (eglglessink, buf);
}

static GstCaps *
gst_eglglessink_getcaps (GstBaseSink * bsink)
{
  GstEglGlesSink *eglglessink;
  GstCaps *ret = NULL;

  eglglessink = GST_EGLGLESSINK (bsink);

  g_mutex_lock (eglglessink->flow_lock);
  if (eglglessink->sinkcaps) {
    ret = gst_caps_ref (eglglessink->sinkcaps);
  } else {
    ret =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  }

  g_mutex_unlock (eglglessink->flow_lock);

  return ret;
}

static gboolean
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstEglGlesSink *eglglessink;
  gboolean ret = TRUE;
  gint width, height;
  EGLNativeWindowType window;
  GstEglGlesImageFmt *format;

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_DEBUG_OBJECT (eglglessink,
      "In setcaps. Possible caps %" GST_PTR_FORMAT ", setting caps %"
      GST_PTR_FORMAT, eglglessink->current_caps, caps);

  if (!(ret = gst_video_format_parse_caps (caps, &eglglessink->format, &width,
              &height))) {
    GST_ERROR_OBJECT (eglglessink, "Got weird and/or incomplete caps");
    goto HANDLE_ERROR;
  }

  format = gst_eglglessink_get_compat_format_from_caps (eglglessink, caps);
  if (!format) {
    GST_ERROR_OBJECT (eglglessink,
        "No supported and compatible egl/gles format " "found for given caps");
    goto HANDLE_ERROR;
  } else
    GST_INFO_OBJECT (eglglessink, "Selected compatible egl/gles format %d",
        format->fmt);

  g_mutex_lock (eglglessink->flow_lock);
  eglglessink->selected_fmt = format;
  GST_VIDEO_SINK_WIDTH (eglglessink) = width;
  GST_VIDEO_SINK_HEIGHT (eglglessink) = height;
  g_mutex_unlock (eglglessink->flow_lock);

  /* XXX: Renegotiation not implemented yet */
  if (eglglessink->current_caps) {
    GST_ERROR_OBJECT (eglglessink, "Caps already set. Won't do it again");
    if (gst_caps_is_always_compatible (caps, eglglessink->current_caps)) {
      GST_INFO_OBJECT (eglglessink, "Caps are compatible anyway");
      goto SUCCEED;
    } else {
      GST_INFO_OBJECT (eglglessink,
          "Caps %" GST_PTR_FORMAT "Not always compatible with current-caps %"
          GST_PTR_FORMAT, caps, eglglessink->current_caps);
      GST_WARNING_OBJECT (eglglessink, "Renegotiation not implemented");
      goto HANDLE_ERROR;
    }
  }

  if (!gst_eglglessink_choose_config (eglglessink)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't choose EGL config");
    goto HANDLE_ERROR;
  }

  g_mutex_lock (eglglessink->flow_lock);
  eglglessink->current_caps = gst_caps_ref (caps);
  g_mutex_unlock (eglglessink->flow_lock);

  /* By now the application should have set a window
   * already if it meant to do so
   */
  if (!eglglessink->have_window) {
    GST_INFO_OBJECT (eglglessink,
        "No window. Will attempt internal window creation");
    if (!(window = gst_eglglessink_create_window (eglglessink, width, height))) {
      GST_ERROR_OBJECT (eglglessink, "Internal window creation failed!");
      goto HANDLE_ERROR;
    }
    g_mutex_lock (eglglessink->flow_lock);
    eglglessink->using_own_window = TRUE;
    g_mutex_unlock (eglglessink->flow_lock);
    gst_eglglessink_set_window_handle (GST_X_OVERLAY (eglglessink),
        (guintptr) window);
  }

  if (!eglglessink->have_surface) {
    if (!gst_eglglessink_init_egl_surface (eglglessink)) {
      GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL surface from window");
      goto HANDLE_ERROR;
    }
  }

  if (!eglglessink->have_vbo) {
    if (!gst_eglglessink_setup_vbo (eglglessink, FALSE)) {
      GST_ERROR_OBJECT (eglglessink, "VBO setup failed");
      goto HANDLE_ERROR;
    }
  }

SUCCEED:
  GST_INFO_OBJECT (eglglessink, "Setcaps succeed");
  return TRUE;

/* Errors */
HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Setcaps failed");
  return FALSE;
}

static void
gst_eglglessink_wipe_fmt (gpointer data)
{
  GstEglGlesImageFmt *format = data;
  gst_caps_unref (format->caps);
  g_free (format);
}

static GstStateChangeReturn
gst_eglglessink_change_state (GstElement * element, GstStateChange transition)
{
  GstEglGlesSink *eglglessink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  eglglessink = GST_EGLGLESSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!egl_init (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      g_mutex_lock (eglglessink->flow_lock);
      if (eglglessink->display) {
        eglTerminate (eglglessink->display);
        eglglessink->display = NULL;
      }

      eglglessink->selected_fmt = NULL;
      g_list_free_full (eglglessink->supported_fmts, gst_eglglessink_wipe_fmt);
      eglglessink->supported_fmts = NULL;
      gst_caps_unref (eglglessink->sinkcaps);
      eglglessink->sinkcaps = NULL;
      eglglessink->egl_started = FALSE;
      g_mutex_unlock (eglglessink->flow_lock);
      break;
    default:
      break;
  }

done:
  return ret;
}

static void
gst_eglglessink_finalize (GObject * object)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  g_mutex_free (eglglessink->flow_lock);
  eglglessink->flow_lock = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_SILENT:
      eglglessink->silent = g_value_get_boolean (value);
      break;
    case PROP_CREATE_WINDOW:
      eglglessink->can_create_window = g_value_get_boolean (value);
      break;
    case PROP_DEFAULT_HEIGHT:
      eglglessink->window_default_height = g_value_get_int (value);
      break;
    case PROP_DEFAULT_WIDTH:
      eglglessink->window_default_width = g_value_get_int (value);
      break;
    case PROP_FORCE_RENDERING_SLOW:
      eglglessink->force_rendering_slow = g_value_get_boolean (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      eglglessink->keep_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_SILENT:
      g_value_set_boolean (value, eglglessink->silent);
      break;
    case PROP_CREATE_WINDOW:
      g_value_set_boolean (value, eglglessink->can_create_window);
      break;
    case PROP_DEFAULT_HEIGHT:
      g_value_set_int (value, eglglessink->window_default_height);
      break;
    case PROP_DEFAULT_WIDTH:
      g_value_set_int (value, eglglessink->window_default_width);
      break;
    case PROP_FORCE_RENDERING_SLOW:
      g_value_set_boolean (value, eglglessink->force_rendering_slow);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, eglglessink->keep_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglglessink_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "EGL/GLES vout Sink",
      "Sink/Video",
      "An EGL/GLES Video Output Sink Implementing the XOverlay interface",
      "Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_eglglessink_sink_template_factory));
}

/* initialize the eglglessink's class */
static void
gst_eglglessink_class_init (GstEglGlesSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_eglglessink_set_property;
  gobject_class->get_property = gst_eglglessink_get_property;
  gobject_class->finalize = gst_eglglessink_finalize;

  gstelement_class->change_state = gst_eglglessink_change_state;

  gstbasesink_class->start = gst_eglglessink_start;
  gstbasesink_class->stop = gst_eglglessink_stop;
  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_getcaps);
  gstbasesink_class->buffer_alloc = GST_DEBUG_FUNCPTR
      (gst_eglglessink_buffer_alloc);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_eglglessink_show_frame);

  g_object_class_install_property (gobject_class, PROP_SILENT,
      g_param_spec_boolean ("silent", "Silent", "Produce no output",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CREATE_WINDOW,
      g_param_spec_boolean ("create-window", "Create Window",
          "Attempt to create a window if none is provided",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_RENDERING_SLOW,
      g_param_spec_boolean ("force-rendering-slow", "Force Slow Rendering",
          "Force slow rendering path", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force Aspect Ratio",
          "When enabled, scaling will respect original aspect ratio",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEFAULT_WIDTH,
      g_param_spec_int ("window-default-width", "Default Width",
          "Default width for self created windows", 0,
          EGLGLESSINK_MAX_FRAME_WIDTH, EGLGLESSINK_MAX_FRAME_WIDTH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEFAULT_HEIGHT,
      g_param_spec_int ("window-default-height", "Default Height",
          "Default height for self created windows", 0,
          EGLGLESSINK_MAX_FRAME_HEIGHT, EGLGLESSINK_MAX_FRAME_HEIGHT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}


static void
gst_eglglessink_init (GstEglGlesSink * eglglessink,
    GstEglGlesSinkClass * gclass)
{
  /* Init defaults */
  eglglessink->have_window = FALSE;
  eglglessink->have_surface = FALSE;
  eglglessink->have_vbo = FALSE;
  eglglessink->have_texture = FALSE;
  eglglessink->egl_started = FALSE;
  eglglessink->can_create_window = TRUE;
  eglglessink->force_rendering_slow = FALSE;
  eglglessink->keep_aspect_ratio = TRUE;
  eglglessink->using_own_window = FALSE;
  eglglessink->flow_lock = g_mutex_new ();
}

/* Interface initializations. Used here for initializing the XOverlay
 * Interface.
 */
static void
gst_eglglessink_init_interfaces (GType type)
{
  static const GInterfaceInfo implements_info = {
    (GInterfaceInitFunc) gst_eglglessink_implements_init, NULL, NULL
  };

  static const GInterfaceInfo xoverlay_info = {
    (GInterfaceInitFunc) gst_eglglessink_xoverlay_init, NULL, NULL
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_info);
  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &xoverlay_info);

}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
eglglessink_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_eglglessink_debug, "eglglessink",
      0, "Simple EGL/GLES Sink");

  return gst_element_register (plugin, "eglglessink", GST_RANK_PRIMARY,
      GST_TYPE_EGLGLESSINK);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "EGL/GLES Sink"
#endif

#ifndef VERSION
#define VERSION "0.911"
#endif

/* gstreamer looks for this structure to register eglglessinks */
GST_PLUGIN_DEFINE2 (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    eglglessink,
    "EGL/GLES sink",
    eglglessink_plugin_init,
    VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
