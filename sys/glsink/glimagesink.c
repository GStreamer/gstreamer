/* GStreamer
 * Copyright (C) <2003> Julien Moutte <julien@moutte.net>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

/* Our interfaces */
#include <gst/navigation/navigation.h>
#include <gst/xoverlay/xoverlay.h>

/* Object header */
#include "glimagesink.h"

/* Debugging category */
#include <gst/gstinfo.h>
GST_DEBUG_CATEGORY_STATIC (gst_debug_glimagesink);
#define GST_CAT_DEFAULT gst_debug_glimagesink

static void gst_glimagesink_buffer_free (GstBuffer * buffer);

/* ElementFactory information */
static GstElementDetails gst_glimagesink_details =
GST_ELEMENT_DETAILS ("Video sink",
    "Sink/Video",
    "An OpenGL 1.2 based videosink",
    "Gernot Ziegler <gz@lysator.liu.se>, Julien Moutte <julien@moutte.net>");

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
static GstStaticPadTemplate gst_glimagesink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb, "
        "framerate = (double) [ 1.0, 100.0 ], "
        "width = (int) [ 1, MAX ], "
        "height = (int) [ 1, MAX ]; "
        "video/x-raw-yuv, "
        "framerate = (double) [ 1.0, 100.0 ], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

/* GLImageSink signals and args */
enum
{
  SIGNAL_HANDOFF,
  SIGNAL_BUFALLOC,
  LAST_SIGNAL
      /* FILL ME */
};

static guint gst_glimagesink_signals[LAST_SIGNAL] = { 0 };

enum
{
  ARG_0,
  ARG_DISPLAY,
  ARG_SYNCHRONOUS,
  ARG_SIGNAL_HANDOFFS
      /* FILL ME */
};

static GstVideoSinkClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* X11 and GLX stuff */

#define TEX_XSIZE 1024
#define TEX_YSIZE 1024

/* This function handles GstGLImage creation 
   it creates data buffers and corresponding texture IDs */
static GstGLImage *
gst_glimagesink_ximage_new (GstGLImageSink * glimagesink, gint width,
    gint height)
{
  GstGLImage *ximage = NULL;

  g_return_val_if_fail (GST_IS_GLIMAGESINK (glimagesink), NULL);

  ximage = g_new0 (GstGLImage, 1);

  ximage->width = width;
  ximage->height = height;
  ximage->data = NULL;
  ximage->glimagesink = glimagesink;

  g_mutex_lock (glimagesink->x_lock);

  ximage->size =
      (glimagesink->xcontext->bpp / 8) * ximage->width * ximage->height;

  printf ("No ximage_new yet !\n");

  {
    ximage->data = g_malloc (ximage->size);

    ximage->texid = 1000;
  }

  if (0)                        // can't fail ! 
  {
    if (ximage->data)
      g_free (ximage->data);

    g_free (ximage);
    //ximage = NULL;
  }

  g_mutex_unlock (glimagesink->x_lock);

  return ximage;
}

/* This function destroys a GstGLImage handling XShm availability */
static void
gst_glimagesink_ximage_destroy (GstGLImageSink * glimagesink,
    GstGLImage * ximage)
{
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  /* If the destroyed image is the current one we destroy our reference too */
  if (glimagesink->cur_image == ximage)
    glimagesink->cur_image = NULL;

  printf ("No ximage_destroy implemented yet !\n");

  g_mutex_lock (glimagesink->x_lock);

  {
    //if (ximage->ximage) // FIXME: doesnt exist - dealloc textures
    //  XDestroyImage (ximage->ximage);
  }

  g_mutex_unlock (glimagesink->x_lock);

  g_free (ximage);
}

/* This function puts a GstGLImage on a GstGLImagesink's window */
static void
gst_glimagesink_ximage_put (GstGLImageSink * glimagesink, GstGLImage * ximage)
{
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  if (glimagesink->signal_handoffs) {
    g_warning ("Not drawing anything due to signal_handoffs !\n");
    return;
  }

  /* Store a reference to the last image we put */
  if (glimagesink->cur_image != ximage)
    glimagesink->cur_image = ximage;

  g_mutex_lock (glimagesink->x_lock);

  // both upload the video, and redraw the screen

  //printf("No ximage_put yet !\n");

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  //glTranslatef(0.0, 0.0, -5.0);

  glEnable (GL_TEXTURE_2D);

  glBindTexture (GL_TEXTURE_2D, ximage->texid);
  glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, ximage->width, ximage->height,
      GL_RGB, GL_UNSIGNED_BYTE, ximage->data);

  float xmax = (float) ximage->width / TEX_XSIZE;
  float ymax = (float) ximage->height / TEX_YSIZE;

  //float aspect = ximage->width/(float)ximage->height;

  // don't know what to do with pixel aspect yet.
  //float pixel_aspect = glimagesink->pixel_width/(float)glimagesink->pixel_height;

  //if (aspect != pixel_aspect)
  //  g_warning("screen aspect %f differs from pixel_aspect %f !", aspect, pixel_aspect);

  glColor4f (1, 1, 1, 1);
  glBegin (GL_QUADS);

  glNormal3f (0, -1, 0);

  glTexCoord2f (xmax, 0);
  glVertex3f (1, 1, 0);

  glTexCoord2f (0, 0);
  glVertex3f (-1, 1, 0);

  glTexCoord2f (0, ymax);
  glVertex3f (-1, -1, 0);

  glTexCoord2f (xmax, ymax);
  glVertex3f (1, -1, 0);
  glEnd ();

#if 1                           // for pointer feedback, later
  glDisable (GL_TEXTURE_2D);
  if (glimagesink->pointer_moved)
    glColor3f (1, 1, 1);
  else
    glColor3f (1, 0, 1);

  if (glimagesink->pointer_button[0])
    glColor3f (1, 0, 0);

  float px = 2 * glimagesink->pointer_x / (float) ximage->width - 1.0;
  float py = 2 * glimagesink->pointer_y / (float) ximage->height - 1.0;

  glPointSize (10);
  glBegin (GL_POINTS);
  glVertex2f (px, -py);
  glEnd ();
#endif

  glXSwapBuffers (glimagesink->xcontext->disp, glimagesink->window->win);

  g_mutex_unlock (glimagesink->x_lock);
}

