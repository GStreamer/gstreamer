#import "VideoViewController.h"
#import <gst/player/player.h>
#import <UIKit/UIKit.h>

@interface VideoViewController () {
    GstPlayer *player;
    int media_width;                /* Width of the clip */
    int media_height;               /* height ofthe clip */
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
    
    /* As soon as the GStreamer backend knows the real values, these ones will be replaced */
    media_width = 320;
    media_height = 240;

    player = gst_player_new (gst_player_video_overlay_video_renderer_new ((__bridge gpointer)(video_view)), NULL);
    g_object_set (player, "uri", [uri UTF8String], NULL);
    
    gst_debug_set_threshold_for_name("gst-player", GST_LEVEL_TRACE);
    
    g_signal_connect (player, "position-updated", G_CALLBACK (position_updated), (__bridge gpointer) self);
    g_signal_connect (player, "duration-changed", G_CALLBACK (duration_changed), (__bridge gpointer) self);
    g_signal_connect (player, "video-dimensions-changed", G_CALLBACK (video_dimensions_changed), (__bridge gpointer) self);
    
    is_local_media = [uri hasPrefix:@"file://"];
    is_playing_desired = NO;

    time_slider.value = 0;
    time_slider.minimumValue = 0;
    time_slider.maximumValue = 0;
}

- (void)viewDidDisappear:(BOOL)animated
{
    if (player)
    {
        gst_object_unref (player);
    }
    [UIApplication sharedApplication].idleTimerDisabled = NO;
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

/* Called when the Play button is pressed */
-(IBAction) play:(id)sender
{
    gst_player_play (player);
    is_playing_desired = YES;
    [UIApplication sharedApplication].idleTimerDisabled = YES;
}

/* Called when the Pause button is pressed */
-(IBAction) pause:(id)sender
{
    gst_player_pause(player);
    is_playing_desired = NO;
    [UIApplication sharedApplication].idleTimerDisabled = NO;
}

/* Called when the time slider position has changed, either because the user dragged it or
 * we programmatically changed its position. dragging_slider tells us which one happened */
- (IBAction)sliderValueChanged:(id)sender {
    if (!dragging_slider) return;
    // If this is a local file, allow scrub seeking, this is, seek as soon as the slider is moved.
    if (is_local_media)
        gst_player_seek (player, time_slider.value * 1000000);
    [self updateTimeWidget];
}

/* Called when the user starts to drag the time slider */
- (IBAction)sliderTouchDown:(id)sender {
    gst_player_pause (player);
    dragging_slider = YES;
}

/* Called when the user stops dragging the time slider */
- (IBAction)sliderTouchUp:(id)sender {
    dragging_slider = NO;
    // If this is a remote file, scrub seeking is probably not going to work smoothly enough.
    // Therefore, perform only the seek when the slider is released.
    if (!is_local_media)
        gst_player_seek (player, ((long)time_slider.value) * 1000000);
    if (is_playing_desired)
        gst_player_play (player);
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

static void video_dimensions_changed (GstPlayer * unused, gint width, gint height, VideoViewController * self)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        if (width > 0 && height > 0) {
            [self videoDimensionsChanged:width height:height];
        }
    });
}

-(void) videoDimensionsChanged:(NSInteger)width height:(NSInteger)height
{
    media_width = width;
    media_height = height;
    [self viewDidLayoutSubviews];
    [video_view setNeedsLayout];
    [video_view layoutIfNeeded];
}

static void position_updated (GstPlayer * unused, GstClockTime position, VideoViewController *self)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self positionUpdated:(int) (position / 1000000)];
    });
}

-(void) positionUpdated:(NSInteger)position
{
    /* Ignore messages from the pipeline if the time sliders is being dragged */
    if (dragging_slider) return;
    
    time_slider.value = position;
    [self updateTimeWidget];
}

static void duration_changed (GstPlayer * unused, GstClockTime duration, VideoViewController *self)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self durationChanged:(int) (duration / 1000000)];
    });
}

-(void) durationChanged:(NSInteger)duration
{
    time_slider.maximumValue = duration;
    [self updateTimeWidget];
}

@end
