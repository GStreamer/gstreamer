# iOS tutorial 3: Video

## Goal

![screenshot]

Except for [](tutorials/basic/toolkit-integration.md),
which embedded a video window on a GTK application, all tutorials so far
relied on GStreamer video sinks to create a window to display their
contents. The video sink on iOS is not capable of creating its own
window, so a drawing surface always needs to be provided. This tutorial
shows:

  - How to allocate a drawing surface on the Xcode Interface Builder and
    pass it to GStreamer

## Introduction

Since iOS does not provide a windowing system, a GStreamer video sink
cannot create pop-up windows as it would do on a Desktop platform.
Fortunately, the `VideoOverlay` interface allows providing video sinks with
an already created window onto which they can draw, as we have seen
in [](tutorials/basic/toolkit-integration.md).

In this tutorial, a `UIView` widget (actually, a subclass of it) is
placed on the main storyboard. In the `viewDidLoad` method of the
`ViewController`, we pass a pointer to this `UIView `to the instance of
the `GStreamerBackend`, so it can tell the video sink where to draw.

## The User Interface

The storyboard from the previous tutorial is expanded: A `UIView `is
added over the toolbar and pinned to all sides so it takes up all
available space (`video_container_view` outlet). Inside it, another
`UIView `is added (`video_view` outlet) which contains the actual video,
centered to its parent, and with a size that adapts to the media size
(through the `video_width_constraint` and `video_height_constraint`
outlets):

**ViewController.h**

```
#import <UIKit/UIKit.h>
#import "GStreamerBackendDelegate.h"

@interface ViewController : UIViewController <GStreamerBackendDelegate> {
    IBOutlet UILabel *message_label;
    IBOutlet UIBarButtonItem *play_button;
    IBOutlet UIBarButtonItem *pause_button;
    IBOutlet UIView *video_view;
    IBOutlet UIView *video_container_view;
    IBOutlet NSLayoutConstraint *video_width_constraint;
    IBOutlet NSLayoutConstraint *video_height_constraint;
}

-(IBAction) play:(id)sender;
-(IBAction) pause:(id)sender;

/* From GStreamerBackendDelegate */
-(void) gstreamerInitialized;
-(void) gstreamerSetUIMessage:(NSString *)message;

@end
```

## The View Controller

The `ViewController `class manages the UI, instantiates
the `GStreamerBackend` and also performs some UI-related tasks on its
behalf:

**ViewController.m**

```
#import "ViewController.h"
#import "GStreamerBackend.h"
#import <UIKit/UIKit.h>

@interface ViewController () {
    GStreamerBackend *gst_backend;
    int media_width;
    int media_height;
}

@end

@implementation ViewController

/*
 * Methods from UIViewController
 */

- (void)viewDidLoad
{
    [super viewDidLoad];

    play_button.enabled = FALSE;
    pause_button.enabled = FALSE;

    /* Make these constant for now, later tutorials will change them */
    media_width = 320;
    media_height = 240;

    gst_backend = [[GStreamerBackend alloc] init:self videoView:video_view];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

/* Called when the Play button is pressed */
-(IBAction) play:(id)sender
{
    [gst_backend play];
}

/* Called when the Pause button is pressed */
-(IBAction) pause:(id)sender
{
    [gst_backend pause];
}

- (void)viewDidLayoutSubviews
{
    CGFloat view_width = video_container_view.bounds.size.width;
    CGFloat view_height = video_container_view.bounds.size.height;

    CGFloat correct_height = view_width * media_height / media_width;
    CGFloat correct_width = view_height * media_width / media_height;

    if (correct_height < view_height) {
        video_height_constraint.constant = correct_height;
        video_width_constraint.constant = view_width;
    } else {
        video_width_constraint.constant = correct_width;
        video_height_constraint.constant = view_height;
    }
}

/*
 * Methods from GstreamerBackendDelegate
 */

-(void) gstreamerInitialized
{
    dispatch_async(dispatch_get_main_queue(), ^{
        play_button.enabled = TRUE;
        pause_button.enabled = TRUE;
        message_label.text = @"Ready";
    });
}

-(void) gstreamerSetUIMessage:(NSString *)message
{
    dispatch_async(dispatch_get_main_queue(), ^{
        message_label.text = message;
    });
}

@end
```

