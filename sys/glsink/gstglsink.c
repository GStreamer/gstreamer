/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

/*
<ds-work> your element belongs to a scheduler, which calls some functions from the same thread
<ds-work> all the other functions could be called from any random thread
<gernot> ds-work: which are the "some" function in that case ? 
<gernot> It is quite costly to do glXGetCurrentContext for every function call.
<ds-work> _chain, -get, _loop
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <sys/time.h>

/*#define GST_DEBUG_FORCE_DISABLE*/

#include "gstglsink.h"

/* elementfactory information */
static GstElementDetails gst_glsink_details = {
  "OpenGL Sink/GLX",
  "Sink/GLVideo",
  "An OpenGL based video sink - uses OpenGL and GLX to draw video, utilizing different acceleration options",
  "Gernot Ziegler <gz@lysator.liu.se>"
};

/* default template - initiated with class struct to allow gst-register to work
   with X running */
GST_PAD_TEMPLATE_FACTORY (gst_glsink_sink_template_factory,
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_CAPS_NEW ("glsink_rgbsink", "video/x-raw-rgb",
	"framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
	"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
	"height", GST_PROPS_INT_RANGE (0, G_MAXINT)),
    GST_CAPS_NEW ("glsink_yuvsink", "video/x-raw-yuv",
	"framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT),
	"width", GST_PROPS_INT_RANGE (0, G_MAXINT),
	"height", GST_PROPS_INT_RANGE (0, G_MAXINT))
    )

/* glsink signals and args */
     enum
     {
       LAST_SIGNAL
     };


     enum
     {
       ARG_0,
       ARG_WIDTH,
       ARG_HEIGHT,
       ARG_FRAMES_DISPLAYED,
       ARG_FRAME_TIME,
       ARG_HOOK,
       ARG_MUTE,
       ARG_REPAINT,
       ARG_DEMO,
       ARG_DUMP
     };

/* GLsink class */
#define GST_TYPE_GLSINK		(gst_glsink_get_type())
#define GST_GLSINK(obj)		(G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GLSINK,GstGLSink))
#define GST_GLSINK_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_GLSINK,GstGLSink))
#define GST_IS_GLSINK(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GLSINK))
#define GST_IS_GLSINK_CLASS(obj)	(G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_GLSINK))

     typedef struct _GstGLSink GstGLSink;
     typedef struct _GstGLSinkClass GstGLSinkClass;

     struct _GstGLSink
     {
       GstElement element;

       GstPad *sinkpad;

       gint frames_displayed;
       guint64 frame_time;
       gint width, height;
       gboolean muted;
       gint demo;		// some kind of fun demo mode to let GL show its 3D capabilities
       gboolean dumpvideo;	// dump the video down to .ppm:s 
       GstBuffer *last_image;	/* not thread safe ? */

       GstClock *clock;

       GMutex *cache_lock;
       GList *cache;

       /* plugins */
       GstImagePlugin *plugin;
       GstImageConnection *conn;

       /* allow anybody to hook in here */
       GstImageInfo *hook;
     };

     struct _GstGLSinkClass
     {
       GstElementClass parent_class;

       /* plugins */
       GList *plugins;
     };


     static GType gst_glsink_get_type (void);
     static void gst_glsink_base_init (gpointer g_class);
     static void gst_glsink_class_init (GstGLSinkClass * klass);
     static void gst_glsink_init (GstGLSink * sink);

/* static void 			gst_glsink_dispose 		(GObject *object); */

     static void gst_glsink_chain (GstPad * pad, GstData * _data);
     static void gst_glsink_set_clock (GstElement * element, GstClock * clock);
     static GstElementStateReturn gst_glsink_change_state (GstElement *
    element);
     static GstPadLinkReturn gst_glsink_sinkconnect (GstPad * pad,
    GstCaps * caps);
     static GstCaps *gst_glsink_getcaps (GstPad * pad, GstCaps * caps);

     static void gst_glsink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
     static void gst_glsink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

     static void gst_glsink_release_conn (GstGLSink * sink);
     static void gst_glsink_append_cache (GstGLSink * sink,
    GstImageData * image);
     static gboolean gst_glsink_set_caps (GstGLSink * sink, GstCaps * caps);

