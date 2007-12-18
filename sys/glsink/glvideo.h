
#ifndef __GST_GL_H__
#define __GST_GL_H__

#include <GL/glx.h>
#include <GL/gl.h>
#include <gst/gst.h>

typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstGLDrawable GstGLDrawable;

typedef enum {
  GST_GL_IMAGE_TYPE_RGBx,
  GST_GL_IMAGE_TYPE_BGRx,
  GST_GL_IMAGE_TYPE_xRGB,
  GST_GL_IMAGE_TYPE_xBGR,
  GST_GL_IMAGE_TYPE_YUY2,
  GST_GL_IMAGE_TYPE_UYVY,
  GST_GL_IMAGE_TYPE_AYUV,
} GstGLImageType;


struct _GstGLDisplay {
  Display *display;
  XVisualInfo *visinfo;
  GLXContext context;
  GMutex *lock;

  Screen *screen;
  int scrnum;
  Window root;

  int max_texture_size;

  gboolean have_ycbcr_texture;
  gboolean have_texture_rectangle;
  gboolean have_color_matrix;
}; 

struct _GstGLDrawable {
  GstGLDisplay *display;

  Window window;

  gboolean destroy_on_free;

  int win_width;
  int win_height;
};


GstGLDisplay *gst_gl_display_new (const char *display_name);
gboolean gst_gl_display_can_handle_type (GstGLDisplay *display,
    GstGLImageType type);
void gst_gl_display_free (GstGLDisplay *display);
void gst_gl_display_lock (GstGLDisplay *display);
void gst_gl_display_unlock (GstGLDisplay *display);

/* drawable */

GstGLDrawable * gst_gl_drawable_new_window (GstGLDisplay *display);
GstGLDrawable * gst_gl_drawable_new_root_window (GstGLDisplay *display);
GstGLDrawable * gst_gl_drawable_new_from_window (GstGLDisplay *display, Window window);
void gst_gl_drawable_free (GstGLDrawable *drawable);
void gst_gl_drawable_lock (GstGLDrawable *drawable);
void gst_gl_drawable_unlock (GstGLDrawable *drawable);
void gst_gl_drawable_update_attributes (GstGLDrawable *drawable);
void gst_gl_drawable_clear (GstGLDrawable *drawable);
void gst_gl_drawable_draw_image (GstGLDrawable *drawable, GstGLImageType type, void *data, int width, int height);


#endif

