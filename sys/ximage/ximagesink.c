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
#include "ximagesink.h"

static void gst_ximagesink_buffer_free (GstBuffer *buffer);

/* ElementFactory information */
static GstElementDetails gst_ximagesink_details = GST_ELEMENT_DETAILS (
  "Video sink",
  "Sink/Video",
  "A standard X based videosink",
  "Julien Moutte <julien@moutte.net>"
);

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
static GstStaticPadTemplate gst_ximagesink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("video/x-raw-rgb, "
    "framerate = (double) [ 1.0, 100.0 ], "
    "width = (int) [ 0, MAX ], "
    "height = (int) [ 0, MAX ]")
);

enum {
  ARG_0,
  ARG_DISPLAY,
  ARG_SYNCHRONOUS
  /* FILL ME */
};

static GstVideoSinkClass *parent_class = NULL;

/* ============================================================= */
/*                                                               */
/*                       Private Methods                         */
/*                                                               */
/* ============================================================= */

/* X11 stuff */

/* This function handles GstXImage creation depending on XShm availability */
static GstXImage *
gst_ximagesink_ximage_new (GstXImageSink *ximagesink, gint width, gint height)
{
  GstXImage *ximage = NULL;
  
  g_return_val_if_fail (ximagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  
  ximage = g_new0 (GstXImage, 1);
  
  ximage->width = width;
  ximage->height = height;
  ximage->data = NULL;
  ximage->ximagesink = ximagesink;
  
  g_mutex_lock (ximagesink->x_lock);

  ximage->size =  (ximagesink->xcontext->bpp / 8) * ximage->width * ximage->height;
  
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm)
    {
      ximage->ximage = XShmCreateImage (ximagesink->xcontext->disp,
                                        ximagesink->xcontext->visual,
                                        ximagesink->xcontext->depth,
                                        ZPixmap, NULL, &ximage->SHMInfo,
                                        ximage->width, ximage->height);
      
      ximage->SHMInfo.shmid = shmget (IPC_PRIVATE, ximage->size,
                                      IPC_CREAT | 0777);
  
      ximage->SHMInfo.shmaddr = shmat (ximage->SHMInfo.shmid, 0, 0);
      ximage->ximage->data = ximage->SHMInfo.shmaddr;
  
      ximage->SHMInfo.readOnly = FALSE;
  
      XShmAttach (ximagesink->xcontext->disp, &ximage->SHMInfo);
    }
  else
#endif /* HAVE_XSHM */
    {
      ximage->data = g_malloc (ximage->size);
      
      ximage->ximage = XCreateImage (ximagesink->xcontext->disp,
                                     ximagesink->xcontext->visual, 
                                     ximagesink->xcontext->depth,
                                     ZPixmap, 0, ximage->data, 
                                     ximage->width, ximage->height,
                                     ximagesink->xcontext->bpp,
                                     ximage->width * (ximagesink->xcontext->bpp / 8));
    }
  
  if (ximage->ximage)
    {
      XSync(ximagesink->xcontext->disp, 0);
    }
  else
    {
      if (ximage->data)
        g_free (ximage->data);
      
      g_free (ximage);
      
      ximage = NULL;
    }
    
  g_mutex_unlock (ximagesink->x_lock);
  
  return ximage;
}

/* This function destroys a GstXImage handling XShm availability */ 
static void
gst_ximagesink_ximage_destroy (GstXImageSink *ximagesink, GstXImage *ximage)
{
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  g_mutex_lock (ximagesink->x_lock);
  
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm)
    {
      if (ximage->SHMInfo.shmaddr)
        XShmDetach (ximagesink->xcontext->disp, &ximage->SHMInfo);
  
      if (ximage->ximage)
        XDestroyImage (ximage->ximage);
  
      if (ximage->SHMInfo.shmaddr)
        shmdt (ximage->SHMInfo.shmaddr);
  
      if (ximage->SHMInfo.shmid > 0)
        shmctl (ximage->SHMInfo.shmid, IPC_RMID, 0);
    }
  else
#endif /* HAVE_XSHM */ 
    {
      if (ximage->ximage)
        XDestroyImage (ximage->ximage);
    }

  g_mutex_unlock (ximagesink->x_lock);
  
  g_free (ximage);
}

/* This function puts a GstXImage on a GstXImageSink's window */
static void
gst_ximagesink_ximage_put (GstXImageSink *ximagesink, GstXImage *ximage)
{
  gint x, y;
 
  g_return_if_fail (ximage != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  /* We center the image in the window */
  x = MAX (0, (ximagesink->xwindow->width - ximage->width) / 2);
  y = MAX (0, (ximagesink->xwindow->height- ximage->height) / 2);

  g_mutex_lock (ximagesink->x_lock);
#ifdef HAVE_XSHM
  if (ximagesink->xcontext->use_xshm)
    {  
      XShmPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win, 
                    ximagesink->xwindow->gc, ximage->ximage, 
                    0, 0, x, y, ximage->width, ximage->height, 
                    FALSE);
    }
  else
#endif /* HAVE_XSHM */
    {
      XPutImage (ximagesink->xcontext->disp, ximagesink->xwindow->win, 
                 ximagesink->xwindow->gc, ximage->ximage,  
                 0, 0, x, y, ximage->width, ximage->height);
    }
  
  XSync(ximagesink->xcontext->disp, FALSE);
  
  g_mutex_unlock (ximagesink->x_lock);
}