/* prototypes from plugins */
     extern GstImagePlugin *get_gl_rgbimage_plugin (void);
     extern GstImagePlugin *get_gl_nvimage_plugin (void);

/* default output */
     extern void gst_glxwindow_new (GstGLSink * sink);
     extern void gst_glxwindow_hook_context (GstImageInfo * info);
     extern void gst_glxwindow_unhook_context (GstImageInfo * info);


     static GstPadTemplate *sink_template;

     static GstElementClass *parent_class = NULL;

/* static guint gst_glsink_signals[LAST_SIGNAL] = { 0 }; */

     static GType gst_glsink_get_type (void)
{
  static GType videosink_type = 0;

  if (!videosink_type) {
    static const GTypeInfo videosink_info = {
      sizeof (GstGLSinkClass),
      gst_glsink_base_init,
      NULL,
      (GClassInitFunc) gst_glsink_class_init,
      NULL,
      NULL,
      sizeof (GstGLSink),
      0,
      (GInstanceInitFunc) gst_glsink_init,
    };
    videosink_type =
	g_type_register_static (GST_TYPE_ELEMENT, "GstGLSink", &videosink_info,
	0);
  }
  return videosink_type;
}

static void
gst_glsink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_glsink_details);

  gst_element_class_add_pad_template (element_class,
      GST_PAD_TEMPLATE_GET (gst_glsink_sink_template_factory));
}

