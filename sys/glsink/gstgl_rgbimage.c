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
#include <string.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <math.h>

#include "gstglsink.h"

typedef struct _GstGLImageConnection GstGLImageConnection;

// this contains everything to draw an image, including all necessary graphics card data. 
struct _GstGLImageConnection
{
  GstImageConnection conn;
  Display *dpy;			// the Xlib drawing context
  GLXContext ctx;		// The GLX drawing context
  gint w, h;
  gint bpp;

  int rgbatex_id;
  unsigned char *m_memory;
};

#define TEX_XSIZE 1024
#define TEX_YSIZE 1024


typedef struct _GstGLImage GstGLImage;
struct _GstGLImage
{
  GstImageData data;
  GstGLImageConnection *conn;
};

static GstGLImageInfo *gst_gl_rgbimage_info (GstImageInfo * info);
static GstGLImageConnection *gst_gl_rgbimage_connection (GstImageConnection *
    conn);

static GstCaps *gst_gl_rgbimage_get_caps (GstImageInfo * info);
static GstImageConnection *gst_gl_rgbimage_set_caps (GstImageInfo * info,
    GstCaps * caps);
static GstImageData *gst_gl_rgbimage_get_image (GstImageInfo * info,
    GstImageConnection * conn);
static void gst_gl_rgbimage_put_image (GstImageInfo * info,
    GstImageData * image);
static void gst_gl_rgbimage_free_image (GstImageData * image);
static void gst_gl_rgbimage_open_conn (GstImageConnection * conn,
    GstImageInfo * info);
static void gst_gl_rgbimage_close_conn (GstImageConnection * conn,
    GstImageInfo * info);
static void gst_gl_rgbimage_free_conn (GstImageConnection * conn);

GstImagePlugin *
get_gl_rgbimage_plugin (void)
{
  static GstImagePlugin plugin = { gst_gl_rgbimage_get_caps,
    gst_gl_rgbimage_set_caps,
    gst_gl_rgbimage_get_image,
    gst_gl_rgbimage_put_image,
    gst_gl_rgbimage_free_image
  };

  return &plugin;
}

static GstGLImageInfo *
gst_gl_rgbimage_info (GstImageInfo * info)
{
  if (info == NULL || info->id != GST_MAKE_FOURCC ('X', 'l', 'i', 'b')) {
    return NULL;
  }
  return (GstGLImageInfo *) info;
}

static GstGLImageConnection *
gst_gl_rgbimage_connection (GstImageConnection * conn)
{
  if (conn == NULL || conn->free_conn != gst_gl_rgbimage_free_conn)
    return NULL;
  return (GstGLImageConnection *) conn;
}

GstCaps *
gst_gl_rgbimage_get_caps (GstImageInfo * info)
{
  GstCaps *caps = NULL;
  Visual *visual;
  int xpad;
  XWindowAttributes attrib;
  XImage *ximage;
  GstGLImageInfo *xinfo = gst_gl_rgbimage_info (info);

  g_warning ("rgbimage get caps called, context %p, endianness %d !\n",
      glXGetCurrentContext (), G_BIG_ENDIAN);
  /* we don't handle this image information */
  if (xinfo == NULL)
    return NULL;

  XGetWindowAttributes (xinfo->dpy, xinfo->win, &attrib);

  visual = attrib.visual;
  if (attrib.depth <= 8)
    xpad = 8;
  else if (attrib.depth <= 16)
    xpad = 16;
  else
    xpad = 32;

  // create a temporary image
  ximage = XCreateImage (xinfo->dpy, visual, attrib.depth, ZPixmap, 0, NULL,
      100, 100, xpad, (attrib.depth + 7) / 8 * 100);
  if (ximage != NULL) {
    caps =
	GST_CAPS_NEW ("forcing Video RGB", "video/x-raw-rgb", "format",
	GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB ")), "depth", GST_PROPS_INT (24),
	"bpp", GST_PROPS_INT (24), "red_mask", GST_PROPS_INT (0xff),
	"green_mask", GST_PROPS_INT (0xff00), "blue_mask",
	GST_PROPS_INT (0xff0000), "endianness", GST_PROPS_INT (G_BIG_ENDIAN),
								/*= 1234/4321 (INT) <- endianness */
	"width", GST_PROPS_INT_RANGE (0, TEX_XSIZE),	/* can't have videos larger than TEX_SIZE */
	"height", GST_PROPS_INT_RANGE (0, TEX_YSIZE)
	);
    XDestroyImage (ximage);
  }

  printf ("GL_RGBImage: returning caps at %p", caps);
  return caps;
}

