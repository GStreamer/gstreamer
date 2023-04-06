# iOS tutorial 4: A basic media player

## Goal

![screenshot]

Enough testing with synthetic images and audio tones! This tutorial
finally shows how to play actual media in your iOS device; media streamed
directly from the Internet!. We will review:

  - How to keep the User Interface regularly updated with the current
    playback position and duration
  - How to implement a [Time
    Slider](http://developer.apple.com/library/ios/#documentation/UIKit/Reference/UISlider_Class/Reference/Reference.html)
  - How to report the media size to adapt the display surface

It also uses the knowledge gathered in the [](tutorials/basic/index.md) regarding:

  - How to use `playbin` to play any kind of media
  - How to handle network resilience problems

## Introduction

From the previous tutorials, we already have almost all necessary
pieces to build a media player. The most complex part is assembling a
pipeline which retrieves, decodes and displays the media, but we
already know that the `playbin` element can take care of all that for
us. We only need to replace the manual pipeline we used in
[](tutorials/ios/video.md) with a single-element `playbin` pipeline
and we are good to go!

However, we can do better than. We will add a [Time
Slider](http://developer.apple.com/library/ios/#documentation/UIKit/Reference/UISlider_Class/Reference/Reference.html),
with a moving thumb that will advance as our current position in the
media advances. We will also allow the user to drag the thumb, to jump
(or *seek*) to a different position.

And finally, we will make the video surface adapt to the media size, so
the video sink is not forced to draw black borders around the clip.
 This also allows the iOS layout to adapt more nicely to the actual
media content. You can still force the video surface to have a specific
size if you really want to.

## The User Interface

The User Interface from the previous tutorial is expanded again. A
`UISlider` has been added to the toolbar, to keep track of the current
position in the clip, and allow the user to change it. Also, a
(read-only) `UITextField` is added to show the exact clip position and
duration.

**VideoViewController.h**

```
#import <UIKit/UIKit.h>
#import "GStreamerBackendDelegate.h"

@interface VideoViewController : UIViewController <GStreamerBackendDelegate> {
    IBOutlet UILabel *message_label;
    IBOutlet UIBarButtonItem *play_button;
    IBOutlet UIBarButtonItem *pause_button;
    IBOutlet UIView *video_view;
    IBOutlet UIView *video_container_view;
    IBOutlet NSLayoutConstraint *video_width_constraint;
    IBOutlet NSLayoutConstraint *video_height_constraint;
    IBOutlet UIToolbar *toolbar;
    IBOutlet UITextField *time_label;
    IBOutlet UISlider *time_slider;
}

@property (retain,nonatomic) NSString *uri;

-(IBAction) play:(id)sender;
-(IBAction) pause:(id)sender;
-(IBAction) sliderValueChanged:(id)sender;
-(IBAction) sliderTouchDown:(id)sender;
-(IBAction) sliderTouchUp:(id)sender;

/* From GStreamerBackendDelegate */
-(void) gstreamerInitialized;
-(void) gstreamerSetUIMessage:(NSString *)message;

@end
```

Note how we register callbacks for some of the Actions the
[UISlider](http://developer.apple.com/library/ios/#documentation/UIKit/Reference/UISlider_Class/Reference/Reference.html) generates.
Also note that the class has been renamed from `ViewController` to
`VideoViewController`, since the next tutorial adds another
`ViewController` and we will need to differentiate.

## The Video View Controller

The `ViewController` class manages the UI, instantiates
the `GStreamerBackend` and also performs some UI-related tasks on its
behalf:

![](images/icons/grey_arrow_down.gif)Due to the extension of this code,
this view is collapsed by default. Click here to expand…

**VideoViewController.m**

```
#import "VideoViewController.h"
#import "GStreamerBackend.h"
#import <UIKit/UIKit.h>

@interface VideoViewController () {
    GStreamerBackend *gst_backend;
    int media_width;                /* Width of the clip */
    int media_height;               /* height of the clip */
    Boolean dragging_slider;        /* Whether the time slider is being dragged or not */
    Boolean is_local_media;         /* Whether this clip is stored locally or is being streamed */
    Boolean is_playing_desired;     /* Whether the user asked to go to PLAYING */
}

@end

@implementation VideoViewController

@synthesize uri;

/*
 * Private methods
 */

/* The text widget acts as an slave for the seek bar, so it reflects what the seek bar shows, whether
 * it is an actual pipeline position or the position the user is currently dragging to. */
- (void) updateTimeWidget
{
    NSInteger position = time_slider.value / 1000;
    NSInteger duration = time_slider.maximumValue / 1000;
    NSString *position_txt = @" -- ";
    NSString *duration_txt = @" -- ";

    if (duration > 0) {
        NSUInteger hours = duration / (60 * 60);
        NSUInteger minutes = (duration / 60) % 60;
        NSUInteger seconds = duration % 60;

        duration_txt = [NSString stringWithFormat:@"%02u:%02u:%02u", hours, minutes, seconds];
    }
    if (position > 0) {
        NSUInteger hours = position / (60 * 60);
        NSUInteger minutes = (position / 60) % 60;
        NSUInteger seconds = position % 60;

        position_txt = [NSString stringWithFormat:@"%02u:%02u:%02u", hours, minutes, seconds];
    }

    NSString *text = [NSString stringWithFormat:@"%@ / %@",
                      position_txt, duration_txt];

    time_label.text = text;
}

/*
 * Methods from UIViewController
 */

- (void)viewDidLoad
{
    [super viewDidLoad];

    play_button.enabled = FALSE;
    pause_button.enabled = FALSE;

    /* As soon as the GStreamer backend knows the real values, these ones will be replaced */
    media_width = 320;
    media_height = 240;

    uri = @"https://gstreamer.freedesktop.org/data/media/sintel_trailer-368p.ogv";

    gst_backend = [[GStreamerBackend alloc] init:self videoView:video_view];
}

- (void)viewDidDisappear:(BOOL)animated
{
    if (gst_backend)
    {
        [gst_backend deinit];
    }
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
    is_playing_desired = YES;
}

/* Called when the Pause button is pressed */
-(IBAction) pause:(id)sender
{
    [gst_backend pause];
    is_playing_desired = NO;
}

/* Called when the time slider position has changed, either because the user dragged it or
 * we programmatically changed its position. dragging_slider tells us which one happened */
- (IBAction)sliderValueChanged:(id)sender {
    if (!dragging_slider) return;
    // If this is a local file, allow scrub seeking, this is, seek as soon as the slider is moved.
    if (is_local_media)
        [gst_backend setPosition:time_slider.value];
    [self updateTimeWidget];
}

/* Called when the user starts to drag the time slider */
- (IBAction)sliderTouchDown:(id)sender {
    [gst_backend pause];
    dragging_slider = YES;
}

/* Called when the user stops dragging the time slider */
- (IBAction)sliderTouchUp:(id)sender {
    dragging_slider = NO;
    // If this is a remote file, scrub seeking is probably not going to work smoothly enough.
    // Therefore, perform only the seek when the slider is released.
    if (!is_local_media)
        [gst_backend setPosition:time_slider.value];
    if (is_playing_desired)
        [gst_backend play];
}

/* Called when the size of the main view has changed, so we can
 * resize the sub-views in ways not allowed by storyboarding. */
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

    time_slider.frame = CGRectMake(time_slider.frame.origin.x, time_slider.frame.origin.y, toolbar.frame.size.width - time_slider.frame.origin.x - 8, time_slider.frame.size.height);
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
        [gst_backend setUri:uri];
        is_local_media = [uri hasPrefix:@"file://"];
        is_playing_desired = NO;
    });
}

-(void) gstreamerSetUIMessage:(NSString *)message
{
    dispatch_async(dispatch_get_main_queue(), ^{
        message_label.text = message;
    });
}

-(void) mediaSizeChanged:(NSInteger)width height:(NSInteger)height
{
    media_width = width;
    media_height = height;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self viewDidLayoutSubviews];
        [video_view setNeedsLayout];
        [video_view layoutIfNeeded];
    });
}

-(void) setCurrentPosition:(NSInteger)position duration:(NSInteger)duration
{
    /* Ignore messages from the pipeline if the time sliders is being dragged */
    if (dragging_slider) return;

    dispatch_async(dispatch_get_main_queue(), ^{
        time_slider.maximumValue = duration;
        time_slider.value = position;
        [self updateTimeWidget];
    });
}

@end
```

Supporting arbitrary media URIs

The `GStreamerBackend`  provides the `setUri()` method so we can
indicate the URI of the media to play. Since `playbin` will be taking
care of retrieving the media, we can use local or remote URIs
indistinctly (`file://` or `http://`, for example). From the UI code,
though, we want to keep track of whether the file is local or remote,
because we will not offer the same functionalities. We keep track of
this in the `is_local_media` variable, which is set when the URI is set,
in the `gstreamerInitialized` method:

```
-(void) gstreamerInitialized
{
    dispatch_async(dispatch_get_main_queue(), ^{
        play_button.enabled = TRUE;
        pause_button.enabled = TRUE;
        message_label.text = @"Ready";
        [gst_backend setUri:uri];
        is_local_media = [uri hasPrefix:@"file://"];
        is_playing_desired = NO;
    });
}
```

Reporting media size

Every time the size of the media changes (which could happen mid-stream,
for some kind of streams), or when it is first detected,
`GStreamerBackend`  calls our `mediaSizeChanged()` callback:

```
-(void) mediaSizeChanged:(NSInteger)width height:(NSInteger)height
{
    media_width = width;
    media_height = height;
    dispatch_async(dispatch_get_main_queue(), ^{
        [self viewDidLayoutSubviews];
        [video_view setNeedsLayout];
        [video_view layoutIfNeeded];
    });
}
```

Here we simply store the new size and ask the layout to be recalculated.
As we have already seen in [](tutorials/ios/a-running-pipeline.md),
methods which change the UI must be called from the main thread, and we
are now in a callback from some GStreamer internal thread. Hence, the
usage
of `dispatch_async()`[.](http://developer.android.com/reference/android/app/Activity.html#runOnUiThread\(java.lang.Runnable\))

### Refreshing the Time Slider

[](tutorials/basic/toolkit-integration.md) has
already shown how to implement a Seek Bar (or [Time
Slider](http://developer.apple.com/library/ios/#documentation/UIKit/Reference/UISlider_Class/Reference/Reference.html)
in this tutorial) using the GTK+ toolkit. The implementation on iOS is
very similar.

The Seek Bar accomplishes to functions: First, it moves on its own to
reflect the current playback position in the media. Second, it can be
dragged by the user to seek to a different position.

To realize the first function, `GStreamerBackend`  will periodically
call our `setCurrentPosition` method so we can update the position of
the thumb in the Seek Bar. Again we do so from the UI thread, using
`dispatch_async()`.

```
-(void) setCurrentPosition:(NSInteger)position duration:(NSInteger)duration
{
    /* Ignore messages from the pipeline if the time sliders is being dragged */
    if (dragging_slider) return;

    dispatch_async(dispatch_get_main_queue(), ^{
        time_slider.maximumValue = duration;
        time_slider.value = position;
        [self updateTimeWidget];
    });
}
```

Also note that if the user is currently dragging the slider (the
`dragging_slider` variable is explained below) we ignore
`setCurrentPosition` calls from `GStreamerBackend`, as they would
interfere with the user’s actions.

To the left of the Seek Bar (refer to the screenshot at the top of this
page), there is
a [TextField](https://developer.apple.com/library/ios/#documentation/UIKit/Reference/UITextField_Class/Reference/UITextField.html) widget
which we will use to display the current position and duration in
"`HH:mm:ss / HH:mm:ss`" textual format. The `updateTimeWidget` method
takes care of it, and must be called every time the Seek Bar is
updated:

```
/* The text widget acts as an slave for the seek bar, so it reflects what the seek bar shows, whether
 * it is an actual pipeline position or the position the user is currently dragging to. */
- (void) updateTimeWidget
{
    NSInteger position = time_slider.value / 1000;
    NSInteger duration = time_slider.maximumValue / 1000;
    NSString *position_txt = @" -- ";
    NSString *duration_txt = @" -- ";

    if (duration > 0) {
        NSUInteger hours = duration / (60 * 60);
        NSUInteger minutes = (duration / 60) % 60;
        NSUInteger seconds = duration % 60;

        duration_txt = [NSString stringWithFormat:@"%02u:%02u:%02u", hours, minutes, seconds];
    }
    if (position > 0) {
        NSUInteger hours = position / (60 * 60);
        NSUInteger minutes = (position / 60) % 60;
        NSUInteger seconds = position % 60;

        position_txt = [NSString stringWithFormat:@"%02u:%02u:%02u", hours, minutes, seconds];
    }

    NSString *text = [NSString stringWithFormat:@"%@ / %@",
                      position_txt, duration_txt];

    time_label.text = text;
}
```

Seeking with the Seek Bar

To perform the second function of the Seek Bar (allowing the user to
seek by dragging the thumb), we register some callbacks through IBAction
outlets. Refer to the storyboard in this tutorial’s project to see which
outlets are connected. We will be notified when the user starts dragging
the Slider, when the Slider position changes and when the users releases
the Slider.

```
/* Called when the user starts to drag the time slider */
- (IBAction)sliderTouchDown:(id)sender {
    [gst_backend pause];
    dragging_slider = YES;
}
```

`sliderTouchDown` is called when the user starts dragging. Here we pause
the pipeline because if the user is searching for a particular scene, we
do not want it to keep moving. We also mark that a drag operation is in
progress in the `dragging_slider` variable.

```
/* Called when the time slider position has changed, either because the user dragged it or
 * we programmatically changed its position. dragging_slider tells us which one happened */
- (IBAction)sliderValueChanged:(id)sender {
    if (!dragging_slider) return;
    // If this is a local file, allow scrub seeking, this is, seek as soon as the slider is moved.
    if (is_local_media)
        [gst_backend setPosition:time_slider.value];
    [self updateTimeWidget];
}
```

`sliderValueChanged` is called every time the Slider’s thumb moves, be
it because the user dragged it, or because we changed its value form the
program. We discard the latter case using the
`dragging_slider` variable.

As the comment says, if this is a local media, we allow scrub seeking,
this is, we jump to the indicated position as soon as the thumb moves.
Otherwise, the seek operation will be performed when the thumb is
released, and the only thing we do here is update the textual time
widget.

```
/* Called when the user stops dragging the time slider */
- (IBAction)sliderTouchUp:(id)sender {
    dragging_slider = NO;
    // If this is a remote file, scrub seeking is probably not going to work smoothly enough.
    // Therefore, perform only the seek when the slider is released.
    if (!is_local_media)
        [gst_backend setPosition:time_slider.value];
    if (is_playing_desired)
        [gst_backend play];
}
```

Finally, `sliderTouchUp` is called when the thumb is released. We
perform the seek operation if the file was non-local, restore the
pipeline to the desired playing state and end the dragging operation by
setting `dragging_slider` to NO.

This concludes the User interface part of this tutorial. Let’s review
now the `GStreamerBackend`  class that allows this to work.

## The GStreamer Backend

The `GStreamerBackend` class performs all GStreamer-related tasks and
offers a simplified interface to the application, which does not need to
deal with all the GStreamer details. When it needs to perform any UI
action, it does so through a delegate, which is expected to adhere to
the `GStreamerBackendDelegate` protocol.

**GStreamerBackend.m**

```
#import "GStreamerBackend.h"

#include <gst/gst.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

/* Do not allow seeks to be performed closer than this distance. It is visually useless, and will probably
 * confuse some demuxers. */
#define SEEK_MIN_DELAY (500 * GST_MSECOND)

@interface GStreamerBackend()
-(void)setUIMessage:(gchar*) message;
-(void)app_function;
-(void)check_initialization_complete;
@end

@implementation GStreamerBackend {
    id ui_delegate;              /* Class that we use to interact with the user interface */
    GstElement *pipeline;        /* The running pipeline */
    GstElement *video_sink;      /* The video sink element which receives VideoOverlay commands */
    GMainContext *context;       /* GLib context used to run the main loop */
    GMainLoop *main_loop;        /* GLib main loop */
    gboolean initialized;        /* To avoid informing the UI multiple times about the initialization */
    UIView *ui_video_view;       /* UIView that holds the video */
    GstState state;              /* Current pipeline state */
    GstState target_state;       /* Desired pipeline state, to be set once buffering is complete */
    gint64 duration;             /* Cached clip duration */
    gint64 desired_position;     /* Position to seek to, once the pipeline is running */
    GstClockTime last_seek_time; /* For seeking overflow prevention (throttling) */
    gboolean is_live;            /* Live streams do not use buffering */
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
        self->duration = GST_CLOCK_TIME_NONE;

        GST_DEBUG_CATEGORY_INIT (debug_category, "tutorial-4", 0, "iOS tutorial 4");
        gst_debug_set_threshold_for_name("tutorial-4", GST_LEVEL_DEBUG);

        /* Start the bus monitoring task */
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            [self app_function];
        });
    }

    return self;
}

-(void) deinit
{
    if (main_loop) {
        g_main_loop_quit(main_loop);
    }
}

-(void) play
{
    target_state = GST_STATE_PLAYING;
    is_live = (gst_element_set_state (pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_NO_PREROLL);
}

-(void) pause
{
    target_state = GST_STATE_PAUSED;
    is_live = (gst_element_set_state (pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
}

-(void) setUri:(NSString*)uri
{
    const char *char_uri = [uri UTF8String];
    g_object_set(pipeline, "uri", char_uri, NULL);
    GST_DEBUG ("URI set to %s", char_uri);
}

-(void) setPosition:(NSInteger)milliseconds
{
    gint64 position = (gint64)(milliseconds * GST_MSECOND);
    if (state >= GST_STATE_PAUSED) {
        execute_seek(position, self);
    } else {
        GST_DEBUG ("Scheduling seek to %" GST_TIME_FORMAT " for later", GST_TIME_ARGS (position));
        self->desired_position = position;
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

/* Tell the application what is the current position and clip duration */
-(void) setCurrentUIPosition:(gint)pos duration:(gint)dur
{
    if(ui_delegate && [ui_delegate respondsToSelector:@selector(setCurrentPosition:duration:)])
    {
        [ui_delegate setCurrentPosition:pos duration:dur];
    }
}

/* If we have pipeline and it is running, query the current position and clip duration and inform
 * the application */
static gboolean refresh_ui (GStreamerBackend *self) {
    gint64 position;

    /* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
    if (!self || !self->pipeline || self->state < GST_STATE_PAUSED)
        return TRUE;

    /* If we didn't know it yet, query the stream duration */
    if (!GST_CLOCK_TIME_IS_VALID (self->duration)) {
        gst_element_query_duration (self->pipeline, GST_FORMAT_TIME, &self->duration);
    }

    if (gst_element_query_position (self->pipeline, GST_FORMAT_TIME, &position)) {
        /* The UI expects these values in milliseconds, and GStreamer provides nanoseconds */
        [self setCurrentUIPosition:position / GST_MSECOND duration:self->duration / GST_MSECOND];
    }
    return TRUE;
}

/* Forward declaration for the delayed seek callback */
static gboolean delayed_seek_cb (GStreamerBackend *self);

/* Perform seek, if we are not too close to the previous seek. Otherwise, schedule the seek for
 * some time in the future. */
static void execute_seek (gint64 position, GStreamerBackend *self) {
    gint64 diff;

    if (position == GST_CLOCK_TIME_NONE)
        return;

    diff = gst_util_get_timestamp () - self->last_seek_time;

    if (GST_CLOCK_TIME_IS_VALID (self->last_seek_time) && diff < SEEK_MIN_DELAY) {
        /* The previous seek was too close, delay this one */
        GSource *timeout_source;

        if (self->desired_position == GST_CLOCK_TIME_NONE) {
            /* There was no previous seek scheduled. Setup a timer for some time in the future */
            timeout_source = g_timeout_source_new ((SEEK_MIN_DELAY - diff) / GST_MSECOND);
            g_source_set_callback (timeout_source, (GSourceFunc)delayed_seek_cb, (__bridge void *)self, NULL);
            g_source_attach (timeout_source, self->context);
            g_source_unref (timeout_source);
        }
        /* Update the desired seek position. If multiple requests are received before it is time
         * to perform a seek, only the last one is remembered. */
        self->desired_position = position;
        GST_DEBUG ("Throttling seek to %" GST_TIME_FORMAT ", will be in %" GST_TIME_FORMAT,
                   GST_TIME_ARGS (position), GST_TIME_ARGS (SEEK_MIN_DELAY - diff));
    } else {
        /* Perform the seek now */
        GST_DEBUG ("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
        self->last_seek_time = gst_util_get_timestamp ();
        gst_element_seek_simple (self->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, position);
        self->desired_position = GST_CLOCK_TIME_NONE;
    }
}

/* Delayed seek callback. This gets called by the timer setup in the above function. */
static gboolean delayed_seek_cb (GStreamerBackend *self) {
    GST_DEBUG ("Doing delayed seek to %" GST_TIME_FORMAT, GST_TIME_ARGS (self->desired_position));
    execute_seek (self->desired_position, self);
    return FALSE;
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

/* Called when the End Of the Stream is reached. Just move to the beginning of the media and pause. */
static void eos_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self) {
    self->target_state = GST_STATE_PAUSED;
    self->is_live = (gst_element_set_state (self->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_NO_PREROLL);
    execute_seek (0, self);
}

/* Called when the duration of the media changes. Just mark it as unknown, so we re-query it in the next UI refresh. */
static void duration_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self) {
    self->duration = GST_CLOCK_TIME_NONE;
}

/* Called when buffering messages are received. We inform the UI about the current buffering level and
 * keep the pipeline paused until 100% buffering is reached. At that point, set the desired state. */
static void buffering_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self) {
    gint percent;

    if (self->is_live)
        return;

    gst_message_parse_buffering (msg, &percent);
    if (percent < 100 && self->target_state >= GST_STATE_PAUSED) {
        gchar * message_string = g_strdup_printf ("Buffering %d%%", percent);
        gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
        [self setUIMessage:message_string];
        g_free (message_string);
    } else if (self->target_state >= GST_STATE_PLAYING) {
        gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
    } else if (self->target_state >= GST_STATE_PAUSED) {
        [self setUIMessage:"Buffering complete"];
    }
}

/* Called when the clock is lost */
static void clock_lost_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self) {
    if (self->target_state >= GST_STATE_PLAYING) {
        gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
        gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
    }
}

/* Retrieve the video sink's Caps and tell the application about the media size */
static void check_media_size (GStreamerBackend *self) {
    GstElement *video_sink;
    GstPad *video_sink_pad;
    GstCaps *caps;
    GstVideoInfo info;

    /* Retrieve the Caps at the entrance of the video sink */
    g_object_get (self->pipeline, "video-sink", &video_sink, NULL);

    /* Do nothing if there is no video sink (this might be an audio-only clip */
    if (!video_sink) return;

    video_sink_pad = gst_element_get_static_pad (video_sink, "sink");
    caps = gst_pad_get_current_caps (video_sink_pad);

    if (gst_video_info_from_caps(&info, caps)) {
        info.width = info.width * info.par_n / info.par_d
        GST_DEBUG ("Media size is %dx%d, notifying application", info.width, info.height);

        if (self->ui_delegate && [self->ui_delegate respondsToSelector:@selector(mediaSizeChanged:info.height:)])
        {
            [self->ui_delegate mediaSizeChanged:info.width height:info.height];
        }
    }

    gst_caps_unref(caps);
    gst_object_unref (video_sink_pad);
    gst_object_unref(video_sink);
}

/* Notify UI about pipeline state changes */
static void state_changed_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self)
{
    GstState old_state, new_state, pending_state;
    gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
    /* Only pay attention to messages coming from the pipeline, not its children */
    if (GST_MESSAGE_SRC (msg) == GST_OBJECT (self->pipeline)) {
        self->state = new_state;
        gchar *message = g_strdup_printf("State changed to %s", gst_element_state_get_name(new_state));
        [self setUIMessage:message];
        g_free (message);

        if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED)
        {
            check_media_size(self);

            /* If there was a scheduled seek, perform it now that we have moved to the Paused state */
            if (GST_CLOCK_TIME_IS_VALID (self->desired_position))
                execute_seek (self->desired_position, self);
        }
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
    GSource *timeout_source;
    GSource *bus_source;
    GError *error = NULL;

    GST_DEBUG ("Creating pipeline");

    /* Create our own GLib Main Context and make it the default one */
    context = g_main_context_new ();
    g_main_context_push_thread_default(context);

    /* Build pipeline */
    pipeline = gst_parse_launch("playbin", &error);
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
    g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback)eos_cb, (__bridge void *)self);
    g_signal_connect (G_OBJECT (bus), "message::state-changed", (GCallback)state_changed_cb, (__bridge void *)self);
    g_signal_connect (G_OBJECT (bus), "message::duration", (GCallback)duration_cb, (__bridge void *)self);
    g_signal_connect (G_OBJECT (bus), "message::buffering", (GCallback)buffering_cb, (__bridge void *)self);
    g_signal_connect (G_OBJECT (bus), "message::clock-lost", (GCallback)clock_lost_cb, (__bridge void *)self);
    gst_object_unref (bus);

    /* Register a function that GLib will call 4 times per second */
    timeout_source = g_timeout_source_new (250);
    g_source_set_callback (timeout_source, (GSourceFunc)refresh_ui, (__bridge void *)self, NULL);
    g_source_attach (timeout_source, context);
    g_source_unref (timeout_source);

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
    pipeline = NULL;

    ui_delegate = NULL;
    ui_video_view = NULL;

    return;
}

@end
```

Supporting arbitrary media URIs

The UI code will call `setUri` whenever it wants to change the playing
URI (in this tutorial the URI never changes, but it does in the next
one):

```
-(void) setUri:(NSString*)uri
{
    const char *char_uri = [uri UTF8String];
    g_object_set(pipeline, "uri", char_uri, NULL);
    GST_DEBUG ("URI set to %s", char_uri);
}
```

We first need to obtain a plain `char *` from within the `NSString *` we
get, using the `UTF8String` method.

`playbin`’s URI is exposed as a common `GObject` property, so we simply
set it with `g_object_set()`.

### Reporting media size

Some codecs allow the media size (width and height of the video) to
change during playback. For simplicity, this tutorial assumes that they
do not. Therefore, in the `READY` to `PAUSED` state change, once the Caps of
the decoded media are known, we inspect them in `check_media_size()`:

```
/* Retrieve the video sink's Caps and tell the application about the media size */
static void check_media_size (GStreamerBackend *self) {
    GstElement *video_sink;
    GstPad *video_sink_pad;
    GstCaps *caps;
    GstVideoInfo info;

    /* Retrieve the Caps at the entrance of the video sink */
    g_object_get (self->pipeline, "video-sink", &video_sink, NULL);

    /* Do nothing if there is no video sink (this might be an audio-only clip */
    if (!video_sink) return;

    video_sink_pad = gst_element_get_static_pad (video_sink, "sink");
    caps = gst_pad_get_current_caps (video_sink_pad);

    if (gst_video_info_from_caps(&info, caps)) {
        info.width = info.width * info.par_n / info.par_d;
        GST_DEBUG ("Media size is %dx%d, notifying application", info.width, info.height);

        if (self->ui_delegate && [self->ui_delegate respondsToSelector:@selector(mediaSizeChanged:info.height:)])
        {
            [self->ui_delegate mediaSizeChanged:info.width height:info.height];
        }
    }

    gst_caps_unref(caps);
    gst_object_unref (video_sink_pad);
    gst_object_unref(video_sink);
}
```

We first retrieve the video sink element from the pipeline, using
the `video-sink` property of `playbin`, and then its sink Pad. The
negotiated Caps of this Pad, which we recover using
`gst_pad_get_current_caps()`,  are the Caps of the decoded media.

The helper functions `gst_video_format_parse_caps()` and
`gst_video_parse_caps_pixel_aspect_ratio()` turn the Caps into
manageable integers, which we pass to the application through
its `mediaSizeChanged` callback.

### Refreshing the Seek Bar

To keep the UI updated, a `GLib` timer is installed in
the `app_function` that fires 4 times per second (or every 250ms),
right before entering the main loop:

```
/* Register a function that GLib will call 4 times per second */
timeout_source = g_timeout_source_new (250);
g_source_set_callback (timeout_source, (GSourceFunc)refresh_ui, (__bridge void *)self, NULL);
g_source_attach (timeout_source, context);
g_source_unref (timeout_source);
```

Then, in the `refresh_ui` method:

```
/* If we have pipeline and it is running, query the current position and clip duration and inform
 * the application */
static gboolean refresh_ui (GStreamerBackend *self) {
    gint64 position;

    /* We do not want to update anything unless we have a working pipeline in the PAUSED or PLAYING state */
    if (!self || !self->pipeline || self->state < GST_STATE_PAUSED)
        return TRUE;

    /* If we didn't know it yet, query the stream duration */
    if (!GST_CLOCK_TIME_IS_VALID (self->duration)) {
        gst_element_query_duration (self->pipeline, GST_FORMAT_TIME, &self->duration);
    }

    if (gst_element_query_position (self->pipeline, GST_FORMAT_TIME, &position)) {
        /* The UI expects these values in milliseconds, and GStreamer provides nanoseconds */
        [self setCurrentUIPosition:position / GST_MSECOND duration:self->duration / GST_MSECOND];
    }
    return TRUE;
}
```

If it is unknown, the clip duration is retrieved, as explained in
[](tutorials/basic/time-management.md). The current position is
retrieved next, and the UI is informed of both through its
`setCurrentUIPosition` callback.

Bear in mind that all time-related measures returned by GStreamer are in
nanoseconds, whereas, for simplicity, we decided to make the UI code
work in milliseconds.

### Seeking with the Seek Bar

The UI code already takes care of most of the complexity of seeking by
dragging the thumb of the Seek Bar. From the `GStreamerBackend`, we just
need to honor the calls to `setPosition` and instruct the pipeline to
jump to the indicated position.

There are, though, a couple of caveats. Firstly, seeks are only possible
when the pipeline is in the `PAUSED` or `PLAYING` state, and we might
receive seek requests before that happens. Secondly, dragging the Seek
Bar can generate a very high number of seek requests in a short period
of time, which is visually useless and will impair responsiveness. Let’s
see how to overcome these problems.

#### Delayed seeks

In `setPosition`:

```
-(void) setPosition:(NSInteger)milliseconds
{
    gint64 position = (gint64)(milliseconds * GST_MSECOND);
    if (state >= GST_STATE_PAUSED) {
        execute_seek(position, self);
    } else {
        GST_DEBUG ("Scheduling seek to %" GST_TIME_FORMAT " for later", GST_TIME_ARGS (position));
        self->desired_position = position;
    }
}
```

If we are already in the correct state for seeking, execute it right
away; otherwise, store the desired position in
the `desired_position` variable. Then, in
the `state_changed_cb()` callback:

```
if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED)
{
    check_media_size(self);

    /* If there was a scheduled seek, perform it now that we have moved to the Paused state */
    if (GST_CLOCK_TIME_IS_VALID (self->desired_position))
        execute_seek (self->desired_position, self);
}
```

Once the pipeline moves from the `READY` to the `PAUSED` state, we check if
there is a pending seek operation and execute it.
The `desired_position` variable is reset inside `execute_seek()`.

#### Seek throttling

A seek is potentially a lengthy operation. The demuxer (the element
typically in charge of seeking) needs to estimate the appropriate byte
offset inside the media file that corresponds to the time position to
jump to. Then, it needs to start decoding from that point until the
desired position is reached. If the initial estimate is accurate, this
will not take long, but, on some container formats, or when indexing
information is missing, it can take up to several seconds.

If a demuxer is in the process of performing a seek and receives a
second one, it is up to it to finish the first one, start the second one
or abort both, which is a bad thing. A simple method to avoid this issue
is *throttling*, which means that we will only allow one seek every half
a second (for example): after performing a seek, only the last seek
request received during the next 500ms is stored, and will be honored
once this period elapses.

To achieve this, all seek requests are routed through
the `execute_seek()` method:

```
/* Perform seek, if we are not too close to the previous seek. Otherwise, schedule the seek for
 * some time in the future. */
static void execute_seek (gint64 position, GStreamerBackend *self) {
    gint64 diff;

    if (position == GST_CLOCK_TIME_NONE)
        return;

    diff = gst_util_get_timestamp () - self->last_seek_time;

    if (GST_CLOCK_TIME_IS_VALID (self->last_seek_time) && diff < SEEK_MIN_DELAY) {
        /* The previous seek was too close, delay this one */
        GSource *timeout_source;

        if (self->desired_position == GST_CLOCK_TIME_NONE) {
            /* There was no previous seek scheduled. Setup a timer for some time in the future */
            timeout_source = g_timeout_source_new ((SEEK_MIN_DELAY - diff) / GST_MSECOND);
            g_source_set_callback (timeout_source, (GSourceFunc)delayed_seek_cb, (__bridge void *)self, NULL);
            g_source_attach (timeout_source, self->context);
            g_source_unref (timeout_source);
        }
        /* Update the desired seek position. If multiple requests are received before it is time
         * to perform a seek, only the last one is remembered. */
        self->desired_position = position;
        GST_DEBUG ("Throttling seek to %" GST_TIME_FORMAT ", will be in %" GST_TIME_FORMAT,
                   GST_TIME_ARGS (position), GST_TIME_ARGS (SEEK_MIN_DELAY - diff));
    } else {
        /* Perform the seek now */
        GST_DEBUG ("Seeking to %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
        self->last_seek_time = gst_util_get_timestamp ();
        gst_element_seek_simple (self->pipeline, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, position);
        self->desired_position = GST_CLOCK_TIME_NONE;
    }
}
```

The time at which the last seek was performed is stored in
the `last_seek_time` variable. This is wall clock time, not to be
confused with the stream time carried in the media time stamps, and is
obtained with `gst_util_get_timestamp()`.

If enough time has passed since the last seek operation, the new one is
directly executed and `last_seek_time` is updated. Otherwise, the new
seek is scheduled for later. If there is no previously scheduled seek, a
one-shot timer is setup to trigger 500ms after the last seek operation.
If another seek was already scheduled, its desired position is simply
updated with the new one.

The one-shot timer calls `delayed_seek_cb()`, which simply
calls `execute_seek()` again.

> ![information]
> Ideally, `execute_seek()` will now find that enough time has indeed passed since the last seek and the scheduled one will proceed. It might happen, though, that after 500ms of the previous seek, and before the timer wakes up, yet another seek comes through and is executed. `delayed_seek_cb()` needs to check for this condition to avoid performing two very close seeks, and therefore calls `execute_seek()` instead of performing the seek itself.
>
>This is not a complete solution: the scheduled seek will still be executed, even though a more-recent seek has already been executed that should have cancelled it. However, it is a good tradeoff between functionality and simplicity.

###  Network resilience

[](tutorials/basic/streaming.md) has already
shown how to adapt to the variable nature of the network bandwidth by
using buffering. The same procedure is used here, by listening to the
buffering
messages:

```
g_signal_connect (G_OBJECT (bus), "message::buffering", (GCallback)buffering_cb, (__bridge void *)self);
```

And pausing the pipeline until buffering is complete (unless this is a
live
source):



```
/* Called when buffering messages are received. We inform the UI about the current buffering level and
 * keep the pipeline paused until 100% buffering is reached. At that point, set the desired state. */
static void buffering_cb (GstBus *bus, GstMessage *msg, GStreamerBackend *self) {
    gint percent;

    if (self->is_live)
        return;

    gst_message_parse_buffering (msg, &percent);
    if (percent < 100 && self->target_state >= GST_STATE_PAUSED) {
        gchar * message_string = g_strdup_printf ("Buffering %d%%", percent);
        gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
        [self setUIMessage:message_string];
        g_free (message_string);
    } else if (self->target_state >= GST_STATE_PLAYING) {
        gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
    } else if (self->target_state >= GST_STATE_PAUSED) {
        [self setUIMessage:"Buffering complete"];
    }
}
```

`target_state` is the state in which we have been instructed to set the
pipeline, which might be different to the current state, because
buffering forces us to go to `PAUSED`. Once buffering is complete we set
the pipeline to the `target_state`.

## Conclusion

This tutorial has shown how to embed a `playbin` pipeline into an iOS
application. This, effectively, turns such application into a basic
media player, capable of streaming and decoding all the formats
GStreamer understands. More particularly, it has shown:

  - How to keep the User Interface regularly updated by using a timer,
    querying the pipeline position and calling a UI code method.
  - How to implement a Seek Bar which follows the current position and
    transforms thumb motion into reliable seek events.
  - How to report the media size to adapt the display surface, by
    reading the sink Caps at the appropriate moment and telling the UI
    about it.

The next tutorial adds the missing bits to turn the application built
here into an acceptable iOS media player.

  [information]: images/icons/emoticons/information.svg
  [screenshot]: images/tutorials/ios-a-basic-media-player-screenshot.png
