#import "GStreamerBackend.h"

#include <gst/gst.h>

@implementation GStreamerBackend


-(NSString*) getGStreamerVersion
{
    char *str = gst_version_string();
    NSString *version = [NSString stringWithUTF8String:str];
    g_free(str);
    return version;
}

@end