/* This function handles a GstXWindow creation */
static GstGLWindow *
gst_glimagesink_xwindow_new (GstGLImageSink * glimagesink, gint width,
    gint height)
{
  GstGLWindow *xwindow = NULL;
  GstXContext *xcontext = glimagesink->xcontext;

  if (glimagesink->signal_handoffs) {
    g_warning ("NOT CREATING any window due to signal_handoffs !\n");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_GLIMAGESINK (glimagesink), NULL);

  xwindow = g_new0 (GstGLWindow, 1);

  xwindow->width = width;
  xwindow->height = height;
  xwindow->internal = TRUE;

  g_mutex_lock (glimagesink->x_lock);

  Colormap cmap;

  /* create a color map */
  cmap =
      XCreateColormap (xcontext->disp, RootWindow (xcontext->disp,
          xcontext->visualinfo->screen), xcontext->visualinfo->visual,
      AllocNone);
  xwindow->attr.colormap = cmap;
  xwindow->attr.border_pixel = 0;

#if 0
  /* set sizes */
  xwindow->x = 0;
  xwindow->y = 0;
  xwindow->width = 10;
  xwindow->height = 10;

  xwindow->rotX = 0;
  xwindow->rotY = 0;
  xwindow->zoom = 1;
  xwindow->zoomdir = 0.01;
#endif

  xwindow->attr.event_mask =
      ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask;

  /* create a window in window mode */
  xwindow->win =
      XCreateWindow (xcontext->disp, /*xcontext->root, */
      RootWindow (xcontext->disp, xcontext->visualinfo->screen),
      0, 0, xwindow->width, xwindow->height, 0, xcontext->visualinfo->depth,
      InputOutput, xcontext->visualinfo->visual,
      CWBorderPixel | CWColormap | CWEventMask, &xwindow->attr);

  /* only set window title and handle wm_delete_events if in windowed mode */
  Atom wmDelete = XInternAtom (xcontext->disp, "WM_DELETE_WINDOW", True);

  XSetWMProtocols (xcontext->disp, xwindow->win, &wmDelete, 1);
  XSetStandardProperties (xcontext->disp, xwindow->win, "glsink",
      "glsink", None, NULL, 0, NULL);

#if 0
  XSelectInput (xcontext->disp, xwindow->win,
      ExposureMask | StructureNotifyMask);
#else // we want more than that
  XSelectInput (glimagesink->xcontext->disp, xwindow->win, ExposureMask |
      StructureNotifyMask | PointerMotionMask | KeyPressMask |
      KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
#endif

  //xwindow->win = XCreateSimpleWindow (glimagesink->xcontext->disp,
  //    glimagesink->xcontext->root,
  //    0, 0, xwindow->width, xwindow->height, 0, 0, glimagesink->xcontext->black);

  XMapRaised (glimagesink->xcontext->disp, xwindow->win);

  /* connect the glx-context to the window */
  glXMakeCurrent (xcontext->disp, xwindow->win, xcontext->glx);

  printf ("Initializing OpenGL parameters\n");
  /* initialize OpenGL drawing */
  glDisable (GL_DEPTH_TEST);
  //glShadeModel(GL_SMOOTH);

  glDisable (GL_TEXTURE_2D);
  glDisable (GL_CULL_FACE);
  glClearDepth (1.0f);
  glClearColor (0, 0.5, 0, 1);

  // both upload the video, and redraw the screen
  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


  //glEnable(GL_LIGHT0);                                        // Quick And Dirty Lighting (Assumes Light0 Is Set Up)
  //glEnable(GL_LIGHTING);                                      // Enable Lighting
  glDisable (GL_COLOR_MATERIAL);        // Enable Material Coloring
  glEnable (GL_AUTO_NORMAL);    // let OpenGL generate the Normals

  glDisable (GL_BLEND);

  glPolygonMode (GL_FRONT, GL_FILL);
  glPolygonMode (GL_BACK, GL_FILL);

  glShadeModel (GL_SMOOTH);
  glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

  glBindTexture (GL_TEXTURE_2D, 1000);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, TEX_XSIZE, TEX_YSIZE, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, NULL);

  glXSwapBuffers (xcontext->disp, xwindow->win);

  g_mutex_unlock (glimagesink->x_lock);

  gst_x_overlay_got_xwindow_id (GST_X_OVERLAY (glimagesink), xwindow->win);

  return xwindow;
}

/* This function destroys a GstGLWindow */
static void
gst_glimagesink_xwindow_destroy (GstGLImageSink * glimagesink,
    GstGLWindow * xwindow)
{
  GstXContext *xcontext = glimagesink->xcontext;

  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  g_mutex_lock (glimagesink->x_lock);

  if (glimagesink->signal_handoffs) {
    g_warning ("NOT DESTROYING any window due to signal_handoff !\n");
    return;
  }

  if (xcontext->glx) {
    if (!glXMakeCurrent (xcontext->disp, None, NULL)) {
      printf ("Could not release drawing context.\n");
    }
    glXDestroyContext (xcontext->disp, xcontext->glx);
    xcontext->glx = NULL;
  }
#if 0                           // not used: prepared for fs mode
  /* switch back to original desktop resolution if we were in fs */
  if (GLWin.fs) {
    XF86VidModeSwitchToMode (GLWin.dpy, GLWin.screen, &GLWin.deskMode);
    XF86VidModeSetViewPort (GLWin.dpy, GLWin.screen, 0, 0);
  }
#endif

  /* If we did not create that window we just free the GC and let it live */
  if (xwindow->internal)
    XDestroyWindow (glimagesink->xcontext->disp, xwindow->win);
  else
    XSelectInput (glimagesink->xcontext->disp, xwindow->win, 0);

  printf ("Check Xwindow destroy !\n");

  g_mutex_unlock (glimagesink->x_lock);

  g_free (xwindow);
}

/* This function resizes a GstGLWindow */
static void
gst_glimagesink_xwindow_resize (GstGLImageSink * glimagesink,
    GstGLWindow * xwindow, guint width, guint height)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  g_mutex_lock (glimagesink->x_lock);

  xwindow->width = width;
  xwindow->height = height;

  XResizeWindow (glimagesink->xcontext->disp, xwindow->win,
      xwindow->width, xwindow->height);

  printf ("No xwindow resize implemented yet !\n");

  g_mutex_unlock (glimagesink->x_lock);
}

