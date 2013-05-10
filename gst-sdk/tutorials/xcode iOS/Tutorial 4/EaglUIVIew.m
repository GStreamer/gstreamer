#import "EaglUIVIew.h"

#import <QuartzCore/QuartzCore.h>

@implementation EaglUIView


+ (Class) layerClass
{
    return [CAEAGLLayer class];
}

@end
