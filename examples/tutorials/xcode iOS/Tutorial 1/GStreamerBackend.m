#import "GStreamerBackend.h"

#include <gst/gst.h>

@implementation GStreamerBackend

-(NSString*) getGStreamerVersion
{
    char *version_utf8 = gst_version_string();
    NSString *version_string = [NSString stringWithUTF8String:version_utf8];
    g_free(version_utf8);
    return version_string;
}

@end