static void
gst_glimagesink_xwindow_update_geometry (GstGLImageSink * glimagesink,
    GstGLWindow * xwindow)
{
  XWindowAttributes attr;

  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  /* Update the window geometry */
  g_mutex_lock (glimagesink->x_lock);
  XGetWindowAttributes (glimagesink->xcontext->disp,
      glimagesink->window->win, &attr);
  g_mutex_unlock (glimagesink->x_lock);

  // FIXME: Need to introduce OpenGL setup here if PROJECTION_MATRIX depends on width/height
  //printf("No update geometry implemented yet !\n");

  glimagesink->window->width = attr.width;
  glimagesink->window->height = attr.height;
}

#if 0
static void
gst_glimagesink_renegotiate_size (GstGLImageSink * glimagesink)
{
  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  if (!glimagesink->window)
    return;

  gst_glimagesink_xwindow_update_geometry (glimagesink, glimagesink->window);

  if (glimagesink->window->width <= 1 || glimagesink->window->height <= 1)
    return;

  if (GST_PAD_IS_NEGOTIATING (GST_VIDEOSINK_PAD (glimagesink)) ||
      !gst_pad_is_negotiated (GST_VIDEOSINK_PAD (glimagesink)))
    return;

  /* Window got resized or moved. We do caps negotiation again to get video
     scaler to fit that new size only if size of the window differs from our
     size. */

  if (GST_VIDEOSINK_WIDTH (glimagesink) != glimagesink->window->width ||
      GST_VIDEOSINK_HEIGHT (glimagesink) != glimagesink->window->height) {
    GstPadLinkReturn r;

    r = gst_pad_try_set_caps (GST_VIDEOSINK_PAD (glimagesink),
        gst_caps_new_simple ("video/x-raw-rgb",
            "bpp", G_TYPE_INT, glimagesink->xcontext->bpp,
            "depth", G_TYPE_INT, glimagesink->xcontext->depth,
            "endianness", G_TYPE_INT, glimagesink->xcontext->endianness,
            "red_mask", G_TYPE_INT, glimagesink->xcontext->visual->red_mask,
            "green_mask", G_TYPE_INT, glimagesink->xcontext->visual->green_mask,
            "blue_mask", G_TYPE_INT, glimagesink->xcontext->visual->blue_mask,
            "width", G_TYPE_INT, glimagesink->window->width,
            "height", G_TYPE_INT, glimagesink->window->height,
            "framerate", G_TYPE_DOUBLE, glimagesink->framerate, NULL));

    if ((r == GST_PAD_LINK_OK) || (r == GST_PAD_LINK_DONE)) {
      /* Renegotiation succeeded, we update our size and image */
      GST_VIDEOSINK_WIDTH (glimagesink) = glimagesink->window->width;
      GST_VIDEOSINK_HEIGHT (glimagesink) = glimagesink->window->height;

      if ((glimagesink->glimage) &&
          ((GST_VIDEOSINK_WIDTH (glimagesink) != glimagesink->glimage->width) ||
              (GST_VIDEOSINK_HEIGHT (glimagesink) !=
                  glimagesink->glimage->height))) {
        /* We renew our ximage only if size changed */
        gst_glimagesink_ximage_destroy (glimagesink, glimagesink->glimage);

        glimagesink->glimage = gst_glimagesink_ximage_new (glimagesink,
            GST_VIDEOSINK_WIDTH (glimagesink),
            GST_VIDEOSINK_HEIGHT (glimagesink));
      }
    }
  }
}
#endif

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_glimagesink_handle_xevents (GstGLImageSink * glimagesink, GstPad * pad)
{
  XEvent e;

  glimagesink->pointer_moved = FALSE;

  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  //printf("handling xevents\n");

  //gst_glimagesink_renegotiate_size (glimagesink);

  /* Then we get all pointer motion events, only the last position is
     interesting. */
  g_mutex_lock (glimagesink->x_lock);
  while (XCheckWindowEvent (glimagesink->xcontext->disp,
          glimagesink->window->win, PointerMotionMask, &e)) {
    g_mutex_unlock (glimagesink->x_lock);

    switch (e.type) {
      case MotionNotify:
        glimagesink->pointer_x = e.xmotion.x;
        glimagesink->pointer_y = e.xmotion.y;
        glimagesink->pointer_moved = TRUE;
        break;
      default:
        break;
    }

    g_mutex_lock (glimagesink->x_lock);
  }
  g_mutex_unlock (glimagesink->x_lock);

  if (glimagesink->pointer_moved) {
    GST_DEBUG ("glimagesink pointer moved over window at %d,%d",
        glimagesink->pointer_x, glimagesink->pointer_y);
    gst_navigation_send_mouse_event (GST_NAVIGATION (glimagesink),
        "mouse-move", 0, glimagesink->pointer_x, glimagesink->pointer_y);
  }

  /* We get all remaining events on our window to throw them upstream */
  g_mutex_lock (glimagesink->x_lock);
  while (XCheckWindowEvent (glimagesink->xcontext->disp,
          glimagesink->window->win,
          KeyPressMask | KeyReleaseMask |
          ButtonPressMask | ButtonReleaseMask, &e)) {
    KeySym keysym;

    /* We lock only for the X function call */
    g_mutex_unlock (glimagesink->x_lock);

    switch (e.type) {
      case ButtonPress:
        /* Mouse button pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("glimagesink button %d pressed over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        glimagesink->pointer_button[e.xbutton.button - Button1] = TRUE;
        gst_navigation_send_mouse_event (GST_NAVIGATION (glimagesink),
            "mouse-button-press", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case ButtonRelease:
        GST_DEBUG ("glimagesink button %d release over window at %d,%d",
            e.xbutton.button, e.xbutton.x, e.xbutton.x);
        glimagesink->pointer_button[e.xbutton.button - Button1] = FALSE;
        gst_navigation_send_mouse_event (GST_NAVIGATION (glimagesink),
            "mouse-button-release", e.xbutton.button, e.xbutton.x, e.xbutton.y);
        break;
      case KeyPress:
      case KeyRelease:
        /* Key pressed/released over our window. We send upstream
           events for interactivity/navigation */
        GST_DEBUG ("glimagesink key %d released over window at %d,%d",
            e.xkey.keycode, e.xkey.x, e.xkey.x);
        keysym = XKeycodeToKeysym (glimagesink->xcontext->disp,
            e.xkey.keycode, 0);
        if (keysym != NoSymbol) {
          gst_navigation_send_key_event (GST_NAVIGATION (glimagesink),
              e.type == KeyPress ?
              "key-press" : "key-release", XKeysymToString (keysym));
        } else {
          gst_navigation_send_key_event (GST_NAVIGATION (glimagesink),
              e.type == KeyPress ? "key-press" : "key-release", "unknown");
        }
        break;
      default:
        GST_DEBUG ("glimagesink unhandled X event (%d)", e.type);
    }

    g_mutex_lock (glimagesink->x_lock);
  }
  g_mutex_unlock (glimagesink->x_lock);
}