/* This function handles a GstXWindow creation */
static GstXWindow *
gst_ximagesink_xwindow_new (GstXImageSink *ximagesink, gint width, gint height)
{
  GstXWindow *xwindow = NULL;
  
  g_return_val_if_fail (ximagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  
  xwindow = g_new0 (GstXWindow, 1);
  
  xwindow->width = width;
  xwindow->height = height;
  xwindow->internal = TRUE;
  
  g_mutex_lock (ximagesink->x_lock);
  
  xwindow->win = XCreateSimpleWindow (ximagesink->xcontext->disp,
                                      ximagesink->xcontext->root, 
                                      0, 0, xwindow->width, xwindow->height, 
                                      0, 0, ximagesink->xcontext->black);
    
  XMapRaised (ximagesink->xcontext->disp, xwindow->win);
  
  XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
                StructureNotifyMask | PointerMotionMask | KeyPressMask |
                KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
  
  xwindow->gc = XCreateGC (ximagesink->xcontext->disp,
                           xwindow->win, 0, NULL);

  g_mutex_unlock (ximagesink->x_lock);
  
  return xwindow;
}

/* This function destroys a GstXWindow */
static void
gst_ximagesink_xwindow_destroy (GstXImageSink *ximagesink, GstXWindow *xwindow)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  g_mutex_lock (ximagesink->x_lock);
  
  /* If we did not create that window we just free the GC and let it live */
  if (xwindow->internal)
    XDestroyWindow (ximagesink->xcontext->disp, xwindow->win);
  else
    XSelectInput (ximagesink->xcontext->disp, xwindow->win, 0);
    
  XFreeGC (ximagesink->xcontext->disp, xwindow->gc);
  
  g_mutex_unlock (ximagesink->x_lock);
  
  g_free (xwindow);
}

/* This function resizes a GstXWindow */
static void
gst_ximagesink_xwindow_resize (GstXImageSink *ximagesink, GstXWindow *xwindow,
                               guint width, guint height)
{
  g_return_if_fail (xwindow != NULL);
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  g_mutex_lock (ximagesink->x_lock);
  
  xwindow->width = width;
  xwindow->height = height;
  
  XResizeWindow (ximagesink->xcontext->disp, xwindow->win,
                 xwindow->width, xwindow->height);

  g_mutex_unlock (ximagesink->x_lock);
}

/* This function handles XEvents that might be in the queue. It generates
   GstEvent that will be sent upstream in the pipeline to handle interactivity
   and navigation. It will also listen for configure events on the window to
   trigger caps renegotiation so on the fly software scaling can work. */
