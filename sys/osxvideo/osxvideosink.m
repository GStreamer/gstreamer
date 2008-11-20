/* GStreamer
 * OSX video sink
 * Copyright (C) 2004-6 Zaheer Abbas Merali <zaheerabbas at merali dot org>
 * Copyright (C) 2007 Pioneers of the Inevitable <songbird@songbirdnest.com>
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
 *
 * The development of this code was made possible due to the involvement of
 * Pioneers of the Inevitable, the creators of the Songbird Music player.
 * 
 */

/**
 * SECTION:element-osxvideosink
 *
 * <refsect2>
 * <para>
 * The OSXVideoSink renders video frames to a MacOSX window. The video output
 * can be directed to a window embedded in an existing NSApp. This can be done
 * by setting the "embed" property to #TRUE. When the NSView to be embedded is
 * created an element #GstMessage with a name of 'have-ns-view' will be created
 * and posted on the bus. The pointer to the NSView to embed will be in the
 * 'nsview' field of that message. If no embedding is requested, the plugin will
 * create a standalone window.
 * </para>
 * <title>Examples</title>
 * <para>
 * Simple timeline to test the sink :
 * <programlisting>
 * gst-launch-0.10 -v videotestsrc ! osxvideosink
 * </programlisting>
 * </para>
 * </refsect2>
 */

#include "config.h"

/* Object header */
#include "osxvideosink.h"
#include <unistd.h>
#import "cocoawindow.h"

/* Debugging category */
GST_DEBUG_CATEGORY (gst_debug_osx_video_sink);
#define GST_CAT_DEFAULT gst_debug_osx_video_sink

/* ElementFactory information */
static const GstElementDetails gst_osx_video_sink_details =
GST_ELEMENT_DETAILS ("OSX Video sink",
    "Sink/Video",
    "OSX native videosink",
    "Zaheer Abbas Merali <zaheerabbas at merali dot org>");

/* Default template - initiated with class struct to allow gst-register to work
   without X running */
static GstStaticPadTemplate gst_osx_video_sink_sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "framerate = (fraction) [ 0, MAX ], "
        "width = (int) [ 1, MAX ], "
	"height = (int) [ 1, MAX ], "
#if G_BYTE_ORDER == G_BIG_ENDIAN
       "format = (fourcc) YUY2")
#else
        "format = (fourcc) UYVY")
#endif
    );

// much of the following cocoa NSApp code comes from libsdl and libcaca
@implementation NSApplication(Gst)
- (void)setRunning
{
    _running = 1;
}
@end

@implementation GstAppDelegate : NSObject
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication *)sender
{
    // destroy stuff here!
    GST_DEBUG("Kill me please!");
    return NSTerminateNow;
}
@end


enum
{
  ARG_0,
  ARG_EMBED,
  ARG_FULLSCREEN
      /* FILL ME */
};

static GstVideoSinkClass *parent_class = NULL;


/* cocoa event loop - needed if not run in own app */
static void
cocoa_event_loop (GstOSXVideoSink * vsink)
{
  NSAutoreleasePool *pool;

  GST_DEBUG_OBJECT (vsink, "Entering event loop");
  
  pool = [[NSAutoreleasePool alloc] init];

  while ([NSApp isRunning]) {
    NSEvent *event = [NSApp nextEventMatchingMask:NSAnyEventMask
      			    untilDate:[NSDate distantPast]
			    inMode:NSDefaultRunLoopMode dequeue:YES ];
    if ( event == nil ) {
      g_usleep (2000);
      break;
    } else {
      switch ([event type]) {
      default: //XXX Feed me please
        [NSApp sendEvent:event];
        break;
      }
      /* loop */
    }
  }

  [pool release];
}