/* attributes for a single buffered visual in RGBA format with at least
 * 4 bits per color and a 16 bit depth buffer */
static int attrListSingle[] = {
  GLX_RGBA,
  GLX_RED_SIZE, 4,
  GLX_GREEN_SIZE, 4,
  GLX_BLUE_SIZE, 4,
  GLX_DEPTH_SIZE, 16,
  None
};

/* attributes for a double buffered visual in RGBA format with at least
 * 4 bits per color and a 16 bit depth buffer */
static int attrListDouble[] = {
  GLX_RGBA, GLX_DOUBLEBUFFER,
  GLX_RED_SIZE, 4,
  GLX_GREEN_SIZE, 4,
  GLX_BLUE_SIZE, 4,
  GLX_DEPTH_SIZE, 16,
  None
};

/* This function get the X Display and global infos about it. Everything is
   stored in our object and will be cleaned when the object is disposed. Note
   here that caps for supported format are generated without any window or 
   image creation */
static GstXContext *
gst_glimagesink_xcontext_get (GstGLImageSink * glimagesink)
{
  printf ("Acquiring X context\n");

  GstXContext *xcontext = NULL;

  g_return_val_if_fail (GST_IS_GLIMAGESINK (glimagesink), NULL);

  xcontext = g_new0 (GstXContext, 1);

  g_mutex_lock (glimagesink->x_lock);

  xcontext->disp = XOpenDisplay (glimagesink->display_name);

  if (!xcontext->disp) {
    g_mutex_unlock (glimagesink->x_lock);
    g_free (xcontext);
    GST_ELEMENT_ERROR (glimagesink, RESOURCE, TOO_LAZY, (NULL),
        ("Could not open display"));
    return NULL;
  }

  xcontext->screen_num = DefaultScreen (xcontext->disp);
  xcontext->screen = DefaultScreenOfDisplay (xcontext->disp);

  /* get an appropriate visual */
  xcontext->visualinfo =
      glXChooseVisual (xcontext->disp, xcontext->screen_num, attrListDouble);
  if (xcontext->visualinfo == NULL) {
    xcontext->visualinfo =
        glXChooseVisual (xcontext->disp, xcontext->screen_num, attrListSingle);
    GST_DEBUG ("Only Singlebuffered Visual!\n");

    if (xcontext->visualinfo == NULL)
      GST_ELEMENT_ERROR (glimagesink, RESOURCE, TOO_LAZY, (NULL),
          ("Could not open GLX connection"));
  } else {
    GST_DEBUG ("Got Doublebuffered Visual!\n");
  }
  int glxMajorVersion, glxMinorVersion;

  glXQueryVersion (xcontext->disp, &glxMajorVersion, &glxMinorVersion);
  GST_DEBUG ("glX-Version %d.%d\n", glxMajorVersion, glxMinorVersion);

  printf ("Creating GLX context\n");

  /* create a GLX context */
  xcontext->glx =
      glXCreateContext (xcontext->disp, xcontext->visualinfo, 0, GL_TRUE);

  if (glXIsDirect (xcontext->disp, xcontext->glx))
    printf ("Congrats, you have Direct Rendering!\n");
  else
    printf ("Sorry, no Direct Rendering possible!\n");

  xcontext->endianness =
      (ImageByteOrder (xcontext->disp) ==
      LSBFirst) ? G_LITTLE_ENDIAN : G_BIG_ENDIAN;

  xcontext->visual = DefaultVisual (xcontext->disp, xcontext->screen_num);
  xcontext->root = DefaultRootWindow (xcontext->disp);

#if 1
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i;

  xcontext->white = XWhitePixel (xcontext->disp, xcontext->screen_num);
  xcontext->black = XBlackPixel (xcontext->disp, xcontext->screen_num);
  xcontext->depth = DefaultDepthOfScreen (xcontext->screen);

  /* We get supported pixmap formats at supported depth */
  px_formats = XListPixmapFormats (xcontext->disp, &nb_formats);

  if (!px_formats) {
    XCloseDisplay (xcontext->disp);
    g_mutex_unlock (glimagesink->x_lock);
    g_free (xcontext);
    return NULL;
  }

  /* We get bpp value corresponding to our running depth */
  for (i = 0; i < nb_formats; i++) {
    if (px_formats[i].depth == xcontext->depth)
      xcontext->bpp = px_formats[i].bits_per_pixel;
  }

  XFree (px_formats);
#endif

  /* our caps system handles 24/32bpp RGB as big-endian. */
  if ((xcontext->bpp == 24 || xcontext->bpp == 32) &&
      xcontext->endianness == G_LITTLE_ENDIAN) {
    xcontext->endianness = G_BIG_ENDIAN;
    xcontext->visual->red_mask = GUINT32_TO_BE (xcontext->visual->red_mask);
    xcontext->visual->green_mask = GUINT32_TO_BE (xcontext->visual->green_mask);
    xcontext->visual->blue_mask = GUINT32_TO_BE (xcontext->visual->blue_mask);
    if (xcontext->bpp == 24) {
      xcontext->visual->red_mask >>= 8;
      xcontext->visual->green_mask >>= 8;
      xcontext->visual->blue_mask >>= 8;
    }
  }

  xcontext->endianness = G_BIG_ENDIAN;
  xcontext->visual->red_mask = 0xff0000;
  xcontext->visual->green_mask = 0xff00;
  xcontext->visual->blue_mask = 0xff;
  xcontext->bpp = 24;
  xcontext->depth = 24;

  //char yuvformat[4] = {'Y', 'V', '1', '2'};

  if (!glimagesink->signal_handoffs)
    xcontext->caps = gst_caps_new_simple ("video/x-raw-rgb",
        "bpp", G_TYPE_INT, xcontext->bpp,
        "depth", G_TYPE_INT, xcontext->depth,
        "endianness", G_TYPE_INT, xcontext->endianness,
        "red_mask", G_TYPE_INT, xcontext->visual->red_mask,
        "green_mask", G_TYPE_INT, xcontext->visual->green_mask,
        "blue_mask", G_TYPE_INT, xcontext->visual->blue_mask,
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);
  else
    xcontext->caps = gst_caps_new_simple ("video/x-raw-yuv",
        //                                      "format", GST_TYPE_FOURCC, GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12")),
        "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "height", GST_TYPE_INT_RANGE, 1, G_MAXINT,
        "framerate", GST_TYPE_DOUBLE_RANGE, 1.0, 100.0, NULL);

  g_mutex_unlock (glimagesink->x_lock);

  return xcontext;
}

