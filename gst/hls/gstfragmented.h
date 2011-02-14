#ifndef __GST_FRAGMENTED_H__
#define __GST_FRAGMENTED_H__

#include <gst/gst.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (fragmented_debug);

#define LOG_CAPS(obj, caps) GST_DEBUG_OBJECT (obj, "%s: %" GST_PTR_FORMAT, #caps, caps)

G_END_DECLS

#endif /* __GST_FRAGMENTED_H__ */