static GstImageConnection *
gst_gl_rgbimage_set_caps (GstImageInfo * info, GstCaps * caps)
{
  g_warning ("in set_caps !\n");

  GstGLImageConnection *new = NULL;
  Visual *visual;
  XWindowAttributes attrib;
  GstGLImageInfo *xinfo = gst_gl_rgbimage_info (info);
  guint32 format;
  gint depth;
  gint endianness;
  gint red_mask, green_mask, blue_mask;
  gint width, height, bpp;

  /* check if this is the right image info */
  if (xinfo == NULL)
    return NULL;

  XGetWindowAttributes (xinfo->dpy, xinfo->win, &attrib);

  visual = attrib.visual;

  gst_caps_get (caps,
      "format", &format,
      "depth", &depth,
      "endianness", &endianness,
      "red_mask", &red_mask,
      "green_mask", &green_mask,
      "blue_mask", &blue_mask,
      "width", &width, "height", &height, "bpp", &bpp, NULL);

  /* check if the caps are ok */
  if (format != GST_MAKE_FOURCC ('R', 'G', 'B', ' '))
    return NULL;
  /* if (gst_caps_get_int (caps, "bpp") != ???) return NULL; */
  //if (depth != attrib.depth) return NULL;
  //if (endianness != ((ImageByteOrder (xinfo->dpy) == LSBFirst) ? G_LITTLE_ENDIAN : G_BIG_ENDIAN)) return NULL;
  //if (red_mask != visual->red_mask) return NULL;
  //if (green_mask != visual->green_mask) return NULL;
  //if (blue_mask != visual->blue_mask) return NULL;
  GST_DEBUG ("GL_RGBImage: caps %p are ok, creating image", caps);

  new = g_new (GstGLImageConnection, 1);
  new->conn.open_conn = gst_gl_rgbimage_open_conn;
  new->conn.close_conn = gst_gl_rgbimage_close_conn;
  new->conn.free_conn = gst_gl_rgbimage_free_conn;
  new->dpy = xinfo->dpy;
  new->ctx = xinfo->ctx;
  new->w = width;
  new->h = height;
  new->bpp = bpp;

  return (GstImageConnection *) new;
}

static GstImageData *
gst_gl_rgbimage_get_image (GstImageInfo * info, GstImageConnection * conn)
{
  GstGLImage *image;

  //XWindowAttributes attrib;
  GstGLImageInfo *xinfo = gst_gl_rgbimage_info (info);
  GstGLImageConnection *xconn = gst_gl_rgbimage_connection (conn);

  image = g_new (GstGLImage, 1);

  /* checks */
  if (xinfo == NULL)
    return NULL;
  if (xconn == NULL)
    return NULL;
  if (xinfo->dpy != xconn->dpy) {
    g_warning ("XImage: wrong x display specified in 'get_image'\n");
    return NULL;
  }

  image->conn = xconn;
  image->data.size = xconn->w * xconn->h * 4;
  image->data.data = g_malloc (image->data.size);
  if (image->data.data == NULL) {
    g_warning ("GL_RGBImage: data allocation failed!");
    g_free (image);
    return NULL;
  }

  return (GstImageData *) image;
}