/* This function cleans the X context. Closing the Display and unrefing the
   caps for supported formats. */
static void
gst_glimagesink_xcontext_clear (GstGLImageSink * glimagesink)
{
  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  gst_caps_free (glimagesink->xcontext->caps);

  g_mutex_lock (glimagesink->x_lock);

  XCloseDisplay (glimagesink->xcontext->disp);

  g_mutex_unlock (glimagesink->x_lock);

  glimagesink->xcontext = NULL;
}

static void
gst_glimagesink_imagepool_clear (GstGLImageSink * glimagesink)
{
  g_mutex_lock (glimagesink->pool_lock);

  while (glimagesink->image_pool) {
    GstGLImage *ximage = glimagesink->image_pool->data;

    glimagesink->image_pool = g_slist_delete_link (glimagesink->image_pool,
        glimagesink->image_pool);
    gst_glimagesink_ximage_destroy (glimagesink, ximage);
  }

  g_mutex_unlock (glimagesink->pool_lock);
}

/* 
=================
Element stuff 
=================
*/

static GstCaps *
gst_glimagesink_fixate (GstPad * pad, const GstCaps * caps)
{
  printf ("Linking the sink\n");

  GstStructure *structure;
  GstCaps *newcaps;

  if (gst_caps_get_size (caps) > 1)
    return NULL;

  newcaps = gst_caps_copy (caps);
  structure = gst_caps_get_structure (newcaps, 0);

  if (gst_caps_structure_fixate_field_nearest_int (structure, "width", 320)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_int (structure, "height", 240)) {
    return newcaps;
  }
  if (gst_caps_structure_fixate_field_nearest_double (structure, "framerate",
          30.0)) {
    return newcaps;
  }

  gst_caps_free (newcaps);
  return NULL;
}

static GstCaps *
gst_glimagesink_getcaps (GstPad * pad)
{
  GstGLImageSink *glimagesink;

  glimagesink = GST_GLIMAGESINK (gst_pad_get_parent (pad));

  if (glimagesink->xcontext)
    return gst_caps_copy (glimagesink->xcontext->caps);

#if 0
  if (!glimagesink->signal_handoffs)
    return gst_caps_from_string ("video/x-raw-rgb, "
        "framerate = (double) [ 1, 100 ], "
        "width = (int) [ 0, MAX ], " "height = (int) [ 0, MAX ]");
  else
#endif
    return gst_caps_from_string ("video/x-raw-yuv, "
        "framerate = (double) [ 1, 100 ], "
        "width = (int) [ 0, MAX ], " "height = (int) [ 0, MAX ]");
}

static GstPadLinkReturn
gst_glimagesink_sink_link (GstPad * pad, const GstCaps * caps)
{
  GstGLImageSink *glimagesink;
  gboolean ret;
  GstStructure *structure;

  glimagesink = GST_GLIMAGESINK (gst_pad_get_parent (pad));

  if (!glimagesink->xcontext)
    return GST_PAD_LINK_DELAYED;

  GST_DEBUG_OBJECT (glimagesink,
      "sinkconnect possible caps %" GST_PTR_FORMAT " with given caps %"
      GST_PTR_FORMAT, glimagesink->xcontext->caps, caps);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width",
      &(GST_VIDEOSINK_WIDTH (glimagesink)));
  ret &= gst_structure_get_int (structure, "height",
      &(GST_VIDEOSINK_HEIGHT (glimagesink)));
  ret &= gst_structure_get_double (structure,
      "framerate", &glimagesink->framerate);
  if (!ret)
    return GST_PAD_LINK_REFUSED;

  glimagesink->pixel_width = 1;
  gst_structure_get_int (structure, "pixel_width", &glimagesink->pixel_width);

  glimagesink->pixel_height = 1;
  gst_structure_get_int (structure, "pixel_height", &glimagesink->pixel_height);

  /* Creating our window and our image */
  if (!glimagesink->window)
    glimagesink->window = gst_glimagesink_xwindow_new (glimagesink,
        GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));
  else {
    if (glimagesink->window->internal)
      gst_glimagesink_xwindow_resize (glimagesink, glimagesink->window,
          GST_VIDEOSINK_WIDTH (glimagesink),
          GST_VIDEOSINK_HEIGHT (glimagesink));
  }

  if ((glimagesink->glimage) && ((GST_VIDEOSINK_WIDTH (glimagesink) != glimagesink->glimage->width) || (GST_VIDEOSINK_HEIGHT (glimagesink) != glimagesink->glimage->height))) { /* We renew our ximage only if size changed */
    gst_glimagesink_ximage_destroy (glimagesink, glimagesink->glimage);

    glimagesink->glimage = gst_glimagesink_ximage_new (glimagesink,
        GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));
  } else if (!glimagesink->glimage)     /* If no ximage, creating one */
    glimagesink->glimage = gst_glimagesink_ximage_new (glimagesink,
        GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));

  gst_x_overlay_got_desired_size (GST_X_OVERLAY (glimagesink),
      GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));

  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_glimagesink_change_state (GstElement * element)
{
  GstGLImageSink *glimagesink;

  printf ("change state\n");

  glimagesink = GST_GLIMAGESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      /* Initializing the XContext */
      if (!glimagesink->xcontext) {
        glimagesink->xcontext = gst_glimagesink_xcontext_get (glimagesink);
        if (!glimagesink->xcontext)
          return GST_STATE_FAILURE;
      }
      printf ("null to ready done\n");
      break;
    case GST_STATE_READY_TO_PAUSED:
      printf ("ready to paused\n");
      //if (glimagesink->window) // not needed with OpenGL
      //  gst_glimagesink_xwindow_clear (glimagesink, glimagesink->window);
      glimagesink->time = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      glimagesink->framerate = 0;
      GST_VIDEOSINK_WIDTH (glimagesink) = 0;
      GST_VIDEOSINK_HEIGHT (glimagesink) = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      if (glimagesink->glimage) {
        gst_glimagesink_ximage_destroy (glimagesink, glimagesink->glimage);
        glimagesink->glimage = NULL;
      }

      if (glimagesink->image_pool)
        gst_glimagesink_imagepool_clear (glimagesink);

      if (glimagesink->window) {
        gst_glimagesink_xwindow_destroy (glimagesink, glimagesink->window);
        glimagesink->window = NULL;
      }

      if (glimagesink->xcontext) {
        gst_glimagesink_xcontext_clear (glimagesink);
        glimagesink->xcontext = NULL;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_glimagesink_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf = GST_BUFFER (data);
  GstGLImageSink *glimagesink;

  //printf("CHAIN CALL\n");

  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  glimagesink = GST_GLIMAGESINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (data)) {
    gst_pad_event_default (pad, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  /* update time */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    glimagesink->time = GST_BUFFER_TIMESTAMP (buf);
  }

  if (glimagesink->signal_handoffs)
    g_signal_emit (G_OBJECT (glimagesink),
        gst_glimagesink_signals[SIGNAL_HANDOFF], 0, buf, pad);
  else {
    /* If this buffer has been allocated using our buffer management we simply
       put the ximage which is in the PRIVATE pointer */
    if (GST_BUFFER_FREE_DATA_FUNC (buf) == gst_glimagesink_buffer_free)
      gst_glimagesink_ximage_put (glimagesink, GST_BUFFER_PRIVATE (buf));
    else {                      /* Else we have to copy the data into our private image, */
      /* if we have one... */
      printf ("Non-locally allocated: Sub-optimal buffer transfer!\n");
      if (glimagesink->glimage) {
#if 0
        memcpy (glimagesink->glimage->ximage->data,
            GST_BUFFER_DATA (buf),
            MIN (GST_BUFFER_SIZE (buf), glimagesink->glimage->size));
#endif
        gst_glimagesink_ximage_put (glimagesink, glimagesink->glimage);
      } else {                  /* No image available. Something went wrong during capsnego ! */

        gst_buffer_unref (buf);
        GST_ELEMENT_ERROR (glimagesink, CORE, NEGOTIATION, (NULL),
            ("no format defined before chain function"));
        return;
      }
    }
  }

  GST_DEBUG ("clock wait: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (glimagesink->time));

  /// ah, BTW, I think the gst_element_wait should happen _before_ the ximage is shown
  if (GST_VIDEOSINK_CLOCK (glimagesink))
    gst_element_wait (GST_ELEMENT (glimagesink), glimagesink->time);

  /* set correct time for next buffer */
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf) && glimagesink->framerate > 0)
    glimagesink->time += GST_SECOND / glimagesink->framerate;

  gst_buffer_unref (buf);

  if (!glimagesink->signal_handoffs)
    gst_glimagesink_handle_xevents (glimagesink, pad);
}

