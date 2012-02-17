


#ifndef __GST_INTERFACES_ENUM_TYPES_H__
#define __GST_INTERFACES_ENUM_TYPES_H__

#include <glib-object.h>

G_BEGIN_DECLS

/* enumerations from "navigation.h" */
GType gst_navigation_command_get_type (void);
#define GST_TYPE_NAVIGATION_COMMAND (gst_navigation_command_get_type())
GType gst_navigation_query_type_get_type (void);
#define GST_TYPE_NAVIGATION_QUERY_TYPE (gst_navigation_query_type_get_type())
GType gst_navigation_message_type_get_type (void);
#define GST_TYPE_NAVIGATION_MESSAGE_TYPE (gst_navigation_message_type_get_type())
GType gst_navigation_event_type_get_type (void);
#define GST_TYPE_NAVIGATION_EVENT_TYPE (gst_navigation_event_type_get_type())

/* enumerations from "tunerchannel.h" */
GType gst_tuner_channel_flags_get_type (void);
#define GST_TYPE_TUNER_CHANNEL_FLAGS (gst_tuner_channel_flags_get_type())
G_END_DECLS

#endif /* __GST_INTERFACES_ENUM_TYPES_H__ */