We expand the class to remember the width and height of the media we are
currently playing:

```
@interface ViewController () {
    GStreamerBackend *gst_backend;
    int media_width;
    int media_height;
}
```

In later tutorials this data is retrieved from the GStreamer pipeline,
but in this tutorial, for simplicity’s sake, the width and height of the
media is constant and initialized in `viewDidLoad`:

```
- (void)viewDidLoad
{
    [super viewDidLoad];

    play_button.enabled = FALSE;
    pause_button.enabled = FALSE;

    /* Make these constant for now, later tutorials will change them */
    media_width = 320;
    media_height = 240;

    gst_backend = [[GStreamerBackend alloc] init:self videoView:video_view];
}
```

As shown below, the `GStreamerBackend` constructor has also been
expanded to accept another parameter: the `UIView *` where the video
sink should draw.

The rest of the `ViewController `code is the same as the previous
tutorial, except for the code that adapts the `video_view` size to the
media size, respecting its aspect ratio:

```
- (void)viewDidLayoutSubviews
{
    CGFloat view_width = video_container_view.bounds.size.width;
    CGFloat view_height = video_container_view.bounds.size.height;

    CGFloat correct_height = view_width * media_height / media_width;
    CGFloat correct_width = view_height * media_width / media_height;

    if (correct_height < view_height) {
        video_height_constraint.constant = correct_height;
        video_width_constraint.constant = view_width;
    } else {
        video_width_constraint.constant = correct_width;
        video_height_constraint.constant = view_height;
    }
}
```

The `viewDidLayoutSubviews` method is called every time the main view
size has changed (for example, due to a device orientation change) and
the entire layout has been recalculated. At this point, we can access
the `bounds` property of the `video_container_view` to retrieve its new
size and change the `video_view` size accordingly.

The simple algorithm above maximizes either the width or the height of
the `video_view`, while changing the other axis so the aspect ratio of
the media is preserved. The goal is to provide the GStreamer video sink
with a surface of the correct proportions, so it does not need to add
black borders (*letterboxing*), which is a waste of processing power.

The final size is reported to the layout engine by changing the
`constant` field in the width and height `Constraints` of the
`video_view`. These constraints have been created in the storyboard and
are accessible to the `ViewController `through IBOutlets, as is usually
done with other widgets.

## The GStreamer Backend

The `GStreamerBackend` class performs all GStreamer-related tasks and
offers a simplified interface to the application, which does not need to
deal with all the GStreamer details. When it needs to perform any UI
action, it does so through a delegate, which is expected to adhere to
the `GStreamerBackendDelegate` protocol:

**GStreamerBackend.m**