/* Buffer management */

static void
gst_glimagesink_buffer_free (GstBuffer * buffer)
{
  GstGLImageSink *glimagesink;
  GstGLImage *ximage;

  ximage = GST_BUFFER_PRIVATE (buffer);

  g_assert (GST_IS_GLIMAGESINK (ximage->glimagesink));
  glimagesink = ximage->glimagesink;

  /* If our geometry changed we can't reuse that image. */
  if ((ximage->width != GST_VIDEOSINK_WIDTH (glimagesink)) ||
      (ximage->height != GST_VIDEOSINK_HEIGHT (glimagesink)))
    gst_glimagesink_ximage_destroy (glimagesink, ximage);
  else {                        /* In that case we can reuse the image and add it to our image pool. */

    g_mutex_lock (glimagesink->pool_lock);
    glimagesink->image_pool = g_slist_prepend (glimagesink->image_pool, ximage);
    g_mutex_unlock (glimagesink->pool_lock);
  }
}

static GstBuffer *
gst_glimagesink_buffer_alloc (GstPad * pad, guint64 offset, guint size)
{
  GstGLImageSink *glimagesink;
  GstBuffer *buffer;
  GstGLImage *ximage = NULL;
  gboolean not_found = TRUE;

  //printf("Allocating new data buffer\n");

  glimagesink = GST_GLIMAGESINK (gst_pad_get_parent (pad));

  g_mutex_lock (glimagesink->pool_lock);

  /* Walking through the pool cleaning unsuable images and searching for a
     suitable one */
  while (not_found && glimagesink->image_pool) {
    ximage = glimagesink->image_pool->data;

    if (ximage) {
      /* Removing from the pool */
      glimagesink->image_pool = g_slist_delete_link (glimagesink->image_pool,
          glimagesink->image_pool);

      if ((ximage->width != GST_VIDEOSINK_WIDTH (glimagesink)) || (ximage->height != GST_VIDEOSINK_HEIGHT (glimagesink))) {     /* This image is unusable. Destroying... */
        gst_glimagesink_ximage_destroy (glimagesink, ximage);
        ximage = NULL;
      } else {                  /* We found a suitable image */

        break;
      }
    }
  }

  g_mutex_unlock (glimagesink->pool_lock);

  if (!ximage) {                /* We found no suitable image in the pool. Creating... */
    ximage = gst_glimagesink_ximage_new (glimagesink,
        GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));
  }

  if (ximage) {
    buffer = gst_buffer_new ();

    /* Storing some pointers in the buffer */
    GST_BUFFER_PRIVATE (buffer) = ximage;

    GST_BUFFER_DATA (buffer) = ximage->data;
    GST_BUFFER_FREE_DATA_FUNC (buffer) = gst_glimagesink_buffer_free;
    GST_BUFFER_SIZE (buffer) = ximage->size;
    return buffer;
  } else
    return NULL;
}

/* Interfaces stuff */

static gboolean
gst_glimagesink_interface_supported (GstImplementsInterface * iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION || type == GST_TYPE_X_OVERLAY);
  return TRUE;
}

static void
gst_glimagesink_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_glimagesink_interface_supported;
}

static void
gst_glimagesink_navigation_send_event (GstNavigation * navigation,
    GstStructure * structure)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (navigation);
  GstEvent *event;
  gint x_offset, y_offset;
  double x, y;

  event = gst_event_new (GST_EVENT_NAVIGATION);
  event->event_data.structure.structure = structure;

  /* We are not converting the pointer coordinates as there's no hardware 
     scaling done here. The only possible scaling is done by videoscale and
     videoscale will have to catch those events and tranform the coordinates
     to match the applied scaling. So here we just add the offset if the image
     is centered in the window.  */

  x_offset = glimagesink->window->width - GST_VIDEOSINK_WIDTH (glimagesink);
  y_offset = glimagesink->window->height - GST_VIDEOSINK_HEIGHT (glimagesink);

  if (gst_structure_get_double (structure, "pointer_x", &x)) {
    x += x_offset;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
  }
  if (gst_structure_get_double (structure, "pointer_y", &y)) {
    y += y_offset;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
  }

  gst_pad_send_event (gst_pad_get_peer (GST_VIDEOSINK_PAD (glimagesink)),
      event);
}