static NSString *
GetApplicationName(void)
{
    NSDictionary *dict;
    NSString *appName = 0;

    /* Determine the application name */
    dict = (NSDictionary *)CFBundleGetInfoDictionary(CFBundleGetMainBundle());
    if (dict)
        appName = [dict objectForKey: @"CFBundleName"];
    
    if (![appName length])
        appName = [[NSProcessInfo processInfo] processName];

    return appName;
}

static void
CreateApplicationMenus(void)
{
    NSString *appName;
    NSString *title;
    NSMenu *appleMenu;
    NSMenu *windowMenu;
    NSMenuItem *menuItem;
    
    /* Create the main menu bar */
    [NSApp setMainMenu:[[NSMenu alloc] init]];

    /* Create the application menu */
    appName = GetApplicationName();
    appleMenu = [[NSMenu alloc] initWithTitle:@""];

        /* Add menu items */
    title = [@"About " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];
    
    title = [@"Hide " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(hide:) keyEquivalent:@/*"h"*/""];

    menuItem = (NSMenuItem *)[appleMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@/*"h"*/""];
    [menuItem setKeyEquivalentModifierMask:(NSAlternateKeyMask|NSCommandKeyMask)];

    [appleMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];

    [appleMenu addItem:[NSMenuItem separatorItem]];

    title = [@"Quit " stringByAppendingString:appName];
    [appleMenu addItemWithTitle:title action:@selector(terminate:) keyEquivalent:@/*"q"*/""];
    
    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:appleMenu];
    [[NSApp mainMenu] addItem:menuItem];
    [menuItem release];

    /* Tell the application object that this is now the application menu */
    [NSApp setAppleMenu:appleMenu];
    [appleMenu release];


    /* Create the window menu */
    windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
    
    /* "Minimize" item */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@/*"m"*/""];
    [windowMenu addItem:menuItem];
    [menuItem release];
    
    /* Put menu into the menubar */
    menuItem = [[NSMenuItem alloc] initWithTitle:@"Window" action:nil keyEquivalent:@""];
    [menuItem setSubmenu:windowMenu];
    [[NSApp mainMenu] addItem:menuItem];
    [menuItem release];
    
    /* Tell the application object that this is now the window menu */
    [NSApp setWindowsMenu:windowMenu];
    [windowMenu release];
}