static void
gst_ximagesink_handle_xevents (GstXImageSink *ximagesink, GstPad *pad)
{
  XEvent e;
  
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  /* We get all events on our window to throw them upstream */
  g_mutex_lock (ximagesink->x_lock);
  while (XCheckWindowEvent (ximagesink->xcontext->disp,
                            ximagesink->xwindow->win,
                            ExposureMask | StructureNotifyMask |
                            PointerMotionMask | KeyPressMask |
                            KeyReleaseMask | ButtonPressMask |
                            ButtonReleaseMask, &e))
    {
      KeySym keysym;
      
      /* We lock only for the X function call */
      g_mutex_unlock (ximagesink->x_lock);
      
      switch (e.type)
        {
          case ConfigureNotify:
            /* Window got resized or moved. We do caps negotiation
               again to get video scaler to fit that new size only if size
               of the window changed. */
            GST_DEBUG ("ximagesink window is at %d, %d with geometry : %d,%d",
                       e.xconfigure.x, e.xconfigure.y,
                       e.xconfigure.width, e.xconfigure.height);
            if ( (ximagesink->xwindow->width != e.xconfigure.width) ||
                 (ximagesink->xwindow->height != e.xconfigure.height) )
              {
                GstPadLinkReturn r;
                ximagesink->xwindow->width = e.xconfigure.width;
                ximagesink->xwindow->height = e.xconfigure.height;
                
                r = gst_pad_try_set_caps (GST_VIDEOSINK_PAD (ximagesink),
                    gst_caps_new_simple ("video/x-raw-rgb",
                      "bpp",        G_TYPE_INT, ximagesink->xcontext->bpp,
                      "depth",      G_TYPE_INT, ximagesink->xcontext->depth,
                      "endianness", G_TYPE_INT, ximagesink->xcontext->endianness,
                      "red_mask",   G_TYPE_INT, ximagesink->xcontext->visual->red_mask,
                      "green_mask", G_TYPE_INT, ximagesink->xcontext->visual->green_mask,
                      "blue_mask",  G_TYPE_INT, ximagesink->xcontext->visual->blue_mask,
                      "width",      G_TYPE_INT, e.xconfigure.width & ~3,
                      "height",     G_TYPE_INT, e.xconfigure.height & ~3,
                      "framerate",  G_TYPE_DOUBLE, ximagesink->framerate,
                      NULL));
                
                if ( (r == GST_PAD_LINK_OK) || (r == GST_PAD_LINK_DONE) )
                  {
                    GST_VIDEOSINK_WIDTH (ximagesink) = e.xconfigure.width & ~3;
                    GST_VIDEOSINK_HEIGHT (ximagesink) = e.xconfigure.height & ~3;
                
                    if ( (ximagesink->ximage) &&
                         ( (GST_VIDEOSINK_WIDTH (ximagesink) != ximagesink->ximage->width) ||
                           (GST_VIDEOSINK_HEIGHT (ximagesink) != ximagesink->ximage->height) ) )
                      {
                        /* We renew our ximage only if size changed */
                        gst_ximagesink_ximage_destroy (ximagesink,
                                                       ximagesink->ximage);
                
                        ximagesink->ximage = gst_ximagesink_ximage_new (
                                                            ximagesink,
                                                            GST_VIDEOSINK_WIDTH (ximagesink),
                                                            GST_VIDEOSINK_HEIGHT (ximagesink));
                      }
                  }
              }
            break;
          case MotionNotify:
            /* Mouse pointer moved over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("ximagesink pointer moved over window at %d,%d",
                       e.xmotion.x, e.xmotion.y);
            gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
                                             "mouse-move", 0,
                                             e.xmotion.x, e.xmotion.y);
            break;
          case ButtonPress:
            /* Mouse button pressed/released over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("ximagesink button %d pressed over window at %d,%d",
                       e.xbutton.button, e.xbutton.x, e.xbutton.x);
            gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
                                             "mouse-button-press",
                                             e.xbutton.button,
                                             e.xbutton.x, e.xbutton.y);
            break;
          case ButtonRelease:
            GST_DEBUG ("ximagesink button %d release over window at %d,%d",
                       e.xbutton.button, e.xbutton.x, e.xbutton.x);
            gst_navigation_send_mouse_event (GST_NAVIGATION (ximagesink),
                                             "mouse-button-release",
                                             e.xbutton.button,
                                             e.xbutton.x, e.xbutton.y);
            break;
          case KeyPress:
          case KeyRelease:
            /* Key pressed/released over our window. We send upstream
               events for interactivity/navigation */
            GST_DEBUG ("ximagesink key %d pressed over window at %d,%d",
                       e.xkey.keycode, e.xkey.x, e.xkey.x);
            keysym = XKeycodeToKeysym (ximagesink->xcontext->disp,
                                       e.xkey.keycode, 0);
            if (keysym != NoSymbol) {
              gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
                                             e.type == KeyPress ? 
                                               "key-press" : "key-release",
                                             XKeysymToString (keysym));
            }
            else {
              gst_navigation_send_key_event (GST_NAVIGATION (ximagesink),
                                             e.type == KeyPress ? 
                                               "key-press" : "key-release",
                                             "unknown");
            }
            break;
          default:
            GST_DEBUG ("ximagesink unhandled X event (%d)", e.type);
        }
      
      g_mutex_lock (ximagesink->x_lock);
    }
  g_mutex_unlock (ximagesink->x_lock);
}

/* This function get the X Display and global infos about it. Everything is
   stored in our object and will be cleaned when the object is disposed. Note
   here that caps for supported format are generated without any window or 
   image creation */
static GstXContext *
gst_ximagesink_xcontext_get (GstXImageSink *ximagesink)
{
  GstXContext *xcontext = NULL;
  XPixmapFormatValues *px_formats = NULL;
  gint nb_formats = 0, i;
  
  g_return_val_if_fail (ximagesink != NULL, NULL);
  g_return_val_if_fail (GST_IS_XIMAGESINK (ximagesink), NULL);
  
  xcontext = g_new0 (GstXContext, 1);
  
  g_mutex_lock (ximagesink->x_lock);
  
  xcontext->disp = XOpenDisplay (ximagesink->display_name);
  
  if (!xcontext->disp)
    {
      g_mutex_unlock (ximagesink->x_lock);
      g_free (xcontext);
      gst_element_error (GST_ELEMENT (ximagesink), "Could not open display");
      return NULL;
    }
  
  xcontext->screen = DefaultScreenOfDisplay (xcontext->disp);
  xcontext->screen_num = DefaultScreen (xcontext->disp);
  xcontext->visual = DefaultVisual(xcontext->disp, xcontext->screen_num);
  xcontext->root = DefaultRootWindow (xcontext->disp);
  xcontext->white = XWhitePixel (xcontext->disp, xcontext->screen_num);
  xcontext->black = XBlackPixel (xcontext->disp, xcontext->screen_num);
  xcontext->depth = DefaultDepthOfScreen (xcontext->screen);
  
  /* We get supported pixmap formats at supported depth */
  px_formats = XListPixmapFormats (xcontext->disp, &nb_formats);
  
  if (!px_formats)
    {
      XCloseDisplay (xcontext->disp);
      g_mutex_unlock (ximagesink->x_lock);
      g_free (xcontext);
      return NULL;
    }
  
  /* We get bpp value corresponding to our running depth */
  for (i=0; i<nb_formats; i++)
    {
      if (px_formats[i].depth == xcontext->depth)
        xcontext->bpp = px_formats[i].bits_per_pixel;
    }
    
  XFree (px_formats);
    
  xcontext->endianness = (ImageByteOrder (xcontext->disp) == LSBFirst) ? G_LITTLE_ENDIAN:G_BIG_ENDIAN;
  
#ifdef HAVE_XSHM
  /* Search for XShm extension support */
  if (XQueryExtension (xcontext->disp, "MIT-SHM", &i, &i, &i))
    {
      xcontext->use_xshm = TRUE;
      GST_DEBUG ("ximagesink is using XShm extension");
    }
  else
    {
      xcontext->use_xshm = FALSE;
      GST_DEBUG ("ximagesink is not using XShm extension");
    }
#endif /* HAVE_XSHM */

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
  
  xcontext->caps = gst_caps_new_simple ("video/x-raw-rgb",
      "bpp",        G_TYPE_INT, xcontext->bpp,
      "depth",      G_TYPE_INT, xcontext->depth,
      "endianness", G_TYPE_INT, xcontext->endianness,
      "red_mask",   G_TYPE_INT, xcontext->visual->red_mask,
      "green_mask", G_TYPE_INT, xcontext->visual->green_mask,
      "blue_mask",  G_TYPE_INT, xcontext->visual->blue_mask,
      "width",      GST_TYPE_INT_RANGE, 0, G_MAXINT,
      "height",     GST_TYPE_INT_RANGE, 0, G_MAXINT,
      "framerate",  GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE,
      NULL);
 
  g_mutex_unlock (ximagesink->x_lock);
  
  return xcontext;
}

