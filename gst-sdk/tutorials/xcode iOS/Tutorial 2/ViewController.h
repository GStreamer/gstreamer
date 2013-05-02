#import <UIKit/UIKit.h>
#import "GStreamerBackendDelegate.h"

@interface ViewController : UIViewController <GStreamerBackendDelegate> {
}

-(IBAction) play:(id)sender;
-(IBAction) pause:(id)sender;

/* From GStreamerBackendDelegate */
-(void) gstreamerError:(NSString *)message from:(id)sender;
-(void) gstreamerEosFrom:(id)sender;

@end