/* This function handles osx window creation */
static GstOSXWindow *
gst_osx_video_sink_osxwindow_new (GstOSXVideoSink * osxvideosink, gint width,
    gint height)
{
  NSRect rect;
  GstOSXWindow *osxwindow = NULL;

  g_return_val_if_fail (GST_IS_OSX_VIDEO_SINK (osxvideosink), NULL);

  GST_DEBUG_OBJECT (osxvideosink, "Creating new OSX window");

  osxwindow = g_new0 (GstOSXWindow, 1);

  osxwindow->width = width;
  osxwindow->height = height;
  osxwindow->internal = TRUE;
  osxwindow->pool = [[NSAutoreleasePool alloc] init];

  if (osxvideosink->embed == FALSE) {
    ProcessSerialNumber psn;
    unsigned int mask =  NSTitledWindowMask      	|
                         NSClosableWindowMask    	|
                         NSResizableWindowMask   	|
			 NSTexturedBackgroundWindowMask |
			 NSMiniaturizableWindowMask;

    rect.origin.x = 100.0;
    rect.origin.y = 100.0;
    rect.size.width = (float) osxwindow->width;
    rect.size.height = (float) osxwindow->height;

    if (!GetCurrentProcess(&psn)) {
        TransformProcessType(&psn, kProcessTransformToForegroundApplication);
        SetFrontProcess(&psn);
    }

    [NSApplication sharedApplication];
 
    osxwindow->win =[[GstOSXVideoSinkWindow alloc]
                         initWithContentRect: rect
                         styleMask: mask
                         backing: NSBackingStoreBuffered
                         defer: NO
                         screen: nil];
    GST_DEBUG("VideoSinkWindow created, %p", osxwindow->win);
    [osxwindow->win autorelease];
    [NSApplication sharedApplication];
    [osxwindow->win makeKeyAndOrderFront:NSApp];
    osxwindow->gstview =[osxwindow->win gstView];
    [osxwindow->gstview autorelease];
    if (osxvideosink->fullscreen)
      [osxwindow->gstview setFullScreen:YES];

    CreateApplicationMenus();

    [NSApp finishLaunching];
    [NSApp setDelegate:[[GstAppDelegate alloc] init]];

    [NSApp setRunning];
    g_static_rec_mutex_init (&osxvideosink->event_task_lock);
    osxvideosink->event_task = gst_task_create ((GstTaskFunction)cocoa_event_loop,
                                                osxvideosink);
    gst_task_set_lock (osxvideosink->event_task, &osxvideosink->event_task_lock);
    gst_task_start (osxvideosink->event_task);
  } else {
    GstStructure *s;
    GstMessage *msg;
    gchar * tmp;
    /* Needs to be embedded */

    rect.origin.x = 0.0;
    rect.origin.y = 0.0;
    rect.size.width = (float) osxwindow->width;
    rect.size.height = (float) osxwindow->height;
    osxwindow->gstview =[[GstGLView alloc] initWithFrame:rect];
    [osxwindow->gstview autorelease];
    
    s = gst_structure_new ("have-ns-view",
			   "nsview", G_TYPE_POINTER, osxwindow->gstview,
			   nil);

    tmp = gst_structure_to_string (s);
    GST_DEBUG_OBJECT (osxvideosink, "Sending message %s (with view %p)",
		      tmp, osxwindow->gstview);
    g_free (tmp);

    msg = gst_message_new_element (GST_OBJECT (osxvideosink), s);
    gst_element_post_message (GST_ELEMENT (osxvideosink), msg);

    GST_LOG_OBJECT (osxvideosink, "'have-ns-view' message sent");
  }
  return osxwindow;
}

/* This function destroys a GstXWindow */
static void
gst_osx_video_sink_osxwindow_destroy (GstOSXVideoSink * osxvideosink,
    GstOSXWindow * osxwindow)
{
  g_return_if_fail (osxwindow != NULL);
  g_return_if_fail (GST_IS_OSX_VIDEO_SINK (osxvideosink));

  [osxwindow->pool release];

  if (osxvideosink->event_task) {
    gst_task_join (osxvideosink->event_task);
    gst_object_unref (osxvideosink->event_task);
    osxvideosink->event_task = NULL;
    g_static_rec_mutex_free (&osxvideosink->event_task_lock);
  }

  g_free (osxwindow);
}

/* This function resizes a GstXWindow */
static void
gst_osx_video_sink_osxwindow_resize (GstOSXVideoSink * osxvideosink,
    GstOSXWindow * osxwindow, guint width, guint height)
{
  NSAutoreleasePool *subPool = [[NSAutoreleasePool alloc] init];
  g_return_if_fail (osxwindow != NULL);
  g_return_if_fail (GST_IS_OSX_VIDEO_SINK (osxvideosink));

  osxwindow->width = width;
  osxwindow->height = height;

  GST_DEBUG_OBJECT (osxvideosink, "Resizing window to (%d,%d)", width, height);
  if (osxwindow->win) {
    /* Call relevant cocoa function to resize window */
    NSSize size;
    size.width = width;
    size.height = height;

    NSLog(@"osxwindow->win = %@", osxwindow->win);
    GST_DEBUG_OBJECT (osxvideosink, "Calling setContentSize on %p", osxwindow->win); 
    [osxwindow->win setContentSize:size];
  }
  else {
    /* Directly resize the underlying view */
    GST_DEBUG_OBJECT (osxvideosink, "Calling setVideoSize on %p", osxwindow->gstview); 
    [osxwindow->gstview setVideoSize:width :height];
  }
  [subPool release];
}