static void
gst_glsink_class_init (GstGLSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_WIDTH, g_param_spec_int ("width", "Width", "The video width", G_MININT, G_MAXINT, 0, G_PARAM_READABLE));	/* CHECKME */
  g_object_class_install_property (gobject_class, ARG_HEIGHT, g_param_spec_int ("height", "Height", "The video height", G_MININT, G_MAXINT, 0, G_PARAM_READABLE));	/* CHECKME */
  g_object_class_install_property (gobject_class, ARG_FRAMES_DISPLAYED, g_param_spec_int ("frames_displayed", "Frames Displayed", "The number of frames displayed so far", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));	/* CHECKME */
  g_object_class_install_property (gobject_class, ARG_FRAME_TIME, g_param_spec_int ("frame_time", "Frame time", "The interval between frames", G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));	/* CHECKME */
  g_object_class_install_property (gobject_class, ARG_HOOK,
      g_param_spec_pointer ("hook", "Hook", "The object receiving the output",
	  G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, ARG_MUTE,
      g_param_spec_boolean ("mute", "Mute", "mute the output ?", FALSE,
	  G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_REPAINT,
      g_param_spec_boolean ("repaint", "Repaint", "repaint the current frame",
	  FALSE, G_PARAM_WRITABLE));
  g_object_class_install_property (gobject_class, ARG_DEMO,
      g_param_spec_int ("demo", "Demo", "demo mode (shows 3D capabilities)", 0,
	  1, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_DUMP,
      g_param_spec_boolean ("dump", "Dump",
	  "stores sequence of frames in .ppm files", FALSE, G_PARAM_READWRITE));

  gobject_class->set_property = gst_glsink_set_property;
  gobject_class->get_property = gst_glsink_get_property;

  /*gobject_class->dispose = gst_glsink_dispose; */

  gstelement_class->change_state = gst_glsink_change_state;
  gstelement_class->set_clock = gst_glsink_set_clock;

  /* plugins */
  klass->plugins = NULL;
  klass->plugins = g_list_append (klass->plugins, get_gl_rgbimage_plugin ());
  klass->plugins = g_list_append (klass->plugins, get_gl_nvimage_plugin ());
}


/*
  GLSink has its own Buffer management - this allows special plugins to create special memory areas for
  buffer upload 
*/
static void
gst_glsink_init (GstGLSink * sink)
{
  sink->sinkpad = gst_pad_new_from_template (sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (sink), sink->sinkpad);
  gst_pad_set_chain_function (sink->sinkpad, gst_glsink_chain);
  gst_pad_set_link_function (sink->sinkpad, gst_glsink_sinkconnect);
  gst_pad_set_getcaps_function (sink->sinkpad, gst_glsink_getcaps);
  gst_pad_set_bufferpool_function (sink->sinkpad, gst_glsink_get_bufferpool);

  sink->last_image = NULL;
  sink->width = 0;
  sink->height = 0;
  sink->muted = FALSE;
  sink->clock = NULL;
  GST_FLAG_SET (sink, GST_ELEMENT_THREAD_SUGGESTED);
  GST_FLAG_SET (sink, GST_ELEMENT_EVENT_AWARE);

  /* create bufferpool and image cache */
  GST_DEBUG ("glsink: creating bufferpool");
  sink->cache_lock = g_mutex_new ();
  sink->cache = NULL;

  /* plugins */
  sink->plugin = NULL;
  sink->conn = NULL;

  /* do initialization of default hook here */
  gst_glxwindow_new (sink);
  //printf("GLSink_init: Current context %p\n", glXGetCurrentContext());
  gst_glxwindow_unhook_context (sink->hook);
}

/** frees the temporary connection that tests the window system capabilities */
static void
gst_glsink_release_conn (GstGLSink * sink)
{
  if (sink->conn == NULL)
    return;

  /* free last image if any */
  if (sink->last_image != NULL) {
    gst_buffer_unref (sink->last_image);
    sink->last_image = NULL;
  }
  /* free cache */
  g_mutex_lock (sink->cache_lock);
  while (sink->cache) {
    sink->plugin->free_image ((GstImageData *) sink->cache->data);
    sink->cache = g_list_delete_link (sink->cache, sink->cache);
  }
  g_mutex_unlock (sink->cache_lock);

  /* release connection */
  sink->conn->free_conn (sink->conn);
  sink->conn = NULL;
}

static void
gst_glsink_append_cache (GstGLSink * sink, GstImageData * image)
{
  g_mutex_lock (sink->cache_lock);
  sink->cache = g_list_prepend (sink->cache, image);
  g_mutex_unlock (sink->cache_lock);
}

#if 0
/* 
   Create a new buffer to hand up the chain.
   This allows the plugins to make its own decoding buffers
 */
static GstBuffer *
gst_glsink_buffer_new (GstBufferPool * pool, gint64 location,
    guint size, gpointer user_data)
{
  GstGLSink *sink;
  GstBuffer *buffer;
  GstImageData *image;

  sink = GST_GLSINK (user_data);

  /* If cache is non-empty, get buffer from there */
  if (sink->cache != NULL) {
    g_mutex_lock (sink->cache_lock);
    image = (GstImageData *) sink->cache->data;
    sink->cache = g_list_delete_link (sink->cache, sink->cache);
    g_mutex_unlock (sink->cache_lock);
  } else {
    /* otherwise, get one from the plugin */
    image = sink->plugin->get_image (sink->hook, sink->conn);
  }

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = image->data;
  GST_BUFFER_SIZE (buffer) = image->size;
  GST_BUFFER_POOL_PRIVATE (buffer) = image;

  return buffer;
}

/*
  Free a buffer that the chain doesn't need anymore. 
*/
static void
gst_glsink_buffer_free (GstBufferPool * pool, GstBuffer * buffer,
    gpointer user_data)
{
  GstGLSink *sink =
      GST_GLSINK (gst_buffer_pool_get_user_data (GST_BUFFER_BUFFERPOOL
	  (buffer)));

  gst_glsink_append_cache (sink,
      (GstImageData *) GST_BUFFER_POOL_PRIVATE (buffer));

  /* set to NULL so the data is not freed */
  GST_BUFFER_DATA (buffer) = NULL;

  gst_buffer_default_free (buffer);
}
#endif

/* 
   Set the caps that the application desires. 
   Go through the plugin list, finding the plugin that first fits the given parameters 
*/
static gboolean
gst_glsink_set_caps (GstGLSink * sink, GstCaps * caps)
{
  g_warning ("in glsink set caps!\n");
  printf ("Getting GLstring, context is %p\n", glXGetCurrentContext ());

  GList *list = ((GstGLSinkClass *) G_OBJECT_GET_CLASS (sink))->plugins;
  GstImageConnection *conn = NULL;

  while (list) {
    printf ("AGetting GLstring, context is %p\n", glXGetCurrentContext ());
    GstImagePlugin *plugin = (GstImagePlugin *) list->data;

    if ((conn = plugin->set_caps (sink->hook, caps)) != NULL) {
      //gst_glsink_release_conn (sink);
      printf ("BGetting GLstring, context is %p\n", glXGetCurrentContext ());
      sink->conn = conn;
      printf ("CGetting GLstring, context is %p\n", glXGetCurrentContext ());
      sink->plugin = plugin;
      sink->conn->open_conn (sink->conn, sink->hook);
      return TRUE;
    }
    list = g_list_next (list);
  }
  return FALSE;
}

/**
Link the input video sink internally.
*/
static GstPadLinkReturn
gst_glsink_sinkconnect (GstPad * pad, GstCaps * caps)
{
  g_warning ("in glsink sinkconnect!\n");
  GstGLSink *sink;
  guint32 fourcc, print_format, result;

  sink = GST_GLSINK (gst_pad_get_parent (pad));

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_glxwindow_hook_context (sink->hook);
  /* try to set the caps on the output */
  result = gst_glsink_set_caps (sink, caps);
  gst_glxwindow_unhook_context (sink->hook);

  if (result == FALSE) {
    return GST_PAD_LINK_REFUSED;
  }

  /* remember width & height */
  gst_caps_get_int (caps, "width", &sink->width);
  gst_caps_get_int (caps, "height", &sink->height);

  gst_caps_get_fourcc_int (caps, "format", &fourcc);
  print_format = GULONG_FROM_LE (fourcc);
  GST_DEBUG ("glsink: setting %08x (%4.4s) %dx%d\n",
      fourcc, (gchar *) & print_format, sink->width, sink->height);

  /* emit signal */
  g_object_freeze_notify (G_OBJECT (sink));
  g_object_notify (G_OBJECT (sink), "width");
  g_object_notify (G_OBJECT (sink), "height");
  g_object_thaw_notify (G_OBJECT (sink));

  return GST_PAD_LINK_OK;
}
static GstCaps *
gst_glsink_getcaps (GstPad * pad, GstCaps * caps)
{
  g_warning ("in glsink get caps!\n");
  /* what is the "caps" parameter good for? */
  GstGLSink *sink = GST_GLSINK (gst_pad_get_parent (pad));
  GstCaps *ret = NULL;
  GList *list = ((GstGLSinkClass *) G_OBJECT_GET_CLASS (sink))->plugins;

  gst_glxwindow_hook_context (sink->hook);
  while (list) {
    ret =
	gst_caps_append (ret,
	((GstImagePlugin *) list->data)->get_caps (sink->hook));
    list = g_list_next (list);
  }

  gst_glxwindow_unhook_context (sink->hook);
  return ret;
}

static void
gst_glsink_set_clock (GstElement * element, GstClock * clock)
{
  GstGLSink *sink = GST_GLSINK (element);

  sink->clock = clock;
}
static void
gst_glsink_chain (GstPad * pad, GstData * _data)
{
  //g_warning("in glsink_chain!\n");
  GstBuffer *buf = GST_BUFFER (_data);
  GstGLSink *sink;

  GstBuffer *buffer;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  sink = GST_GLSINK (gst_pad_get_parent (pad));

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      default:
	gst_pad_event_default (pad, event);
    }
    return;
  }
  GST_DEBUG ("glsink: clock wait: %llu %u",
      GST_BUFFER_TIMESTAMP (buf), GST_BUFFER_SIZE (buf));

#if 0
  GstClockTime time = GST_BUFFER_TIMESTAMP (buf);
  static int frame_drops = 0;

  if (sink->clock && time != -1) {
    if (time < gst_clock_get_time (sink->clock)) {
      g_warning ("Frame drop (%d consecutive) !!", frame_drops);
      /* we are going to drop late buffers */
      gst_buffer_unref (buf);
      frame_drops++;
      return;
    }
    frame_drops = 0;		// we made it - reset time

    GstClockReturn ret;
    GstClockID id =
	gst_clock_new_single_shot_id (sink->clock, GST_BUFFER_TIMESTAMP (buf));

    ret = gst_element_clock_wait (GST_ELEMENT (sink), id, NULL);
    gst_clock_id_free (id);

    /* we are going to drop early buffers */
    if (ret == GST_CLOCK_EARLY) {
      gst_buffer_unref (buf);
      return;
    }
  }
#endif

  /* call the notify _before_ displaying so the handlers can react */
  sink->frames_displayed++;
  g_object_notify (G_OBJECT (sink), "frames_displayed");

  if (!sink->muted) {
    if (glXGetCurrentContext () == NULL) {
      printf ("Rehooking window !\n");
      gst_glxwindow_hook_context (sink->hook);

#if 1
      GST_DEBUG ("Initializing OpenGL parameters\n");
      /* initialize OpenGL drawing */
      glEnable (GL_DEPTH_TEST);
      glEnable (GL_TEXTURE_2D);
      glClearDepth (1.0f);
      glClearColor (0, 0, 0, 0);

      glEnable (GL_AUTO_NORMAL);	// let OpenGL generate the Normals

      glDisable (GL_BLEND);
      glDisable (GL_CULL_FACE);
      glPolygonMode (GL_FRONT, GL_FILL);
      glPolygonMode (GL_BACK, GL_FILL);

      glShadeModel (GL_SMOOTH);
      glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

      GstGLImageInfo *window = (GstGLImageInfo *) sink->hook;
      int w = window->width, h = window->height;

      glViewport (0, 0, (GLint) w, (GLint) h);
      glMatrixMode (GL_PROJECTION);
      glLoadIdentity ();

      GLfloat aspect = (GLfloat) w / (GLfloat) h;

      glFrustum (-aspect, aspect, -1.0, 1.0, 5.0, 500.0);

#endif
      gst_glxwindow_unhook_context (sink->hook);
      gst_glxwindow_hook_context (sink->hook);
      glMatrixMode (GL_MODELVIEW);
#if 0
      sink->hook->free_info (sink->hook);
      printf ("Reallocating window brutally !\n");
      gst_glxwindow_new (sink);
#endif
    }

    /* free last_image, if any */
    if (sink->last_image != NULL)
      gst_buffer_unref (sink->last_image);
    if (sink->bufferpool && GST_BUFFER_BUFFERPOOL (buf) == sink->bufferpool) {

      // awful hack ! But I currently have no other solution without changing the API
      sink->hook->demo = sink->demo;
      sink->hook->dumpvideo = sink->dumpvideo;

      sink->plugin->put_image (sink->hook,
	  (GstImageData *) GST_BUFFER_POOL_PRIVATE (buf));
      sink->last_image = buf;
    } else {
      buffer =
	  gst_buffer_new_from_pool (gst_glsink_get_bufferpool (sink->sinkpad),
	  0, GST_BUFFER_SIZE (buf));
      memcpy (GST_BUFFER_DATA (buffer), GST_BUFFER_DATA (buf),
	  GST_BUFFER_SIZE (buf) >
	  GST_BUFFER_SIZE (buffer) ? GST_BUFFER_SIZE (buffer) :
	  GST_BUFFER_SIZE (buf));

      sink->plugin->put_image (sink->hook,
	  (GstImageData *) GST_BUFFER_POOL_PRIVATE (buffer));

      sink->last_image = buffer;
      gst_buffer_unref (buf);
    }

    //gst_glxwindow_unhook_context(sink->hook);
  }

}


