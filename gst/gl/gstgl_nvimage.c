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


#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>
//#include <GL/glext.h>
#include <GL/glu.h>
#include <string.h>
#include <math.h>

// too lazy to write an API for this ;) 
#include "regcomb_yuvrgb.c"

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
  int slot;                     // < AGP_BUFSLOTS: allocated from AGP mem, otherwise from CPU mem
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
  //int ver, rel, req, ev, err;
  printf ("Checking NVidia OpenGL extensions.\n");
  if (!GL_ARB_multitexture_Init ())
    return FALSE;
  if (!GL_EXT_paletted_texture_Init ())
    return FALSE;
  if (!GL_NV_register_combiners_Init ())
    return FALSE;

#if 0
  if (display == NULL)
    return FALSE;
  if (Success == XvQueryExtension (display, &ver, &rel, &req, &ev, &err))
    return TRUE;
#endif

  return TRUE;
}

static GstCaps *
gst_gl_nvimage_get_caps (GstImageInfo * info)
{
  //gint i;
  //int adaptors;
  //int formats;
  GstCaps *caps = NULL;
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);

  g_warning ("nvimage get caps called, context %p !\n",
      glXGetCurrentContext ());
  /* we don't handle these image information */
  if (xinfo == NULL) {
    printf ("Invalid XInfo struct !\n");
    return NULL;
  }

  if (gst_gl_nvimage_check_xvideo () == FALSE) {
    g_warning ("GL_NVImage: Server has no NVidia extension support\n");
    return NULL;
  }

  caps = gst_caps_append (caps, GST_CAPS_NEW ("nvimage_caps",
          "video/x-raw-yuv",
          "format", GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y', 'V', '1', '2')),
          "width", GST_PROPS_INT_RANGE (0, 1024),
          "height", GST_PROPS_INT_RANGE (0, 1024))
      );
  g_warning ("nvimage returns caps !\n");
  return caps;
}

static GstImageConnection *
gst_gl_nvimage_set_caps (GstImageInfo * info, GstCaps * caps)
{
  //gint i, j = 0;
  //int adaptors;
  //int formats;
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
  if (format != GST_MAKE_FOURCC ('Y', 'V', '1', '2')) {
    GST_DEBUG ("GL_NVImage: Format is invalid !\n");
    return NULL;
  }
  if (0)                        //conn->port == (XvPortID) -1)
  {
    /* this happens if the plugin can't handle the caps, so no warning */
    g_free (conn);
    return NULL;
  }

  GST_DEBUG ("GL_NVImage: caps %p are ok, creating image", caps);
  return (GstImageConnection *) conn;
}

