/* This stores the common OpenGL initialization stuff for all instances */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* gcc -ansi -pedantic on GNU/Linux causes warnings and errors
 * unless this is defined:
 * warning: #warning "Files using this header must be compiled with _SVID_SOURCE or _XOPEN_SOURCE"
 */
#ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 1
#endif

#include <X11/Xlib.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include "gstglsink.h"
#include <string.h>		/* strncmp */

/* attributes for a single buffered visual in RGBA format with at least
 * 4 bits per color and a 16 bit depth buffer */
static int attrListSgl[] = {
  GLX_RGBA, GLX_RED_SIZE, 4,
  GLX_GREEN_SIZE, 4,
  GLX_BLUE_SIZE, 4,
  GLX_DEPTH_SIZE, 16,
  None
};

/* attributes for a double buffered visual in RGBA format with at least
 * 4 bits per color and a 16 bit depth buffer */
static int attrListDbl[] = {
  GLX_RGBA, GLX_DOUBLEBUFFER,
  GLX_RED_SIZE, 4,
  GLX_GREEN_SIZE, 4,
  GLX_BLUE_SIZE, 4,
  GLX_DEPTH_SIZE, 16,
  None
};


GLfloat LightAmbient[] = { 0.1, 0.1, 0.1, 1.0 };	/* reddish ambient light  */
GLfloat LightDiffuse[] = { 0.6, 0.6, 0.6, 1.0 };	/* bluish  diffuse light. */
GLfloat LightPosition[] = { 1.5, 1.5, 1.5, 0.0 };	/* position */


void
gst_glxwindow_unhook_context (GstImageInfo * info)
{
  GstGLImageInfo *window = (GstGLImageInfo *) info;

  if (window->ctx) {
    if (!glXMakeCurrent (window->dpy, None, NULL)) {
      printf ("Could not release drawing context.\n");
    } else
      printf ("Released drawing context.\n");
  }
}

void
gst_glxwindow_hook_context (GstImageInfo * info)
{
  GstGLImageInfo *window = (GstGLImageInfo *) info;

  if (window->ctx && window->win && window->ctx) {
    if (!glXMakeCurrent (window->dpy, window->win, window->ctx)) {
      printf ("Could not acquire GLX drawing context.\n");
    } else
      printf ("Acquired drawing context.\n");
  }
}

static void
gst_glxwindow_free (GstImageInfo * info)
{
  GstGLImageInfo *window = (GstGLImageInfo *) info;

  g_signal_handler_disconnect (window->sink, window->handler_id);

  if (window->ctx) {
    if (!glXMakeCurrent (window->dpy, None, NULL)) {
      printf ("Could not release drawing context.\n");
    }
    glXDestroyContext (window->dpy, window->ctx);
    window->ctx = NULL;
  }
#if 0
  /* switch back to original desktop resolution if we were in fs */
  if (GLWin.fs) {
    XF86VidModeSwitchToMode (GLWin.dpy, GLWin.screen, &GLWin.deskMode);
    XF86VidModeSetViewPort (GLWin.dpy, GLWin.screen, 0, 0);
  }
#endif
  XCloseDisplay (window->dpy);
  g_free (window);
}

static void
gst_glxwindow_callback (GObject * object, GParamSpec * pspec,
    GstGLImageInfo * data)
{
  XWindowAttributes attr;

  XGetWindowAttributes (data->dpy, data->win, &attr);

  if (strncmp (pspec->name, "width", 5) == 0
      || strncmp (pspec->name, "height", 6) == 0) {
    gint w = 0;
    gint h = 0;

    g_object_get (object, "width", &w, NULL);
    g_object_get (object, "height", &h, NULL);
    if (w != attr.width || h != attr.height) {
      attr.width = w;
      attr.height = h;
      XResizeWindow (data->dpy, data->win, attr.width, attr.height);
      XMapRaised (data->dpy, data->win);

      // resize OpenGL
      g_warning ("resizing in OpenGL");
      glViewport (0, 0, (GLint) w, (GLint) h);
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();

      GLfloat aspect = (GLfloat) h / (GLfloat) w;

      glFrustum (-1.0, 1.0, -aspect, aspect, 5.0, 500.0);
    }
  }
  if (attr.width != data->width || attr.height != data->height) {
    data->width = attr.width;
    data->height = attr.height;
  }

}