/* This function cleans the X context. Closing the Display and unrefing the
   caps for supported formats. */
static void
gst_ximagesink_xcontext_clear (GstXImageSink *ximagesink)
{
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  gst_caps_free (ximagesink->xcontext->caps);
  
  g_mutex_lock (ximagesink->x_lock);
  
  XCloseDisplay (ximagesink->xcontext->disp);
  
  g_mutex_unlock (ximagesink->x_lock);
  
  ximagesink->xcontext = NULL;
}

/* Element stuff */

static GstCaps *
gst_ximagesink_fixate (GstPad *pad, const GstCaps *caps)
{
  GstStructure *structure;
  GstCaps *newcaps;

  if (gst_caps_get_size (caps) > 1) return NULL;

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
gst_ximagesink_getcaps (GstPad *pad)
{
  GstXImageSink *ximagesink;
  
  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));
  
  if (ximagesink->xcontext)
    return gst_caps_copy (ximagesink->xcontext->caps);

  return gst_caps_from_string ("video/x-raw-rgb, "
      "framerate = (double) [ 0, MAX ], "
      "width = (int) [ 0, MAX ], "
      "height = (int) [ 0, MAX ]");
}

static GstPadLinkReturn
gst_ximagesink_sinkconnect (GstPad *pad, const GstCaps *caps)
{
  GstXImageSink *ximagesink;
  char *caps_str1, *caps_str2;
  gboolean ret;
  GstStructure *structure;

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  if (!ximagesink->xcontext)
    return GST_PAD_LINK_DELAYED;
  
  caps_str1 = gst_caps_to_string (ximagesink->xcontext->caps);
  caps_str2 = gst_caps_to_string (caps);

  GST_DEBUG ("sinkconnect %s with %s", caps_str1, caps_str2);

  g_free (caps_str1);
  g_free (caps_str2);

  structure = gst_caps_get_structure (caps, 0);
  ret = gst_structure_get_int (structure, "width",
                               &(GST_VIDEOSINK_WIDTH (ximagesink)));
  ret &= gst_structure_get_int (structure, "height",
                                &(GST_VIDEOSINK_HEIGHT (ximagesink)));
  ret &= gst_structure_get_double (structure,
                                   "framerate", &ximagesink->framerate);
  if (!ret) return GST_PAD_LINK_REFUSED;
  
  ximagesink->pixel_width = 1;
  gst_structure_get_int  (structure, "pixel_width",
                          &ximagesink->pixel_width);

  ximagesink->pixel_height = 1;
  gst_structure_get_int  (structure, "pixel_height",
                          &ximagesink->pixel_height);
  
  /* Creating our window and our image */
  if (!ximagesink->xwindow)
    ximagesink->xwindow = gst_ximagesink_xwindow_new (ximagesink,
                                             GST_VIDEOSINK_WIDTH (ximagesink),
                                             GST_VIDEOSINK_HEIGHT (ximagesink));
  else
    {
      if (ximagesink->xwindow->internal)
        gst_ximagesink_xwindow_resize (ximagesink, ximagesink->xwindow,
                                       GST_VIDEOSINK_WIDTH (ximagesink),
                                       GST_VIDEOSINK_HEIGHT (ximagesink));
    }
  
  if ( (ximagesink->ximage) &&
       ( (GST_VIDEOSINK_WIDTH (ximagesink) != ximagesink->ximage->width) ||
         (GST_VIDEOSINK_HEIGHT (ximagesink) != ximagesink->ximage->height) ) )
    { /* We renew our ximage only if size changed */
      gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
  
      ximagesink->ximage = gst_ximagesink_ximage_new (ximagesink,
                                             GST_VIDEOSINK_WIDTH (ximagesink),
                                             GST_VIDEOSINK_HEIGHT (ximagesink));
    }
  else if (!ximagesink->ximage) /* If no ximage, creating one */
    ximagesink->ximage = gst_ximagesink_ximage_new (ximagesink,
                                             GST_VIDEOSINK_WIDTH (ximagesink),
                                             GST_VIDEOSINK_HEIGHT (ximagesink));
  
  gst_x_overlay_got_desired_size (GST_X_OVERLAY (ximagesink),
                                  GST_VIDEOSINK_WIDTH (ximagesink),
                                  GST_VIDEOSINK_HEIGHT (ximagesink));
  
  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_ximagesink_change_state (GstElement *element)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      /* Initializing the XContext */
      if (!ximagesink->xcontext)
        ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink);
      if (!ximagesink->xcontext)
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      ximagesink->time = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      ximagesink->framerate = 0;
      GST_VIDEOSINK_WIDTH (ximagesink) = 0;
      GST_VIDEOSINK_HEIGHT (ximagesink) = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return GST_STATE_SUCCESS;
}

