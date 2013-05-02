#import <Foundation/Foundation.h>
#import "GStreamerBackendDelegate.h"

@interface GStreamerBackend : NSObject

@property (nonatomic,assign) id delegate;

-(NSString*) getGStreamerVersion;

-(BOOL) initializePipeline;

-(void) play;
-(void) pause;
-(void) stop;



@end