```
#import "GStreamerBackend.h"

#include <gst/gst.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

@interface GStreamerBackend()
-(void)setUIMessage:(gchar*) message;
-(void)app_function;
-(void)check_initialization_complete;
@end

@implementation GStreamerBackend {
    id ui_delegate;        /* Class that we use to interact with the user interface */
    GstElement *pipeline;  /* The running pipeline */
    GstElement *video_sink;/* The video sink element which receives VideoOverlay commands */
    GMainContext *context; /* GLib context used to run the main loop */
    GMainLoop *main_loop;  /* GLib main loop */
    gboolean initialized;  /* To avoid informing the UI multiple times about the initialization */
    UIView *ui_video_view; /* UIView that holds the video */
}

/*
 * Interface methods
 */

-(id) init:(id) uiDelegate videoView:(UIView *)video_view
{
    if (self = [super init])
    {
        self->ui_delegate = uiDelegate;
        self->ui_video_view = video_view;

        GST_DEBUG_CATEGORY_INIT (debug_category, "tutorial-3", 0, "iOS tutorial 3");
        gst_debug_set_threshold_for_name("tutorial-3", GST_LEVEL_DEBUG);

        /* Start the bus monitoring task */
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [self app_function];
        });
    }

    return self;
}

-(void) dealloc
{
    if (pipeline) {
        GST_DEBUG("Setting the pipeline to NULL");
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
}

-(void) play
{
    if(gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        [self setUIMessage:"Failed to set pipeline to playing"];
    }
}

-(void) pause
{
    if(gst_element_set_state(pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        [self setUIMessage:"Failed to set pipeline to paused"];
    }
}

/*
 * Private methods
 */

/* Change the message on the UI through the UI delegate */
-(void)setUIMessage:(gchar*) message
{
    NSString *string = [NSString stringWithUTF8String:message];
    if(ui_delegate && [ui_delegate respondsToSelector:@selector(gstreamerSetUIMessage:)])
    {
        [ui_delegate gstreamerSetUIMessage:string];
    }
}

/* Retrieve errors from the bus and show them on the UI */
static void error_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self)
{
    GError *err;
    gchar *debug_info;
    gchar *message_string;

    gst_message_parse_error (msg, &err, &debug_info);
    message_string = g_strdup_printf ("Error received from element %s: %s", GST_OBJECT_NAME (msg->src), err->message);
    g_clear_error (&err);
    g_free (debug_info);
    [self setUIMessage:message_string];
    g_free (message_string);
    gst_element_set_state (self->pipeline, GST_STATE_NULL);
}

/* Notify UI about pipeline state changes */
static void state_changed_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self)
{
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
    /* Only pay attention to messages coming from the pipeline, not its children */
    if (GST_MESSAGE_SRC (msg) == GST_OBJECT (self->pipeline)) {
        gchar *message = g_strdup_printf("State changed to %s", gst_element_state_get_name(new_state));
        [self setUIMessage:message];
        g_free (message);
    }
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
-(void) check_initialization_complete
{
    if (!initialized && main_loop) {
        GST_DEBUG ("Initialization complete, notifying application.");
        if (ui_delegate && [ui_delegate respondsToSelector:@selector(gstreamerInitialized)])
        {
            [ui_delegate gstreamerInitialized];
        }
        initialized = TRUE;
    }
}

/* Main method for the bus monitoring code */
-(void) app_function
{
    GstBus *bus;
    GSource *bus_source;
    GError *error = NULL;

    GST_DEBUG ("Creating pipeline");

    /* Create our own GLib Main Context and make it the default one */
    context = g_main_context_new ();
    g_main_context_push_thread_default(context);

    /* Build pipeline */
    pipeline = gst_parse_launch("videotestsrc ! warptv ! videoconvert ! autovideosink", &error);
    if (error) {
        gchar *message = g_strdup_printf("Unable to build pipeline: %s", error->message);
        g_clear_error (&error);
        [self setUIMessage:message];
        g_free (message);
        return;
    }

    /* Set the pipeline to READY, so it can already accept a window handle */
    gst_element_set_state(pipeline, GST_STATE_READY);

    video_sink = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_VIDEO_OVERLAY);
    if (!video_sink) {
        GST_ERROR ("Could not retrieve video sink");
        return;
    }
    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(video_sink), (guintptr) (id) ui_video_view);

    /* Instruct the bus to emit signals for each received message, and connect to the interesting signals */
    bus = gst_element_get_bus (pipeline);
    bus_source = gst_bus_create_watch (bus);
    g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func, NULL, NULL);
    g_source_attach (bus_source, context);
    g_source_unref (bus_source);
    g_signal_connect (G_OBJECT (bus), "message::error", (GCallback)error_cb, (__bridge void *)self);
    g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, (__bridge void *)self);
    gst_object_unref (bus);

    /* Create a GLib Main Loop and set it to run */
    GST_DEBUG ("Entering main loop...");
    main_loop = g_main_loop_new (context, FALSE);
    [self check_initialization_complete];
    g_main_loop_run (main_loop);
    GST_DEBUG ("Exited main loop");
    g_main_loop_unref (main_loop);
    main_loop = NULL;

    /* Free resources */
    g_main_context_pop_thread_default(context);
    g_main_context_unref (context);
    gst_element_set_state (pipeline, GST_STATE_NULL);
    gst_object_unref (pipeline);

    return;
}

@end
```

The main differences with the previous tutorial are related to the
handling of the `VideoOverlay` interface:

```
@implementation GStreamerBackend {
    id ui_delegate;        /* Class that we use to interact with the user interface */
    GstElement *pipeline;  /* The running pipeline */
    GstElement *video_sink;/* The video sink element which receives VideoOverlay commands */
    GMainContext *context; /* GLib context used to run the main loop */
    GMainLoop *main_loop;  /* GLib main loop */
    gboolean initialized;  /* To avoid informing the UI multiple times about the initialization */
    UIView *ui_video_view; /* UIView that holds the video */
}
```