static void
gst_ximagesink_chain (GstPad *pad, GstData *data)
{
  GstBuffer *buf = GST_BUFFER (data);
  GstXImageSink *ximagesink;
  
  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));
    
  if (GST_IS_EVENT (data))
    {
      gst_pad_event_default (pad, GST_EVENT (data));
      return;
    }
  
  buf = GST_BUFFER (data);
  /* update time */
  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    ximagesink->time = GST_BUFFER_TIMESTAMP (buf);
  }
  GST_DEBUG ("videosink: clock wait: %" G_GUINT64_FORMAT, ximagesink->time);
  
  if (GST_VIDEOSINK_CLOCK (ximagesink)) {
    gst_element_wait (GST_ELEMENT (ximagesink), ximagesink->time);
  }
  
  /* If this buffer has been allocated using our buffer management we simply
     put the ximage which is in the PRIVATE pointer */
  if (GST_BUFFER_FREE_DATA_FUNC (buf) == gst_ximagesink_buffer_free)
    {
      gst_ximagesink_ximage_put (ximagesink, GST_BUFFER_PRIVATE (buf));
    }
  else /* Else we have to copy the data into our private image, */
    {  /* if we have one... */
      if (ximagesink->ximage)
        {
          memcpy (ximagesink->ximage->ximage->data, 
                  GST_BUFFER_DATA (buf), 
                  MIN (GST_BUFFER_SIZE (buf), ximagesink->ximage->size));
          gst_ximagesink_ximage_put (ximagesink, ximagesink->ximage);
        }
      else /* No image available. Something went wrong during capsnego ! */
        {
          gst_buffer_unref (buf);
          gst_element_error (GST_ELEMENT (ximagesink), "no image to draw");
          return;
        }
    }

  /* set correct time for next buffer */
  if (!GST_BUFFER_TIMESTAMP_IS_VALID (buf) && ximagesink->framerate > 0) {
    ximagesink->time += GST_SECOND / ximagesink->framerate;
  }
  
  gst_buffer_unref (buf);
    
  gst_ximagesink_handle_xevents (ximagesink, pad);
}

/* Buffer management */

static void
gst_ximagesink_buffer_free (GstBuffer *buffer)
{
  GstXImageSink *ximagesink;
  GstXImage *ximage;
  
  ximage = GST_BUFFER_PRIVATE (buffer);
  
  g_assert (GST_IS_XIMAGESINK (ximage->ximagesink));
  ximagesink = ximage->ximagesink;
  
  /* If our geometry changed we can't reuse that image. */
  if ( (ximage->width != GST_VIDEOSINK_WIDTH (ximagesink)) ||
       (ximage->height != GST_VIDEOSINK_HEIGHT (ximagesink)) )
    gst_ximagesink_ximage_destroy (ximagesink, ximage);
  else /* In that case we can reuse the image and add it to our image pool. */
    {
      g_mutex_lock (ximagesink->pool_lock);
      ximagesink->image_pool = g_slist_prepend (ximagesink->image_pool, ximage);
      g_mutex_unlock (ximagesink->pool_lock);
    }
}