static void
gst_glimagesink_navigation_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_glimagesink_navigation_send_event;
}

static void
gst_glimagesink_set_xwindow_id (GstXOverlay * overlay, XID xwindow_id)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (overlay);
  GstGLWindow *xwindow = NULL;
  XWindowAttributes attr;

  printf ("set_xwindow_id\n");

  g_return_if_fail (GST_IS_GLIMAGESINK (glimagesink));

  /* If we already use that window return */
  if (glimagesink->window && (xwindow_id == glimagesink->window->win))
    return;

  /* If the element has not initialized the X11 context try to do so */
  if (!glimagesink->xcontext)
    glimagesink->xcontext = gst_glimagesink_xcontext_get (glimagesink);

  if (!glimagesink->xcontext) {
    g_warning ("glimagesink was unable to obtain the X11 context.");
    return;
  }

  /* Clear image pool as the images are unusable anyway */
  gst_glimagesink_imagepool_clear (glimagesink);

  /* Clear the ximage */
  if (glimagesink->glimage) {
    gst_glimagesink_ximage_destroy (glimagesink, glimagesink->glimage);
    glimagesink->glimage = NULL;
  }

  /* If a window is there already we destroy it */
  if (glimagesink->window) {
    gst_glimagesink_xwindow_destroy (glimagesink, glimagesink->window);
    glimagesink->window = NULL;
  }

  /* If the xid is 0 we go back to an internal window */
  if (xwindow_id == 0) {
    /* If no width/height caps nego did not happen window will be created
       during caps nego then */
    if (GST_VIDEOSINK_WIDTH (glimagesink) && GST_VIDEOSINK_HEIGHT (glimagesink)) {
      xwindow = gst_glimagesink_xwindow_new (glimagesink,
          GST_VIDEOSINK_WIDTH (glimagesink),
          GST_VIDEOSINK_HEIGHT (glimagesink));
    }
  } else {
    GST_ELEMENT_ERROR (glimagesink, RESOURCE, TOO_LAZY, (NULL),
        ("glimagesink is incapable of connecting to other X windows !"));
    exit (100);

    xwindow = g_new0 (GstGLWindow, 1);

    xwindow->win = xwindow_id;

    /* We get window geometry, set the event we want to receive,
       and create a GC */
    g_mutex_lock (glimagesink->x_lock);
    XGetWindowAttributes (glimagesink->xcontext->disp, xwindow->win, &attr);
    xwindow->width = attr.width;
    xwindow->height = attr.height;
    xwindow->internal = FALSE;
    XSelectInput (glimagesink->xcontext->disp, xwindow->win, ExposureMask |
        StructureNotifyMask | PointerMotionMask | KeyPressMask |
        KeyReleaseMask);

    //xwindow->gc = XCreateGC (glimagesink->xcontext->disp, xwindow->win, 0, NULL);
    g_mutex_unlock (glimagesink->x_lock);

    /* If that new window geometry differs from our one we try to 
       renegotiate caps */
    if (gst_pad_is_negotiated (GST_VIDEOSINK_PAD (glimagesink)) &&
        (xwindow->width != GST_VIDEOSINK_WIDTH (glimagesink) ||
            xwindow->height != GST_VIDEOSINK_HEIGHT (glimagesink))) {
      GstPadLinkReturn r;

      r = gst_pad_try_set_caps (GST_VIDEOSINK_PAD (glimagesink),
          gst_caps_new_simple ("video/x-raw-rgb",
              "bpp", G_TYPE_INT, glimagesink->xcontext->bpp,
              "depth", G_TYPE_INT, glimagesink->xcontext->depth,
              "endianness", G_TYPE_INT, glimagesink->xcontext->endianness,
              "red_mask", G_TYPE_INT, glimagesink->xcontext->visual->red_mask,
              "green_mask", G_TYPE_INT,
              glimagesink->xcontext->visual->green_mask, "blue_mask",
              G_TYPE_INT, glimagesink->xcontext->visual->blue_mask, "width",
              G_TYPE_INT, xwindow->width, "height", G_TYPE_INT, xwindow->height,
              "framerate", G_TYPE_DOUBLE, glimagesink->framerate, NULL));

      /* If caps nego succeded updating our size */
      if ((r == GST_PAD_LINK_OK) || (r == GST_PAD_LINK_DONE)) {
        GST_VIDEOSINK_WIDTH (glimagesink) = xwindow->width;
        GST_VIDEOSINK_HEIGHT (glimagesink) = xwindow->height;
      }
    }
  }

  /* Recreating our ximage */
  if (!glimagesink->glimage &&
      GST_VIDEOSINK_WIDTH (glimagesink) && GST_VIDEOSINK_HEIGHT (glimagesink)) {
    glimagesink->glimage = gst_glimagesink_ximage_new (glimagesink,
        GST_VIDEOSINK_WIDTH (glimagesink), GST_VIDEOSINK_HEIGHT (glimagesink));
  }

  if (xwindow)
    glimagesink->window = xwindow;
}

static void
gst_glimagesink_get_desired_size (GstXOverlay * overlay,
    guint * width, guint * height)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (overlay);

  *width = GST_VIDEOSINK_WIDTH (glimagesink);
  *height = GST_VIDEOSINK_HEIGHT (glimagesink);
}

static void
gst_glimagesink_expose (GstXOverlay * overlay)
{
  GstGLImageSink *glimagesink = GST_GLIMAGESINK (overlay);

  if (!glimagesink->window)
    return;

  gst_glimagesink_xwindow_update_geometry (glimagesink, glimagesink->window);

  /* We don't act on internal window from outside that could cause some thread
     race with the video sink own thread checking for configure event */
  if (glimagesink->window->internal)
    return;

  //gst_glimagesink_xwindow_clear (glimagesink, glimagesink->window);

  if (glimagesink->cur_image)
    gst_glimagesink_ximage_put (glimagesink, glimagesink->cur_image);
}

