#import <Foundation/Foundation.h>
#import "GStreamerBackendDelegate.h"

@interface GStreamerBackend : NSObject

-(id) init:(id) uiDelegate;
-(void) play;
-(void) pause;
-(void) stop;

@end