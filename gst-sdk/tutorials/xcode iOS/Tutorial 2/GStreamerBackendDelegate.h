#import <Foundation/Foundation.h>

@protocol GStreamerBackendDelegate <NSObject>

@optional
-(void) gstreamerError:(NSString *)message from:(id)sender;
-(void) gstreamerEosFrom:(id)sender;

@end