static void
gst_gl_rgbimage_put_image (GstImageInfo * info, GstImageData * image)
{
  float xmax, ymax;

  GstGLImageInfo *xinfo = gst_gl_rgbimage_info (info);
  GstGLImage *im = (GstGLImage *) image;

  int img_width = im->conn->w;
  int img_height = im->conn->h;

  g_assert (xinfo != NULL);

  // both upload the video, and redraw the screen
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef (0.0, 0.0, -5.0);

  glEnable (GL_TEXTURE_2D);

  if (xinfo->info.demo) {
    glTranslatef (0.0, 0.0, -5.0);	// make it avoid the clipping plane, zoom 2.0 instead
    glRotatef (180.0 * sin (xinfo->rotX), 1, 0, 0);
    glRotatef (180.0 * cos (xinfo->rotY), 0, 1, 0);

    xinfo->rotX += 0.01;
    xinfo->rotY -= 0.015;
    float zoom = xinfo->zoom;

    glScalef (zoom, zoom, zoom);

    if (xinfo->zoom > 2.0)
      xinfo->zoomdir = -0.01;

    if (xinfo->zoom < 1.0)
      xinfo->zoomdir = 0.01;

    xinfo->zoom += xinfo->zoomdir;
  }
  //Draws the surface rectangle
  glBindTexture (GL_TEXTURE_2D, im->conn->rgbatex_id);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, im->conn->w, im->conn->h, GL_RGB,
      GL_UNSIGNED_BYTE, im->data.data);
  xmax = (float) im->conn->w / TEX_XSIZE;
  ymax = (float) im->conn->h / TEX_YSIZE;

  float aspect = img_width / (float) img_height;
  float hor = aspect;

  glColor4f (1, 1, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, -1, 0);

  glTexCoord2f (xmax, 0);
  glVertex3f (hor, 1, 0);

  glTexCoord2f (0, 0);
  glVertex3f (-hor, 1, 0);

  glTexCoord2f (0, ymax);
  glVertex3f (-hor, -1, 0);

  glTexCoord2f (xmax, ymax);
  glVertex3f (hor, -1, 0);
  glEnd ();

  if (xinfo->info.dumpvideo) {
    static int framenr = 0;
    char capfilename[255];
    static guint8 *cap_image_data = NULL, *cap_image_data2 = NULL;
    int i;

    // hmmmm, is this reentrant ?!
    if (cap_image_data == NULL)
      cap_image_data = (guint8 *) malloc (img_width * img_height * 3);

    if (cap_image_data2 == NULL)
      cap_image_data2 = (guint8 *) malloc (img_width * img_height * 3);

    printf ("Recording frame #%d\n", framenr);
    glReadPixels (0, 0, img_width, img_height, GL_RGB, GL_UNSIGNED_BYTE,
	cap_image_data);
    // invert the pixels
    for (i = 0; i < img_height; i++)
      memcpy (cap_image_data2 + i * img_width * 3,
	  cap_image_data + (img_height - 1 - i) * img_width * 3, img_width * 3);

    sprintf (capfilename, "cap%04d.ppm", framenr);
    FILE *outfile = fopen (capfilename, "wb");

    if (outfile != NULL) {
      fprintf (outfile, "P6\n");
      fprintf (outfile, "# created by raw_zb\n");
      fprintf (outfile, "%d %d\n", img_width, img_height);
      fprintf (outfile, "255\n");
      fwrite (cap_image_data2, sizeof (char), img_width * img_height * 3,
	  outfile);
      fclose (outfile);
    }
    framenr++;
  }

  glXSwapBuffers (xinfo->dpy, xinfo->win);
}

void
gst_gl_rgbimage_free_image (GstImageData * image)
{
  GstGLImage *im = (GstGLImage *) image;

  g_warning
      ("gst_gl_rgbimage_free_image doesn't do anything yet -> freeing image\n");
  g_free (im->data.data);
  g_free (im);
}

/* Creates an OpenGL texture to upload the picture over */
static void
gst_gl_rgbimage_open_conn (GstImageConnection * conn, GstImageInfo * info)
{
  g_warning ("Opening RGB Connection; classic OpenGL 1.2 renderer.");

  //GstGLImageInfo *xinfo = gst_gl_rgbimage_info (info);  
  GstGLImageConnection *xconn = gst_gl_rgbimage_connection (conn);

  glGenTextures (1, &xconn->rgbatex_id);
  glBindTexture (GL_TEXTURE_2D, xconn->rgbatex_id);
  glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, TEX_XSIZE, TEX_YSIZE, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, NULL);
}

/* Deletes the creates OpenGL textures */
static void
gst_gl_rgbimage_close_conn (GstImageConnection * conn, GstImageInfo * info)
{
  GstGLImageConnection *xconn = gst_gl_rgbimage_connection (conn);

  //GstGLImageInfo *xinfo = gst_gl_rgbimage_info (info);  

  glDeleteTextures (1, &xconn->rgbatex_id);
}

static void
gst_gl_rgbimage_free_conn (GstImageConnection * conn)
{
  GstGLImageConnection *xconn = gst_gl_rgbimage_connection (conn);

  g_assert (xconn != NULL);

  g_free (xconn);
}
