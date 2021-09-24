#import "ViewController.h"
#import "GStreamerBackend.h"

@interface ViewController () {
    GStreamerBackend *gst_backend;
}

@end

@implementation ViewController

@synthesize label;

- (void)viewDidLoad
{
    [super viewDidLoad];
	// Do any additional setup after loading the view, typically from a nib.
    gst_backend = [[GStreamerBackend alloc] init];

    label.text = [NSString stringWithFormat:@"Welcome to %@!", [gst_backend getGStreamerVersion]];
}

- (void)didReceiveMemoryWarning
{
    [super didReceiveMemoryWarning];
    // Dispose of any resources that can be recreated.
}

@end