The class is expanded to keep track of the video sink element in the
pipeline and the `UIView *` onto which rendering is to occur.

```
-(id) init:(id) uiDelegate videoView:(UIView *)video_view
{
    if (self = [super init])
    {
        self->ui_delegate = uiDelegate;
        self->ui_video_view = video_view;

        GST_DEBUG_CATEGORY_INIT (debug_category, "tutorial-3", 0, "iOS tutorial 3");
        gst_debug_set_threshold_for_name("tutorial-3", GST_LEVEL_DEBUG);

        /* Start the bus monitoring task */
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [self app_function];
        });
    }

    return self;
}
```

The constructor accepts the `UIView *` as a new parameter, which, at
this point, is simply remembered in `ui_video_view`.

```
/* Build pipeline */
pipeline = gst_parse_launch("videotestsrc ! warptv ! videoconvert ! autovideosink", &error);
```

Then, in the `app_function`, the pipeline is constructed. This time we
build a video pipeline using a simple `videotestsrc` element with a
`warptv` to add some spice. The video sink is `autovideosink`, which
choses the appropriate sink for the platform (currently,
`glimagesink` is the only option for
iOS).

```
/* Set the pipeline to READY, so it can already accept a window handle */
gst_element_set_state(pipeline, GST_STATE_READY);

video_sink = gst_bin_get_by_interface(GST_BIN(pipeline), GST_TYPE_VIDEO_OVERLAY);
if (!video_sink) {
    GST_ERROR ("Could not retrieve video sink");
    return;
}
gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(video_sink), (guintptr) (id) ui_video_view);
```

Once the pipeline is built, we set it to READY. In this state, dataflow
has not started yet, but the caps of adjacent elements have been
verified to be compatible and their pads have been linked. Also, the
`autovideosink` has already instantiated the actual video sink so we can
ask for it immediately.

The `gst_bin_get_by_interface()` method will examine the whole pipeline
and return a pointer to an element which supports the requested
interface. We are asking for the `VideoOverlay` interface, explained in
[](tutorials/basic/toolkit-integration.md),
which controls how to perform rendering into foreign (non-GStreamer)
windows. The internal video sink instantiated by `autovideosink` is the
only element in this pipeline implementing it, so it will be returned.

Once we have the video sink, we inform it of the `UIView` to use for
rendering, through the `gst_video_overlay_set_window_handle()` method.

## EaglUIView

One last detail remains. In order for `glimagesink` to be able to draw
on the
[`UIView`](http://developer.apple.com/library/ios/#documentation/UIKit/Reference/UIView_Class/UIView/UIView.html),
the
[`Layer`](http://developer.apple.com/library/ios/#documentation/GraphicsImaging/Reference/CALayer_class/Introduction/Introduction.html#//apple_ref/occ/cl/CALayer) associated
with this view must be of the
[`CAEAGLLayer`](http://developer.apple.com/library/ios/#documentation/QuartzCore/Reference/CAEAGLLayer_Class/CAEGLLayer/CAEGLLayer.html#//apple_ref/occ/cl/CAEAGLLayer) class.
To this avail, we create the `EaglUIView` class, derived from
`UIView `and overriding the `layerClass` method:

**EaglUIView.m**

```
#import "EaglUIVIew.h"

#import <QuartzCore/QuartzCore.h>

@implementation EaglUIView

+ (Class) layerClass
{
    return [CAEAGLLayer class];
}

@end
```

When creating storyboards, bear in mind that the `UIView `which should
contain the video must have `EaglUIView` as its custom class. This is
easy to setup from the Xcode interface builder. Take a look at the
tutorial storyboard to see how to achieve this.

And this is it, using GStreamer to output video onto an iOS application
is as simple as it seems.

## Conclusion

This tutorial has shown:

  - How to display video on iOS using a `UIView `and
    the `VideoOverlay` interface.
  - How to report the media size to the iOS layout engine through
    runtime manipulation of width and height constraints.

The following tutorial plays an actual clip and adds a few more controls
to this tutorial in order to build a simple media player.

It has been a pleasure having you here, and see you soon!

  [screenshot]: images/tutorials/ios-video-screenshot.png