static GstBuffer *
gst_ximagesink_buffer_alloc (GstPad *pad, guint64 offset, guint size)
{
  GstXImageSink *ximagesink;
  GstBuffer *buffer;
  GstXImage *ximage = NULL;
  gboolean not_found = TRUE;
  
  ximagesink = GST_XIMAGESINK (gst_pad_get_parent (pad));

  g_mutex_lock (ximagesink->pool_lock);
  
  /* Walking through the pool cleaning unsuable images and searching for a
     suitable one */
  while (not_found && ximagesink->image_pool)
    {
      ximage = ximagesink->image_pool->data;
      
      if (ximage)
        {
          /* Removing from the pool */
          ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
                                                        ximagesink->image_pool);
          
          if ( (ximage->width != GST_VIDEOSINK_WIDTH (ximagesink)) ||
               (ximage->height != GST_VIDEOSINK_HEIGHT (ximagesink)) )
            { /* This image is unusable. Destroying... */
              gst_ximagesink_ximage_destroy (ximagesink, ximage);
              ximage = NULL;
            }
          else /* We found a suitable image */
            {
              break;
            }
        }
    }
   
  g_mutex_unlock (ximagesink->pool_lock);
  
  if (!ximage) /* We found no suitable image in the pool. Creating... */
    {
      ximage = gst_ximagesink_ximage_new (ximagesink,
                                          GST_VIDEOSINK_WIDTH (ximagesink),
                                          GST_VIDEOSINK_HEIGHT (ximagesink));
    }
  
  if (ximage)
    {
      buffer = gst_buffer_new ();
      
      /* Storing some pointers in the buffer */
      GST_BUFFER_PRIVATE (buffer) = ximage;
      
      GST_BUFFER_DATA (buffer) = ximage->ximage->data;
      GST_BUFFER_FREE_DATA_FUNC (buffer) = gst_ximagesink_buffer_free;
      GST_BUFFER_SIZE (buffer) = ximage->size;
      return buffer;
    }
  else
    return NULL;
}

static void
gst_ximagesink_imagepool_clear (GstXImageSink *ximagesink)
{
  g_mutex_lock(ximagesink->pool_lock);
  
  while (ximagesink->image_pool)
    {
      GstXImage *ximage = ximagesink->image_pool->data;
      ximagesink->image_pool = g_slist_delete_link (ximagesink->image_pool,
                                                    ximagesink->image_pool);
      gst_ximagesink_ximage_destroy (ximagesink, ximage);
    }
  
  g_mutex_unlock(ximagesink->pool_lock);
}

/* Interfaces stuff */

static gboolean
gst_ximagesink_interface_supported (GstImplementsInterface *iface, GType type)
{
  g_assert (type == GST_TYPE_NAVIGATION || type == GST_TYPE_X_OVERLAY);
  return TRUE;
}

static void
gst_ximagesink_interface_init (GstImplementsInterfaceClass *klass)
{
  klass->supported = gst_ximagesink_interface_supported;
}

static void
gst_ximagesink_navigation_send_event (GstNavigation *navigation,
                                      GstStructure *structure)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (navigation);
  GstEvent *event;
  gint x_offset, y_offset;
  double x,y;
  
  event = gst_event_new (GST_EVENT_NAVIGATION);
  event->event_data.structure.structure = structure;

  /* We are not converting the pointer coordinates as there's no hardware 
     scaling done here. The only possible scaling is done by videoscale and
     videoscale will have to catch those events and tranform the coordinates
     to match the applied scaling. So here we just add the offset if the image
     is centered in the window.  */
  
  x_offset = ximagesink->xwindow->width - GST_VIDEOSINK_WIDTH (ximagesink);
  y_offset = ximagesink->xwindow->height - GST_VIDEOSINK_HEIGHT (ximagesink);
  
  if (gst_structure_get_double (structure, "pointer_x", &x))
    {
      x += x_offset;
      gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x, NULL);
    }
  if (gst_structure_get_double (structure, "pointer_y", &y))
    {
      y += y_offset;
      gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE, y, NULL);
    }
    
  gst_pad_send_event (gst_pad_get_peer (GST_VIDEOSINK_PAD (ximagesink)), event);
}

static void
gst_ximagesink_navigation_init (GstNavigationInterface *iface)
{
  iface->send_event = gst_ximagesink_navigation_send_event;
}

