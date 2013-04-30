#import "GStreamerBackend.h"

#include <gst/gst.h>

@interface GStreamerBackend()
-(void)notifyError:(gchar*) message;
-(void)notifyEos;
-(void) _poll_gst_bus;
@end

@implementation GStreamerBackend {
    GstElement *pipeline;
}

@synthesize delegate;

-(void) dealloc
{
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
}

-(void)notifyError:(gchar*) message
{
    NSString *string = [NSString stringWithUTF8String:message];
    if(delegate && [delegate respondsToSelector:@selector(gstreamerError:from:)])
    {
        [delegate gstreamerError:string from:self];
    }
}

-(void)notifyEos
{
    if(delegate && [delegate respondsToSelector:@selector(gstreamerEosFrom)])
    {
        [delegate gstreamerEosFrom:self];
    }
}

-(void) _poll_gst_bus
{
    GstBus *bus;
    GstMessage *msg;
    
    /* Wait until error or EOS */
    bus = gst_element_get_bus (self->pipeline);
    msg = gst_bus_timed_pop_filtered(bus, GST_CLOCK_TIME_NONE,
                                     (GstMessageType) (GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
    gst_object_unref(bus);
    
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            [self stop];
            [self notifyEos];
            NSLog(@"EOS");
            break;
        case GST_MESSAGE_ERROR: {
            GError *gerr = NULL;
            gchar *debug;
            
            gst_message_parse_error(msg, &gerr, &debug);
            
            [self stop];
            NSLog(@"Error %s - %s", gerr->message, debug, nil);
            [self notifyError:gerr->message];
            g_free(debug);
            g_error_free(gerr);
        }
            break;
        default:
            break;
    }
}

-(BOOL) initializePipeline
{
    GError *error = NULL;
    
    if (pipeline)
        return YES;
    
    pipeline = gst_parse_launch("audiotestsrc ! audioconvert ! audioresample ! autoaudiosink", &error);
    if (error) {
        gchar *message = g_strdup_printf("Unable to build pipeline: %s", error->message);
        g_clear_error (&error);
        [self notifyError:message];
        g_free (message);
        return NO;
    }
    
    /* start the bus polling. This will the bus being continuously polled */
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        while (1) {
            [self _poll_gst_bus];
        }
    });
    
    return YES;
}

-(void) play
{
    if(gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        [self notifyError:"Failed to set pipeline to playing"];
    }
}

-(void) pause
{
    if(gst_element_set_state(pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
        [self notifyError:"Failed to set pipeline to paused"];
    }
}

-(void) stop
{
    if(pipeline)
        gst_element_set_state(pipeline, GST_STATE_NULL);
}

-(NSString*) getGStreamerVersion
{
    char *str = gst_version_string();
    NSString *version = [NSString stringWithUTF8String:str];
    g_free(str);
    return version;
}

@end

