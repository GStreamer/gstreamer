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
	// Do any additional setup after loading the view, typically from a nib.
    
    gst_backend = [[GStreamerBackend alloc] init];
    
    if (![gst_backend initializePipeline]) {
        
    }
    gst_backend.delegate = self;
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

-(void) gstreamerError:(NSString *)message from:(id)sender
{
    NSLog(@"Error %@", message, nil);
    
    UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"GStreamer error"
                                                    message:message
                                                   delegate:nil
                                          cancelButtonTitle:@"OK"
                                          otherButtonTitles:nil];
    dispatch_async(dispatch_get_main_queue(), ^{
        /* make sure it runs from the main thread */
        [alert show];
    });
}

-(void) gstreamerEosFrom:(id)sender
{
    UIAlertView *alert = [[UIAlertView alloc] initWithTitle:@"EOS" message:@"End of stream" delegate:nil cancelButtonTitle:@"OK" otherButtonTitles:nil];
    
    dispatch_async(dispatch_get_main_queue(), ^{
        /* make sure it runs from the main thread */
        [alert show];
    });
}

@end
