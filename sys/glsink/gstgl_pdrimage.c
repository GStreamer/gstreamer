#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
/* gcc -ansi -pedantic on GNU/Linux causes warnings and errors
 * unless this is defined:
 * warning: #warning "Files using this header must be compiled with _SVID_SOURCE or _XOPEN_SOURCE"
 */
#ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#define GL_GLEXT_PROTOTYPES

// VERY dangerous:
#define GL_WRITE_PIXEL_DATA_RANGE_NV 0x8878

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glu.h>
#include <string.h>

#include "gstglsink.h"

typedef struct _GstGLImageConnection GstGLImageConnection;
struct _GstGLImageConnection
{
  GstImageConnection conn;
  Display *dpy;
  gint w, h;
  gint bpp;

  int ytex_id;
  int uvtex_id;
  int septex_id;
  unsigned char *m_memory;
  int m_bufslots[4];
};

#define TEX_XSIZE 1024
#define TEX_YSIZE 1024
#define YUVTEX_SIZE ((TEX_XSIZE * TEX_YSIZE) * 3 /2)

#define AGP_BUFSLOTS 4

typedef struct _GstNvImage GstNvImage;
struct _GstNvImage
{
  GstImageData data;
  int slot;			// < AGP_BUFSLOTS: allocated from AGP mem, otherwise from CPU mem
  GstGLImageConnection *conn;
};

static GstGLImageInfo *gst_gl_nvimage_info (GstImageInfo * info);
static GstGLImageConnection *gst_gl_nvimage_connection (GstImageConnection *
    conn);
static gboolean gst_gl_nvimage_check_xvideo ();

static GstCaps *gst_gl_nvimage_get_caps (GstImageInfo * info);
static GstImageConnection *gst_gl_nvimage_set_caps (GstImageInfo * info,
    GstCaps * caps);
static GstImageData *gst_gl_nvimage_get_image (GstImageInfo * info,
    GstImageConnection * conn);
static void gst_gl_nvimage_put_image (GstImageInfo * info,
    GstImageData * image);
static void gst_gl_nvimage_free_image (GstImageData * image);
static void gst_gl_nvimage_open_conn (GstImageConnection * conn,
    GstImageInfo * info);
static void gst_gl_nvimage_close_conn (GstImageConnection * conn,
    GstImageInfo * info);
static void gst_gl_nvimage_free_conn (GstImageConnection * conn);

GstImagePlugin *
get_gl_nvimage_plugin (void)
{
  static GstImagePlugin plugin = { gst_gl_nvimage_get_caps,
    gst_gl_nvimage_set_caps,
    gst_gl_nvimage_get_image,
    gst_gl_nvimage_put_image,
    gst_gl_nvimage_free_image
  };

  return &plugin;
}


static GstGLImageInfo *
gst_gl_nvimage_info (GstImageInfo * info)
{
  if (info == NULL || info->id != GST_MAKE_FOURCC ('X', 'l', 'i', 'b')) {
    return NULL;
  }
  return (GstGLImageInfo *) info;
}

static GstGLImageConnection *
gst_gl_nvimage_connection (GstImageConnection * conn)
{
  if (conn == NULL || conn->free_conn != gst_gl_nvimage_free_conn)
    return NULL;
  return (GstGLImageConnection *) conn;
}

gboolean
gst_gl_nvimage_check_xvideo ()
{
  int ver, rel, req, ev, err;

#if 0
  if (display == NULL)
    return FALSE;
  if (Success == XvQueryExtension (display, &ver, &rel, &req, &ev, &err))
    return TRUE;
#endif

  return FALSE;
}

static GstCaps *
gst_gl_nvimage_get_caps (GstImageInfo * info)
{
  gint i;
  int adaptors;
  int formats;
  GstCaps *caps = NULL;
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);

  /* we don't handle these image information */
  if (xinfo == NULL)
    return NULL;

  if (gst_gl_nvimage_check_xvideo () == FALSE) {
    g_warning ("GL_NVImage: Server has no NVidia extension support\n");
    return NULL;
  }

  caps = gst_caps_append (caps, GST_CAPS_NEW ("xvimage_caps",
	  "video/raw",
	  "format", GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y', 'C', '1', '2')),
	  "width", GST_PROPS_INT_RANGE (0, 1024),
	  "height", GST_PROPS_INT_RANGE (0, 1024))
      );
  return caps;
}