static void
gst_ximagesink_set_xwindow_id (GstXOverlay *overlay, XID xwindow_id)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);
  GstXWindow *xwindow = NULL;
  XWindowAttributes attr;
  
  g_return_if_fail (ximagesink != NULL);
  g_return_if_fail (GST_IS_XIMAGESINK (ximagesink));
  
  /* If we already use that window return */
  if (ximagesink->xwindow && (xwindow_id == ximagesink->xwindow->win))
    return;
  
  /* If the element has not initialized the X11 context try to do so */
  if (!ximagesink->xcontext)
    ximagesink->xcontext = gst_ximagesink_xcontext_get (ximagesink);
  
  if (!ximagesink->xcontext)
    {
      g_warning ("ximagesink was unable to obtain the X11 context.");
      return;
    }
    
  /* Clear image pool as the images are unusable anyway */
  gst_ximagesink_imagepool_clear (ximagesink);
  
  /* Clear the ximage */
  if (ximagesink->ximage)
    {
      gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
      ximagesink->ximage = NULL;
    }
  
  /* If a window is there already we destroy it */
  if (ximagesink->xwindow)
    {
      gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
      ximagesink->xwindow = NULL;
    }
    
  /* If the xid is 0 we go back to an internal window */
  if (xwindow_id == 0)
    {
      /* If no width/height caps nego did not happen window will be created
         during caps nego then */
      if (GST_VIDEOSINK_WIDTH (ximagesink) &&
          GST_VIDEOSINK_HEIGHT (ximagesink))
        {
          xwindow = gst_ximagesink_xwindow_new (ximagesink,
                                            GST_VIDEOSINK_WIDTH (ximagesink),
                                            GST_VIDEOSINK_HEIGHT (ximagesink));
        }
    }
  else
    {
      xwindow = g_new0 (GstXWindow, 1);

      xwindow->win = xwindow_id;
  
      /* We get window geometry, set the event we want to receive,
         and create a GC */
      g_mutex_lock (ximagesink->x_lock);
      XGetWindowAttributes (ximagesink->xcontext->disp, xwindow->win, &attr);
      xwindow->width = attr.width;
      xwindow->height = attr.height;
      xwindow->internal = FALSE;
      XSelectInput (ximagesink->xcontext->disp, xwindow->win, ExposureMask |
                    StructureNotifyMask | PointerMotionMask | KeyPressMask |
                    KeyReleaseMask);
  
      xwindow->gc = XCreateGC (ximagesink->xcontext->disp,
                               xwindow->win, 0, NULL);
      g_mutex_unlock (ximagesink->x_lock);
      
      /* If that new window geometry differs from our one we try to 
         renegotiate caps */
      if (gst_pad_is_negotiated (GST_VIDEOSINK_PAD (ximagesink)) &&
          (xwindow->width != GST_VIDEOSINK_WIDTH (ximagesink) ||
           xwindow->height != GST_VIDEOSINK_HEIGHT (ximagesink)))
        {
          GstPadLinkReturn r;
          r = gst_pad_try_set_caps (GST_VIDEOSINK_PAD (ximagesink),
                  gst_caps_new_simple ("video/x-raw-rgb",
                   "bpp",        G_TYPE_INT, ximagesink->xcontext->bpp,
                   "depth",      G_TYPE_INT, ximagesink->xcontext->depth,
                   "endianness", G_TYPE_INT, ximagesink->xcontext->endianness,
                   "red_mask",   G_TYPE_INT, ximagesink->xcontext->visual->red_mask,
                   "green_mask", G_TYPE_INT, ximagesink->xcontext->visual->green_mask,
                   "blue_mask",  G_TYPE_INT, ximagesink->xcontext->visual->blue_mask,
                   "width",      G_TYPE_INT, xwindow->width & ~3,
                   "height",     G_TYPE_INT, xwindow->height & ~3,
                   "framerate",  G_TYPE_DOUBLE, ximagesink->framerate,
                   NULL));
          
          /* If caps nego succeded updating our size */
          if ( (r == GST_PAD_LINK_OK) || (r == GST_PAD_LINK_DONE) )
            {
              GST_VIDEOSINK_WIDTH (ximagesink) = xwindow->width & ~3;
              GST_VIDEOSINK_HEIGHT (ximagesink) = xwindow->height & ~3;
            }
        }
    }
  
  /* Recreating our ximage */
  if (!ximagesink->ximage &&
      GST_VIDEOSINK_WIDTH (ximagesink) &&
      GST_VIDEOSINK_HEIGHT (ximagesink))
    {
      ximagesink->ximage = gst_ximagesink_ximage_new (
                                            ximagesink,
                                            GST_VIDEOSINK_WIDTH (ximagesink),
                                            GST_VIDEOSINK_HEIGHT (ximagesink));
    }
    
  if (xwindow)
    ximagesink->xwindow = xwindow;
}

static void
gst_ximagesink_get_desired_size (GstXOverlay *overlay,
                                 guint *width, guint *height)
{
  GstXImageSink *ximagesink = GST_XIMAGESINK (overlay);

  *width = GST_VIDEOSINK_WIDTH (ximagesink);
  *height = GST_VIDEOSINK_HEIGHT (ximagesink);
}

static void
gst_ximagesink_xoverlay_init (GstXOverlayClass *iface)
{
  iface->set_xwindow_id = gst_ximagesink_set_xwindow_id;
  iface->get_desired_size = gst_ximagesink_get_desired_size;
}

