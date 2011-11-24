/* 
 * gstmms.h: header file for gst-mms plugin
 */

#ifndef __GST_MMS_H__
#define __GST_MMS_H__

#include <gst/gst.h>
#include <libmms/mmsx.h>
#include <gst/base/gstpushsrc.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_MMS \
  (gst_mms_get_type())
#define GST_MMS(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MMS,GstMMS))
#define GST_MMS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MMS,GstMMSClass))
#define GST_IS_MMS(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MMS))
#define GST_IS_MMS_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MMS))

typedef struct _GstMMS      GstMMS;
typedef struct _GstMMSClass GstMMSClass;

struct _GstMMS
{
  GstPushSrc parent;

  gchar  *uri_name;
  gchar  *current_connection_uri_name;
  guint64  connection_speed;
  
  mmsx_t *connection;
};

struct _GstMMSClass 
{
  GstPushSrcClass parent_class;
};

GType gst_mms_get_type (void);

G_END_DECLS

#endif /* __GST_MMS_H__ */