static GstImageConnection *
gst_gl_nvimage_set_caps (GstImageInfo * info, GstCaps * caps)
{
  gint i, j = 0;
  int adaptors;
  int formats;
  GstGLImageConnection *conn;
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);
  guint32 format;

  /* we don't handle these image information */
  if (xinfo == NULL)
    return NULL;

  conn = g_new0 (GstGLImageConnection, 1);
  conn->conn.open_conn = gst_gl_nvimage_open_conn;
  conn->conn.close_conn = gst_gl_nvimage_close_conn;
  conn->conn.free_conn = gst_gl_nvimage_free_conn;

  gst_caps_get (caps,
      "width", &conn->w, "height", &conn->h, "format", &format, NULL);

  // maybe I should a bit more checking here, e.g. maximum size smaller than maximum texture extents
  if (format != GST_MAKE_FOURCC ('R', 'G', 'B', ' ')) {
    GST_DEBUG (GST_CAT_PLUGIN_INFO, "GL_NVImage: Format is invalid !\n");
    return NULL;
  }
  if (0)			//conn->port == (XvPortID) -1)
  {
    /* this happens if the plugin can't handle the caps, so no warning */
    g_free (conn);
    return NULL;
  }

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "GL_NVImage: caps %p are ok, creating image",
      caps);
  return (GstImageConnection *) conn;
}

static GstImageData *
gst_gl_nvimage_get_image (GstImageInfo * info, GstImageConnection * conn)
{
  GstNvImage *image;
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);
  GstGLImageConnection *nvconn = gst_gl_nvimage_connection (conn);
  int slot = 0;

  /* checks */
  if (xinfo == NULL)
    return NULL;
  if (nvconn == NULL)
    return NULL;

  // I should also check the current GLX context ! 
  // Ah, Don't have to, I am guarantueed to always be in the same thread

  image = g_new0 (GstNvImage, 1);

  for (slot = 0; slot < AGP_BUFSLOTS; slot++) {
    if (!nvconn->m_bufslots[slot])
      break;
  }

  image->data.size = nvconn->w * nvconn->h * 3 / 2;

  if (slot < AGP_BUFSLOTS)	// found an AGP buffer slot
  {
    image->data.data = nvconn->m_memory + slot * YUVTEX_SIZE;
    image->slot = slot;		// store for freeing
    nvconn->m_bufslots[slot] = 1;	// it is now taken
  } else {
    g_warning ("Allocating from main memory !");
    image->data.data = g_malloc (image->data.size);
    image->slot = AGP_BUFSLOTS;	// no AGP slot
  }
  image->conn = nvconn;

  if (image->data.data == NULL) {
    g_warning ("GL_NvImage: data allocation failed!");
    g_free (image);
    return NULL;
  }

  return (GstImageData *) image;
}

static void
gst_gl_nvimage_put_image (GstImageInfo * info, GstImageData * image)
{
  GstNvImage *im = (GstNvImage *) image;
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);

  /* checks omitted for speed (and lazyness), do we need them? */
  g_assert (xinfo != NULL);

  /* Upload the texture here */
  g_warning ("PUTTING IMAGE - BROOOKEN");

  // both upload the video, and redraw the screen
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glEnable (GL_TEXTURE_2D);

  glPushMatrix ();
  //glTranslatef(0,1,0);
  glRotatef (xinfo->rotX - 250, 1, 0, 0);
  glRotatef (xinfo->rotY, 0, 1, 0);
  int zoom = xinfo->zoom;

  glScaled (zoom, zoom, zoom);
  //Draws the surface rectangle

  glBindTexture (GL_TEXTURE_2D, im->conn->ytex_id);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, im->conn->w, im->conn->h, GL_RGBA,
      GL_UNSIGNED_BYTE, im->data.data);
  float xmax = (float) im->conn->w / TEX_XSIZE;
  float ymax = (float) im->conn->h / TEX_YSIZE;

  glColor4f (1, 1, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, -1, 0);

  glTexCoord2f (xmax, 0);
  glVertex3f (4, 0, -4);

  glTexCoord2f (0, 0);
  glVertex3f (-4, 0, -4);

  glTexCoord2f (0, ymax);
  glVertex3f (-4, 0, 4);

  glTexCoord2f (xmax, ymax);
  glVertex3f (4, 0, 4);

  glEnd ();

  glPopMatrix ();

  glXSwapBuffers (xinfo->dpy, xinfo->win);
}