static void
gst_osx_video_sink_osxwindow_clear (GstOSXVideoSink * osxvideosink,
    GstOSXWindow * osxwindow)
{

  g_return_if_fail (osxwindow != NULL);
  g_return_if_fail (GST_IS_OSX_VIDEO_SINK (osxvideosink));

}


/* Element stuff */
static gboolean
gst_osx_video_sink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstOSXVideoSink *osxvideosink;
  GstStructure *structure;
  gboolean res, result = FALSE;
  gint video_width, video_height;

  osxvideosink = GST_OSX_VIDEO_SINK (bsink);

  GST_DEBUG_OBJECT (osxvideosink, "caps: %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  res = gst_structure_get_int (structure, "width", &video_width);
  res &= gst_structure_get_int (structure, "height", &video_height);

  if (!res) {
    goto beach;
  }

  GST_DEBUG_OBJECT (osxvideosink, "our format is: %dx%d video",
      video_width, video_height);

  GST_VIDEO_SINK_WIDTH (osxvideosink) = video_width;
  GST_VIDEO_SINK_HEIGHT (osxvideosink) = video_height;

  gst_osx_video_sink_osxwindow_resize (osxvideosink, osxvideosink->osxwindow,
      video_width, video_height);
  result = TRUE;

beach:
  return result;

}

static GstStateChangeReturn
gst_osx_video_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstOSXVideoSink *osxvideosink;

  osxvideosink = GST_OSX_VIDEO_SINK (element);

  GST_DEBUG_OBJECT (osxvideosink, "%s => %s", 
		    gst_element_state_get_name(GST_STATE_TRANSITION_CURRENT (transition)),
		    gst_element_state_get_name(GST_STATE_TRANSITION_NEXT (transition)));

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      /* Creating our window and our image */
      if (!osxvideosink->osxwindow) {
        GST_VIDEO_SINK_WIDTH (osxvideosink) = 320;
        GST_VIDEO_SINK_HEIGHT (osxvideosink) = 240;
        osxvideosink->osxwindow =
            gst_osx_video_sink_osxwindow_new (osxvideosink,
            GST_VIDEO_SINK_WIDTH (osxvideosink),
            GST_VIDEO_SINK_HEIGHT (osxvideosink));
        gst_osx_video_sink_osxwindow_clear (osxvideosink,
            osxvideosink->osxwindow);
      } else {
        if (osxvideosink->osxwindow->internal)
          gst_osx_video_sink_osxwindow_resize (osxvideosink,
              osxvideosink->osxwindow, GST_VIDEO_SINK_WIDTH (osxvideosink),
              GST_VIDEO_SINK_HEIGHT (osxvideosink));
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      GST_DEBUG ("ready to paused");
      if (osxvideosink->osxwindow)
        gst_osx_video_sink_osxwindow_clear (osxvideosink,
            osxvideosink->osxwindow);
      osxvideosink->time = 0;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      osxvideosink->sw_scaling_failed = FALSE;
      GST_VIDEO_SINK_WIDTH (osxvideosink) = 0;
      GST_VIDEO_SINK_HEIGHT (osxvideosink) = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:

      if (osxvideosink->osxwindow) {
        gst_osx_video_sink_osxwindow_destroy (osxvideosink,
            osxvideosink->osxwindow);
        osxvideosink->osxwindow = NULL;
      }
      break;
  }

  return (GST_ELEMENT_CLASS (parent_class))->change_state (element, transition);

}