void
gst_glxwindow_new (GstElement * sink)
{
  //XGCValues values;
  GstGLImageInfo *new;
  int glxMajorVersion, glxMinorVersion;

  //XSetWindowAttributes attrib;
  XVisualInfo *vi;
  Atom wmDelete;
  Window winDummy;
  unsigned int borderDummy;
  Colormap cmap;
  char *title = "GLSink (experimental)";

  new = g_new0 (GstGLImageInfo, 1);

  if (sink == NULL) {
    sink = gst_element_factory_make ("glsink", "glsink");
    g_assert (sink != NULL);
  }

  /* fill in the ImageInfo */
  new->info.id = GST_MAKE_FOURCC ('X', 'l', 'i', 'b');
  new->info.free_info = gst_glxwindow_free;

  new->dpy = XOpenDisplay (NULL);
  if (!new->dpy) {
    g_warning ("open display failed!\n");
    g_free (new);
    return;
  }
  new->screen = DefaultScreen (new->dpy);
  /* get an appropriate visual */
  vi = glXChooseVisual (new->dpy, new->screen, attrListDbl);
  if (vi == NULL) {
    vi = glXChooseVisual (new->dpy, new->screen, attrListSgl);
    GST_DEBUG ("Only Singlebuffered Visual!\n");
  } else {
    GST_DEBUG ("Got Doublebuffered Visual!\n");
  }
  glXQueryVersion (new->dpy, &glxMajorVersion, &glxMinorVersion);
  GST_DEBUG ("glX-Version %d.%d\n", glxMajorVersion, glxMinorVersion);

  /* create a GLX context */
  new->ctx = glXCreateContext (new->dpy, vi, 0, GL_TRUE);
  /* create a color map */
  cmap = XCreateColormap (new->dpy, RootWindow (new->dpy, vi->screen),
      vi->visual, AllocNone);
  new->attr.colormap = cmap;
  new->attr.border_pixel = 0;

  /* set sizes */
  new->x = 0;
  new->y = 0;
  new->width = 10;
  new->height = 10;

  new->rotX = 0;
  new->rotY = 0;
  new->zoom = 1;
  new->zoomdir = 0.01;

  {
    /* create a window in window mode */
    new->attr.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
	StructureNotifyMask;
    new->win = XCreateWindow (new->dpy, RootWindow (new->dpy, vi->screen),
	new->x, new->y, new->width, new->height, 0, vi->depth, InputOutput,
	vi->visual, CWBorderPixel | CWColormap | CWEventMask, &new->attr);
    if (!new->win) {
      g_warning ("create window failed\n");
      g_free (new);
      return;
    }
    /* only set window title and handle wm_delete_events if in windowed mode */
    wmDelete = XInternAtom (new->dpy, "WM_DELETE_WINDOW", True);
    XSetWMProtocols (new->dpy, new->win, &wmDelete, 1);
    XSetStandardProperties (new->dpy, new->win, title,
	title, None, NULL, 0, NULL);
    XMapRaised (new->dpy, new->win);
  }
  /* connect the glx-context to the window */
  glXMakeCurrent (new->dpy, new->win, new->ctx);
  XGetGeometry (new->dpy, new->win, &winDummy, &new->x, &new->y,
      &new->width, &new->height, &borderDummy, &new->depth);
  printf ("Depth %d\n", new->depth);
  if (glXIsDirect (new->dpy, new->ctx))
    GST_DEBUG ("Congrats, you have Direct Rendering!\n");
  else
    GST_DEBUG ("Sorry, no Direct Rendering possible!\n");

  g_warning ("Initializing OpenGL parameters\n");
  /* initialize OpenGL drawing */
  glEnable (GL_DEPTH_TEST);
  //glShadeModel(GL_SMOOTH);

  glEnable (GL_TEXTURE_2D);
  glDisable (GL_CULL_FACE);
  glClearDepth (1.0f);
  glClearColor (0, 0, 0, 0);

  glLightfv (GL_LIGHT0, GL_AMBIENT, LightAmbient);	/*  add lighting. (ambient) */
  glLightfv (GL_LIGHT0, GL_DIFFUSE, LightDiffuse);	/*  add lighting. (diffuse). */
  glLightfv (GL_LIGHT0, GL_POSITION, LightPosition);	/*  set light position. */

  //glEnable(GL_LIGHT0);                                        // Quick And Dirty Lighting (Assumes Light0 Is Set Up)
  //glEnable(GL_LIGHTING);                                      // Enable Lighting
  glDisable (GL_COLOR_MATERIAL);	// Enable Material Coloring
  glEnable (GL_AUTO_NORMAL);	// let OpenGL generate the Normals

  glDisable (GL_BLEND);

  glPolygonMode (GL_FRONT, GL_FILL);
  glPolygonMode (GL_BACK, GL_FILL);

  glShadeModel (GL_SMOOTH);
  glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

  XSelectInput (new->dpy, new->win, ExposureMask | StructureNotifyMask);

  g_object_set (sink, "hook", new, NULL);
  new->sink = sink;
  new->handler_id =
      g_signal_connect (sink, "notify", G_CALLBACK (gst_glxwindow_callback),
      new);
}