static void
gst_gl_nvimage_free_image (GstImageData * image)
{
  GstNvImage *im = (GstNvImage *) image;

  g_return_if_fail (im != NULL);
  GstGLImageConnection *nvconn = im->conn;

  if (im->slot < AGP_BUFSLOTS) {
    nvconn->m_bufslots[im->slot] = 0;
  } else
    g_free (im->data.data);

  g_free (im);
}

static void
gst_gl_nvimage_open_conn (GstImageConnection * conn, GstImageInfo * info)
{
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);
  GstGLImageConnection *xconn = gst_gl_nvimage_connection (conn);

  unsigned char data_sep[2][2] = { {0, 255}, {0, 255} };
  int slot;

  g_warning ("Opening NVidia Connection");
  xconn->m_memory =
      (unsigned char *) glXAllocateMemoryNV (AGP_BUFSLOTS * YUVTEX_SIZE, 0, 1.0,
      1.0);

  if (!xconn->m_memory) {
    printf
	("Unable to acquire graphics card mem... will acquire in normal memory.\n");
    for (slot = 0; slot < AGP_BUFSLOTS; slot++)
      xconn->m_bufslots[slot] = 1;
  } else {
    // maybe this fast writable memory, awfully slow to read from, though
    glPixelDataRangeNV (GL_WRITE_PIXEL_DATA_RANGE_NV,
	AGP_BUFSLOTS * YUVTEX_SIZE, xconn->m_memory);
    glEnableClientState (GL_WRITE_PIXEL_DATA_RANGE_NV);

    for (slot = 0; slot < AGP_BUFSLOTS; slot++)
      xconn->m_bufslots[slot] = 0;
  }

  glGenTextures (1, &xconn->ytex_id);
  glBindTexture (GL_TEXTURE_2D, xconn->ytex_id);
  glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE8_ALPHA8, TEX_XSIZE, TEX_YSIZE, 0,
      GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);

  glActiveTextureARB (GL_TEXTURE1_ARB);
  glGenTextures (1, &xconn->uvtex_id);
  glBindTexture (GL_TEXTURE_2D, xconn->uvtex_id);
  glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE8_ALPHA8, TEX_XSIZE / 2,
      TEX_YSIZE / 2, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);

  glActiveTextureARB (GL_TEXTURE2_ARB);
  glGenTextures (1, &xconn->septex_id);
  glBindTexture (GL_TEXTURE_2D, xconn->septex_id);
  glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE8, 2, 2, 0, GL_LUMINANCE,
      GL_UNSIGNED_BYTE, data_sep);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);

  glFlushPixelDataRangeNV (GL_WRITE_PIXEL_DATA_RANGE_NV);
  //glEnable(GL_TEXTURE_2D);
  glActiveTextureARB (GL_TEXTURE0_ARB);
  glEnable (GL_TEXTURE_2D);
  glActiveTextureARB (GL_TEXTURE1_ARB);
  glEnable (GL_TEXTURE_2D);
  glActiveTextureARB (GL_TEXTURE2_ARB);
  glEnable (GL_TEXTURE_2D);
  glActiveTextureARB (GL_TEXTURE0_ARB);
}

static void
gst_gl_nvimage_close_conn (GstImageConnection * conn, GstImageInfo * info)
{
  GstGLImageConnection *xconn = gst_gl_nvimage_connection (conn);
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);

  // anything needed in here ? Oh, maybe drawing de-init, or something
  glDeleteTextures (1, &xconn->ytex_id);
  glDeleteTextures (1, &xconn->uvtex_id);
  glDeleteTextures (1, &xconn->septex_id);
}

static void
gst_gl_nvimage_free_conn (GstImageConnection * conn)
{
  GstGLImageConnection *nvconn = gst_gl_nvimage_connection (conn);

  g_free (nvconn);
}
