#import <Foundation/Foundation.h>

@protocol GStreamerBackendDelegate <NSObject>

@optional
-(void) gstreamerInitialized;
-(void) gstreamerSetUIMessage:(NSString *)message;

@end