/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_ximagesink_set_property (GObject *object, guint prop_id,
                             const GValue *value, GParamSpec *pspec)
{
  GstXImageSink *ximagesink;
  
  g_return_if_fail (GST_IS_XIMAGESINK (object));
  
  ximagesink = GST_XIMAGESINK (object);
  
  switch (prop_id)
    {
      case ARG_DISPLAY:
        ximagesink->display_name = g_strdup (g_value_get_string (value));
        break;
      case ARG_SYNCHRONOUS:
        ximagesink->synchronous = g_value_get_boolean (value);
        if (ximagesink->xcontext) {
          XSynchronize (ximagesink->xcontext->disp,
                        ximagesink->synchronous);
        }
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_ximagesink_get_property (GObject *object, guint prop_id,
                             GValue *value, GParamSpec *pspec)
{
  GstXImageSink *ximagesink;
  
  g_return_if_fail (GST_IS_XIMAGESINK (object));
  
  ximagesink = GST_XIMAGESINK (object);
  
  switch (prop_id)
    {
      case ARG_DISPLAY:
        g_value_set_string (value, g_strdup (ximagesink->display_name));
        break;
      case ARG_SYNCHRONOUS:
        g_value_set_boolean (value, ximagesink->synchronous);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_ximagesink_dispose (GObject *object)
{
  GstXImageSink *ximagesink;

  ximagesink = GST_XIMAGESINK (object);

  if (ximagesink->display_name)
    {
      g_free (ximagesink->display_name);
      ximagesink->display_name = NULL;
    }
    
  if (ximagesink->ximage)
    {
      gst_ximagesink_ximage_destroy (ximagesink, ximagesink->ximage);
      ximagesink->ximage = NULL;
    }
    
  if (ximagesink->image_pool)
    gst_ximagesink_imagepool_clear (ximagesink);
  
  if (ximagesink->xwindow)
    {
      gst_ximagesink_xwindow_destroy (ximagesink, ximagesink->xwindow);
      ximagesink->xwindow = NULL;
    }
  
  if (ximagesink->xcontext)
    gst_ximagesink_xcontext_clear (ximagesink);
    
  g_mutex_free (ximagesink->x_lock);
  g_mutex_free (ximagesink->pool_lock);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_ximagesink_init (GstXImageSink *ximagesink)
{
  GST_VIDEOSINK_PAD (ximagesink) = gst_pad_new_from_template (
      gst_static_pad_template_get (&gst_ximagesink_sink_template_factory),
      "sink");
  
  gst_element_add_pad (GST_ELEMENT (ximagesink),
                       GST_VIDEOSINK_PAD (ximagesink));

  gst_pad_set_chain_function (GST_VIDEOSINK_PAD (ximagesink),
                              gst_ximagesink_chain);
  gst_pad_set_link_function (GST_VIDEOSINK_PAD (ximagesink),
                             gst_ximagesink_sinkconnect);
  gst_pad_set_getcaps_function (GST_VIDEOSINK_PAD (ximagesink),
                                gst_ximagesink_getcaps);
  gst_pad_set_fixate_function (GST_VIDEOSINK_PAD (ximagesink),
                                gst_ximagesink_fixate);
  gst_pad_set_bufferalloc_function (GST_VIDEOSINK_PAD (ximagesink),
                                    gst_ximagesink_buffer_alloc);
  
  ximagesink->display_name = NULL;
  ximagesink->xcontext = NULL;
  ximagesink->xwindow = NULL;
  ximagesink->ximage = NULL;
  
  ximagesink->framerate = 0;
  
  ximagesink->x_lock = g_mutex_new ();
  
  ximagesink->pixel_width = ximagesink->pixel_height = 1;
  
  ximagesink->image_pool = NULL;
  ximagesink->pool_lock = g_mutex_new ();

  GST_FLAG_SET(ximagesink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET(ximagesink, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_ximagesink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (element_class, &gst_ximagesink_details);

  gst_element_class_add_pad_template (element_class, 
    gst_static_pad_template_get (&gst_ximagesink_sink_template_factory));
}

static void
gst_ximagesink_class_init (GstXImageSinkClass *klass)
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
  
  gobject_class->dispose = gst_ximagesink_dispose;
  gobject_class->set_property = gst_ximagesink_set_property;
  gobject_class->get_property = gst_ximagesink_get_property;
  
  gstelement_class->change_state = gst_ximagesink_change_state;
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
gst_ximagesink_get_type (void)
{
  static GType ximagesink_type = 0;

  if (!ximagesink_type)
    {
      static const GTypeInfo ximagesink_info = {
        sizeof(GstXImageSinkClass),
        gst_ximagesink_base_init,
        NULL,
        (GClassInitFunc) gst_ximagesink_class_init,
        NULL,
        NULL,
        sizeof(GstXImageSink),
        0,
        (GInstanceInitFunc) gst_ximagesink_init,
      };
      static const GInterfaceInfo iface_info = {
        (GInterfaceInitFunc) gst_ximagesink_interface_init,
        NULL,
        NULL,
      };
      static const GInterfaceInfo navigation_info = {
        (GInterfaceInitFunc) gst_ximagesink_navigation_init,
        NULL,
        NULL,
      };
      static const GInterfaceInfo overlay_info = {
        (GInterfaceInitFunc) gst_ximagesink_xoverlay_init,
        NULL,
        NULL,
      };
      
      ximagesink_type = g_type_register_static (GST_TYPE_VIDEOSINK,
                                                "GstXImageSink",
                                                &ximagesink_info, 0);
      
      g_type_add_interface_static (ximagesink_type, GST_TYPE_IMPLEMENTS_INTERFACE,
                                   &iface_info);
      g_type_add_interface_static (ximagesink_type, GST_TYPE_NAVIGATION,
                                   &navigation_info);
      g_type_add_interface_static (ximagesink_type, GST_TYPE_X_OVERLAY,
                                   &overlay_info);
    }
    
  return ximagesink_type;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;
  
  if (!gst_element_register (plugin, "ximagesink",
                             GST_RANK_SECONDARY, GST_TYPE_XIMAGESINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ximagesink",
  "XFree86 video output plugin based on standard Xlib calls",
  plugin_init,
  VERSION,
  GST_LICENSE,
  GST_PACKAGE,
  GST_ORIGIN)