static GstImageData *
gst_gl_nvimage_get_image (GstImageInfo * info, GstImageConnection * conn)
{
  GstNvImage *image;
  GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);
  GstGLImageConnection *nvconn = gst_gl_nvimage_connection (conn);

  /* checks */
  if (xinfo == NULL)
    return NULL;
  if (nvconn == NULL)
    return NULL;

  // I should also check the current GLX context ! 
  // Ah, Don't have to, I am guarantueed to be in the same thread as put_image

  image = g_new0 (GstNvImage, 1);

  image->data.size = nvconn->w * nvconn->h * 3 / 2;

  //g_warning("Allocating %d bytes from main memory !", image->data.size);
  image->data.data = g_malloc (image->data.size);
  //image->slot = AGP_BUFSLOTS; // no AGP slot

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

  int img_width = im->conn->w;
  int img_height = im->conn->h;
  int uv_width = img_width >> 1;
  int uv_height = img_height >> 1;

  unsigned char *buf_y = im->data.data;
  unsigned char *buf_v = (buf_y + img_width * img_height);
  unsigned char *buf_u = buf_v + ((img_width / 2) * (img_height / 2));

  /* checks omitted for speed (and lazyness), do we need them? */
  g_assert (xinfo != NULL);

  // both upload the video, and redraw the screen
  //glClearColor(0,0.5, 0.3,1.0); // a test color
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  glTranslatef (0.0, 0.0, -5.0);
  glDisable (GL_TEXTURE_2D);

  if (xinfo->info.demo) {
    //g_print("Putting image, context is %p\n", glXGetCurrentContext());

    glTranslatef (0.0, 0.0, -5.0);      // make it avoid the clipping plane, zoom 2.0 instead
    glRotatef (180.0 * sin (xinfo->rotX), 1, 0, 0);
    glRotatef (180.0 * cos (xinfo->rotY), 0, 1, 0);

    xinfo->rotX += 0.01;
    xinfo->rotY -= 0.015;
    float zoom = xinfo->zoom;

    glScalef (zoom, zoom, zoom);
    //glScalef(0.1,0.1,0.1); 

    if (xinfo->zoom > 2.0)
      xinfo->zoomdir = -0.01;

    if (xinfo->zoom < 1.0)
      xinfo->zoomdir = 0.01;

    xinfo->zoom += xinfo->zoomdir;
  }
  //Draws the surface rectangle

  if (Ywidth != im->conn->w || Yheight != im->conn->h) {
    Ywidth = im->conn->w;
    Yheight = im->conn->h;
    UVwidth = im->conn->w / 2;
    UVheight = im->conn->h / 2;
    Initialize_Backend (Ywidth, Yheight, UVwidth, UVheight, GL_LINEAR);
  }

  LoadYUVPlanes (Yhandle, Uhandle, Vhandle, img_width, img_height, uv_width,
      uv_height, buf_y, buf_u, buf_v);
  float xmax = (float) (im->conn->w - 1) / tex_xsize;
  float ymax = (float) (im->conn->h - 1) / tex_ysize;

  /* Upload the texture here */
  //g_warning("PUTTING IMAGE %f %f %d %d\n", xmax, ymax, tex_xsize, tex_ysize);

  //glColor4f(1,1,1,1); // do NOT set a color here ! Done by Initialize_Backend, or actually SetConst !
  glBegin (GL_QUADS);

  float aspect = img_width / (float) img_height;
  float hor = aspect;

  //g_print("Drawing vertices, context is %p\n", glXGetCurrentContext());
  glNormal3f (0, -1, 0);
  glMultiTexCoord2fARB (GL_TEXTURE0_ARB, 0, 0);
  glMultiTexCoord2fARB (GL_TEXTURE1_ARB, 0, 0);
  glMultiTexCoord2fARB (GL_TEXTURE2_ARB, 0, 0);
  glVertex3f (-hor, 1, 0);

  glMultiTexCoord2fARB (GL_TEXTURE0_ARB, 0, ymax);
  glMultiTexCoord2fARB (GL_TEXTURE1_ARB, 0, ymax);
  glMultiTexCoord2fARB (GL_TEXTURE2_ARB, 0, ymax);
  glVertex3f (-hor, -1, 0);

  glMultiTexCoord2fARB (GL_TEXTURE0_ARB, xmax, ymax);
  glMultiTexCoord2fARB (GL_TEXTURE1_ARB, xmax, ymax);
  glMultiTexCoord2fARB (GL_TEXTURE2_ARB, xmax, ymax);
  glVertex3f (hor, -1, 0);

  glMultiTexCoord2fARB (GL_TEXTURE0_ARB, xmax, 0);
  glMultiTexCoord2fARB (GL_TEXTURE1_ARB, xmax, 0);
  glMultiTexCoord2fARB (GL_TEXTURE2_ARB, xmax, 0);
  glVertex3f (hor, 1, 0);

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
      fprintf (outfile, "# created by glsink from GStreamer\n");
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
  //GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);  
  //GstGLImageConnection *xconn = gst_gl_nvimage_connection (conn);

  g_print
      ("Opening NVidia connection; OpenGL on Nvidia, using register combiners.\n");
  {
    Ywidth = TEX_XSIZE;
    Yheight = TEX_YSIZE;
    UVwidth = TEX_XSIZE / 2;
    UVheight = TEX_YSIZE / 2;
    Initialize_Backend (Ywidth, Yheight, UVwidth, UVheight, GL_LINEAR);
  }
  g_print ("Done\n");
}

static void
gst_gl_nvimage_close_conn (GstImageConnection * conn, GstImageInfo * info)
{
  GstGLImageConnection *xconn = gst_gl_nvimage_connection (conn);

  //GstGLImageInfo *xinfo = gst_gl_nvimage_info (info);  

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
