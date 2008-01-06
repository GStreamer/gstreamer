
#ifndef __GST_GL_H__
#define __GST_GL_H__

#include <GL/glx.h>
#include <GL/gl.h>
#include <gst/gst.h>
#include <gst/video/video.h>

typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstGLDisplayClass GstGLDisplayClass;

#define GST_TYPE_GL_DISPLAY \
      (gst_gl_display_get_type())
#define GST_GL_DISPLAY(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_DISPLAY,GstGLDisplay))
#define GST_GL_DISPLAY_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_DISPLAY,GstGLDisplayClass))
#define GST_IS_GL_DISPLAY(obj) \
      (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_DISPLAY))
#define GST_IS_GL_DISPLAY_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_DISPLAY))


struct _GstGLDisplay {
  GObject object;

  Display *display;
  GC gc;
  XVisualInfo *visinfo;
  GLXContext context;
  GMutex *lock;

  Screen *screen;
  int screen_num;
  Visual *visual;
  Window root;
  guint32 white;
  guint32 black;
  int depth;

  int max_texture_size;

  gboolean have_ycbcr_texture;
  gboolean have_texture_rectangle;
  gboolean have_color_matrix;

  Window window;
  gboolean visible;
  Window parent_window;

  int win_width;
  int win_height;

}; 

struct _GstGLDisplayClass {
  GObjectClass object_class;
};

GType gst_gl_display_get_type (void);


GstGLDisplay *gst_gl_display_new (void);
gboolean gst_gl_display_connect (GstGLDisplay *display,
    const char *display_name);
gboolean gst_gl_display_can_handle_type (GstGLDisplay *display,
    GstVideoFormat type);
void gst_gl_display_lock (GstGLDisplay *display);
void gst_gl_display_unlock (GstGLDisplay *display);
void gst_gl_display_set_window (GstGLDisplay *display, Window window);
void gst_gl_display_update_attributes (GstGLDisplay *display);
void gst_gl_display_clear (GstGLDisplay *display);
void gst_gl_display_draw_image (GstGLDisplay * display, GstVideoFormat type,
    void *data, int width, int height);
void gst_gl_display_draw_rbo (GstGLDisplay * display, GLuint rbo,
    int width, int height);
void gst_gl_display_draw_texture (GstGLDisplay * display, GLuint texture,
    int width, int height);
void gst_gl_display_check_error (GstGLDisplay *display, int line);
GLuint gst_gl_display_upload_texture_rectangle (GstGLDisplay *display,
    GstVideoFormat type, void *data, int width, int height);
void gst_gl_display_set_visible (GstGLDisplay *display, gboolean visible);

#endif

