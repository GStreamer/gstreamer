#import <UIKit/UIKit.h>
#import "GStreamerBackendDelegate.h"

@interface ViewController : UIViewController <GStreamerBackendDelegate> {
    IBOutlet UILabel *message_label;
    IBOutlet UIBarButtonItem *play_button;
    IBOutlet UIBarButtonItem *pause_button;
}

-(IBAction) play:(id)sender;
-(IBAction) pause:(id)sender;

/* From GStreamerBackendDelegate */
-(void) gstreamerInitialized;
-(void) gstreamerSetUIMessage:(NSString *)message;

@end