static GstFlowReturn
gst_osx_video_sink_show_frame (GstBaseSink * bsink, GstBuffer * buf)
{
  GstOSXVideoSink *osxvideosink;
  char *viewdata;

  osxvideosink = GST_OSX_VIDEO_SINK (bsink);
  viewdata = [osxvideosink->osxwindow->gstview getTextureBuffer];

  GST_DEBUG ("show_frame");
  memcpy (viewdata, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  [osxvideosink->osxwindow->gstview displayTexture];

  return GST_FLOW_OK;
}

/* Buffer management */



/* =========================================== */
/*                                             */
/*              Init & Class init              */
/*                                             */
/* =========================================== */

static void
gst_osx_video_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOSXVideoSink *osxvideosink;

  g_return_if_fail (GST_IS_OSX_VIDEO_SINK (object));

  osxvideosink = GST_OSX_VIDEO_SINK (object);

  switch (prop_id) {
    case ARG_EMBED:
      osxvideosink->embed = g_value_get_boolean (value);
      break;
    case ARG_FULLSCREEN:
      osxvideosink->fullscreen = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osx_video_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOSXVideoSink *osxvideosink;

  g_return_if_fail (GST_IS_OSX_VIDEO_SINK (object));

  osxvideosink = GST_OSX_VIDEO_SINK (object);

  switch (prop_id) {
    case ARG_EMBED:
      g_value_set_boolean (value, osxvideosink->embed);
      break;
    case ARG_FULLSCREEN:
      g_value_set_boolean (value, osxvideosink->fullscreen);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_osx_video_sink_init (GstOSXVideoSink * osxvideosink)
{

  osxvideosink->osxwindow = NULL;

  osxvideosink->pixel_width = osxvideosink->pixel_height = 1;
  osxvideosink->sw_scaling_failed = FALSE;
  osxvideosink->embed = FALSE;
  osxvideosink->fullscreen = FALSE;

}

static void
gst_osx_video_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_osx_video_sink_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_osx_video_sink_sink_template_factory));
}

static void
gst_osx_video_sink_class_init (GstOSXVideoSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;


  parent_class = g_type_class_ref (GST_TYPE_VIDEO_SINK);

  gobject_class->set_property = gst_osx_video_sink_set_property;
  gobject_class->get_property = gst_osx_video_sink_get_property;

  gstbasesink_class->set_caps = gst_osx_video_sink_setcaps;
  gstbasesink_class->preroll = gst_osx_video_sink_show_frame;
  gstbasesink_class->render = gst_osx_video_sink_show_frame;
  gstelement_class->change_state = gst_osx_video_sink_change_state;

  /**
   * GstOSXVideoSink:embed
   *
   * Set to #TRUE if you are embedding the video window in an application.
   *
   **/

  g_object_class_install_property (gobject_class, ARG_EMBED,
      g_param_spec_boolean ("embed", "embed", "When enabled, it  "
          "can be embedded", FALSE, G_PARAM_READWRITE));

  /**
   * GstOSXVideoSink:fullscreen
   *
   * Set to #TRUE to have the video displayed in fullscreen.
   **/

  g_object_class_install_property (gobject_class, ARG_FULLSCREEN,
      g_param_spec_boolean ("fullscreen", "fullscreen",
          "When enabled, the view  " "is fullscreen", FALSE,
          G_PARAM_READWRITE));
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
gst_osx_video_sink_get_type (void)
{
  static GType osxvideosink_type = 0;

  if (!osxvideosink_type) {
    static const GTypeInfo osxvideosink_info = {
      sizeof (GstOSXVideoSinkClass),
      gst_osx_video_sink_base_init,
      NULL,
      (GClassInitFunc) gst_osx_video_sink_class_init,
      NULL,
      NULL,
      sizeof (GstOSXVideoSink),
      0,
      (GInstanceInitFunc) gst_osx_video_sink_init,
    };

    osxvideosink_type = g_type_register_static (GST_TYPE_VIDEO_SINK,
        "GstOSXVideoSink", &osxvideosink_info, 0);

  }

  return osxvideosink_type;
}

static gboolean
plugin_init (GstPlugin * plugin)
{

  if (!gst_element_register (plugin, "osxvideosink",
          GST_RANK_PRIMARY, GST_TYPE_OSX_VIDEO_SINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (gst_debug_osx_video_sink, "osxvideosink", 0,
      "osxvideosink element");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "osxvideo",
    "OSX native video output plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