static void
gst_glimagesink_xoverlay_init (GstXOverlayClass * iface)
{
  iface->set_xwindow_id = gst_glimagesink_set_xwindow_id;
  iface->get_desired_size = gst_glimagesink_get_desired_size;
  iface->expose = gst_glimagesink_expose;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_glimagesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimagesink;

  g_return_if_fail (GST_IS_GLIMAGESINK (object));

  glimagesink = GST_GLIMAGESINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      glimagesink->display_name = g_strdup (g_value_get_string (value));
      break;
    case ARG_SYNCHRONOUS:
      glimagesink->synchronous = g_value_get_boolean (value);
      if (glimagesink->xcontext) {
        XSynchronize (glimagesink->xcontext->disp, glimagesink->synchronous);
    case ARG_SIGNAL_HANDOFFS:
        glimagesink->signal_handoffs = g_value_get_boolean (value);
        break;
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glimagesink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimagesink;

  g_return_if_fail (GST_IS_GLIMAGESINK (object));

  glimagesink = GST_GLIMAGESINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      g_value_set_string (value, g_strdup (glimagesink->display_name));
      break;
    case ARG_SYNCHRONOUS:
      g_value_set_boolean (value, glimagesink->synchronous);
      break;
    case ARG_SIGNAL_HANDOFFS:
      g_value_set_boolean (value, glimagesink->signal_handoffs);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glimagesink_dispose (GObject * object)
{
  GstGLImageSink *glimagesink;

  glimagesink = GST_GLIMAGESINK (object);

  if (glimagesink->display_name) {
    g_free (glimagesink->display_name);
    glimagesink->display_name = NULL;
  }

  g_mutex_free (glimagesink->x_lock);
  g_mutex_free (glimagesink->pool_lock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_glimagesink_init (GstGLImageSink * glimagesink)
{
  GST_VIDEOSINK_PAD (glimagesink) =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_glimagesink_sink_template_factory), "sink");

  gst_element_add_pad (GST_ELEMENT (glimagesink),
      GST_VIDEOSINK_PAD (glimagesink));

  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_sink_link);
  gst_pad_set_getcaps_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_getcaps);
  gst_pad_set_fixate_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_fixate);
  gst_pad_set_bufferalloc_function (GST_VIDEOSINK_PAD (glimagesink),
      gst_glimagesink_buffer_alloc);

  glimagesink->display_name = NULL;
  glimagesink->xcontext = NULL;
  glimagesink->window = NULL;
  glimagesink->glimage = NULL;
  glimagesink->cur_image = NULL;

  glimagesink->framerate = 0;

  glimagesink->x_lock = g_mutex_new ();

  glimagesink->pixel_width = glimagesink->pixel_height = 1;

  glimagesink->image_pool = NULL;
  glimagesink->pool_lock = g_mutex_new ();

  glimagesink->synchronous = FALSE;
  glimagesink->signal_handoffs = FALSE;

  GST_FLAG_SET (glimagesink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (glimagesink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_glimagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_glimagesink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_glimagesink_sink_template_factory));
}

static void
gst_glimagesink_class_init (GstGLImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_VIDEOSINK);

  g_object_class_install_property (gobject_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "X Display name",
          NULL, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SYNCHRONOUS,
      g_param_spec_boolean ("synchronous", "Synchronous", "When enabled, runs "
          "the X display in synchronous mode. (used only for debugging)", FALSE,
          G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SIGNAL_HANDOFFS,
      g_param_spec_boolean ("signal-handoffs", "Signal handoffs",
          "Send a signal before unreffing the buffer, forces YUV, no GL output",
          FALSE, G_PARAM_READWRITE));
#if 0                           // needed ?
  g_object_class_install_property (gobject_class, ARG_SIGNAL_BUFFER_ALLOC,
      g_param_spec_boolean ("signal-bufferalloc", "Signal buffer allocation",
          "Asks the application for a buffer allocation", FALSE,
          G_PARAM_READWRITE));
#endif

  gst_glimagesink_signals[SIGNAL_HANDOFF] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstGLImageSinkClass, handoff), NULL, NULL,
      gst_marshal_VOID__POINTER_OBJECT, G_TYPE_NONE, 2,
      GST_TYPE_BUFFER, GST_TYPE_PAD);
  gst_glimagesink_signals[SIGNAL_BUFALLOC] =
      g_signal_new ("bufferalloc", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
      G_STRUCT_OFFSET (GstGLImageSinkClass, bufferalloc), NULL, NULL,
      gst_marshal_VOID__POINTER_OBJECT, G_TYPE_NONE, 2,
      GST_TYPE_BUFFER, GST_TYPE_PAD);

  gobject_class->dispose = gst_glimagesink_dispose;
  gobject_class->set_property = gst_glimagesink_set_property;
  gobject_class->get_property = gst_glimagesink_get_property;

  gstelement_class->change_state = gst_glimagesink_change_state;
}

/* ============================================================= */
/*                                                               */
/*                       Public Methods                          */
/*                                                               */
/* ============================================================= */

/* =========================================== */
/*                                             */
/*          Object typing & Creation           */
/*                                             */
/* =========================================== */

GType
gst_glimagesink_get_type (void)
{
  static GType glimagesink_type = 0;

  if (!glimagesink_type) {
    static const GTypeInfo glimagesink_info = {
      sizeof (GstGLImageSinkClass),
      gst_glimagesink_base_init,
      NULL,
      (GClassInitFunc) gst_glimagesink_class_init,
      NULL,
      NULL,
      sizeof (GstGLImageSink),
      0,
      (GInstanceInitFunc) gst_glimagesink_init,
    };
    static const GInterfaceInfo iface_info = {
      (GInterfaceInitFunc) gst_glimagesink_interface_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo navigation_info = {
      (GInterfaceInitFunc) gst_glimagesink_navigation_init,
      NULL,
      NULL,
    };
    static const GInterfaceInfo overlay_info = {
      (GInterfaceInitFunc) gst_glimagesink_xoverlay_init,
      NULL,
      NULL,
    };

    glimagesink_type = g_type_register_static (GST_TYPE_VIDEOSINK,
        "GstGLImageSink", &glimagesink_info, 0);

    g_type_add_interface_static (glimagesink_type,
        GST_TYPE_IMPLEMENTS_INTERFACE, &iface_info);
    g_type_add_interface_static (glimagesink_type, GST_TYPE_NAVIGATION,
        &navigation_info);
    g_type_add_interface_static (glimagesink_type, GST_TYPE_X_OVERLAY,
        &overlay_info);
  }

  return glimagesink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;

  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_SECONDARY, GST_TYPE_GLIMAGESINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_glimagesink, "glimagesink", 0,
      "glimagesink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "glimagesink",
    "OpenGL video output plugin based on OpenGL 1.2 calls",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
