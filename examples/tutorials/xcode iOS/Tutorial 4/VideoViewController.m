#import "VideoViewController.h"
#import "GStreamerBackend.h"
#import <UIKit/UIKit.h>

@interface VideoViewController () {
    GStreamerBackend *gst_backend;
    NSInteger media_width;                /* Width of the clip */
    NSInteger media_height;               /* height of the clip */
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

        duration_txt = [NSString stringWithFormat:@"%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds];
    }
    if (position > 0) {
        NSUInteger hours = position / (60 * 60);
        NSUInteger minutes = (position / 60) % 60;
        NSUInteger seconds = position % 60;

        position_txt = [NSString stringWithFormat:@"%02lu:%02lu:%02lu", (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds];
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

    uri = @"https://www.freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.ogv";

    gst_backend = [[GStreamerBackend alloc] init:self videoView:video_view];
    
    time_slider.value = 0;
    time_slider.minimumValue = 0;
    time_slider.maximumValue = 0;
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
