#import "ViewController.h"
#import "GStreamerBackend.h"
#import <UIKit/UIKit.h>

@interface ViewController () {
    GStreamerBackend *gst_backend;
}

@end

@implementation ViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
    
    play_button.enabled = FALSE;
    pause_button.enabled = FALSE;

    gst_backend = [[GStreamerBackend alloc] init:self];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

-(IBAction) play:(id)sender
{
    [gst_backend play];
}

-(IBAction) pause:(id)sender
{
    [gst_backend pause];
}

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