static void
gst_glsink_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  //g_warning("in set_property!\n");
  GstGLSink *sink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_GLSINK (object));

  sink = GST_GLSINK (object);

  switch (prop_id) {
    case ARG_FRAMES_DISPLAYED:
      sink->frames_displayed = g_value_get_int (value);
      g_object_notify (object, "frames_displayed");
      break;
    case ARG_FRAME_TIME:
      sink->frame_time = g_value_get_int (value);
      break;
    case ARG_HOOK:
      if (sink->hook) {
	sink->hook->free_info (sink->hook);
      }
      sink->hook = g_value_get_pointer (value);
      break;
    case ARG_MUTE:
      sink->muted = g_value_get_boolean (value);
      g_object_notify (object, "mute");
      break;
    case ARG_DEMO:
      sink->demo = g_value_get_int (value);
      g_object_notify (object, "demo");
      break;
    case ARG_DUMP:
      sink->dumpvideo = g_value_get_boolean (value);
      g_object_notify (object, "dump");
      break;
    case ARG_REPAINT:
      if (sink->last_image != NULL) {
	sink->plugin->put_image (sink->hook,
	    (GstImageData *) GST_BUFFER_POOL_PRIVATE (sink->last_image));
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glsink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  //g_warning("in get_property!\n");
  GstGLSink *sink;

  /* it's not null if we got it, but it might not be ours */
  sink = GST_GLSINK (object);

  switch (prop_id) {
    case ARG_WIDTH:
      g_value_set_int (value, sink->width);
      break;
    case ARG_HEIGHT:
      g_value_set_int (value, sink->height);
      break;
    case ARG_FRAMES_DISPLAYED:
      g_value_set_int (value, sink->frames_displayed);
      break;
    case ARG_FRAME_TIME:
      g_value_set_int (value, sink->frame_time / 1000000);
      break;
    case ARG_MUTE:
      g_value_set_boolean (value, sink->muted);
      break;
    case ARG_DEMO:
      g_value_set_int (value, sink->demo);
      break;
    case ARG_DUMP:
      g_value_set_boolean (value, sink->dumpvideo);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstElementStateReturn
gst_glsink_change_state (GstElement * element)
{
  //g_warning("in change_state!\n");
  GstGLSink *sink;

  sink = GST_GLSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
    {
      //g_warning("Going GST_STATE_READY_TO_PAUSED: %p", sink->conn);
    }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
    {
      //g_warning("Going GST_STATE_PAUSED_TO_PLAYING: %p", sink->conn);
    }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (sink->conn)
	sink->conn->close_conn (sink->conn, sink->hook);
      if (sink->last_image) {
	gst_buffer_unref (sink->last_image);
	sink->last_image = NULL;
      }
      break;
    case GST_STATE_READY_TO_NULL:
      gst_glsink_release_conn (sink);
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  /* Loading the library containing GstVideoSink, our parent object */
  if (!gst_library_load ("gstvideo"))
    return FALSE;

  /* this is needed later on in the _real_ init (during a gst-launch) */
  sink_template = gst_pad_template_new ("sink",
      GST_PAD_SINK, GST_PAD_ALWAYS, NULL);

  if (!gst_element_register (plugin, "glsink", GST_RANK_NONE, GST_TYPE_GLSINK))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "glsink",
    "An OpenGL based video sink - uses OpenGL and GLX to draw video, utilizing different acceleration options",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN);
