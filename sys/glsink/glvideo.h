
#ifndef __GLVIDEO_H__
#define __GLVIDEO_H__

#include <GL/glx.h>
#include <GL/gl.h>
#include <glib.h>

typedef struct _GLVideoDisplay GLVideoDisplay;
typedef struct _GLVideoDrawable GLVideoDrawable;

typedef enum {
  GLVIDEO_IMAGE_TYPE_RGBx,
  GLVIDEO_IMAGE_TYPE_BGRx,
  GLVIDEO_IMAGE_TYPE_xRGB,
  GLVIDEO_IMAGE_TYPE_xBGR,
  GLVIDEO_IMAGE_TYPE_YUY2,
  GLVIDEO_IMAGE_TYPE_UYVY,
  GLVIDEO_IMAGE_TYPE_AYUV,
} GLVideoImageType;


struct _GLVideoDisplay {
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

struct _GLVideoDrawable {
  GLVideoDisplay *display;

  Window window;

  gboolean destroy_on_free;

  int win_width;
  int win_height;
};


GLVideoDisplay *glv_display_new (const char *display_name);
gboolean glv_display_can_handle_type (GLVideoDisplay *display,
    GLVideoImageType type);
void glv_display_free (GLVideoDisplay *display);

/* drawable */

GLVideoDrawable * glv_drawable_new_window (GLVideoDisplay *display);
GLVideoDrawable * glv_drawable_new_root_window (GLVideoDisplay *display);
GLVideoDrawable * glv_drawable_new_from_window (GLVideoDisplay *display, Window window);
void glv_drawable_free (GLVideoDrawable *drawable);
void glv_drawable_lock (GLVideoDrawable *drawable);
void glv_drawable_unlock (GLVideoDrawable *drawable);
void glv_drawable_update_attributes (GLVideoDrawable *drawable);
void glv_drawable_clear (GLVideoDrawable *drawable);
void glv_drawable_draw_image (GLVideoDrawable *drawable, GLVideoImageType type, void *data, int width, int height);


#endif

