/* 
 * efence.h
 */

#ifndef __GST_EFENCE_H__
#define __GST_EFENCE_H__

#include <gst/gst.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* #define's don't like whitespacey bits */
#define GST_TYPE_EFENCE \
  (gst_gst_efence_get_type())
#define GST_EFENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_EFENCE,GstEFence))
#define GST_EFENCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_EFENCE,GstEFence))
#define GST_IS_EFENCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_EFENCE))
#define GST_IS_EFENCE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_EFENCE))

typedef struct _GstEFence      GstEFence;
typedef struct _GstEFenceClass GstEFenceClass;

struct _GstEFence
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  gboolean fence_top;
};

struct _GstEFenceClass 
{
  GstElementClass parent_class;
};

GType gst_gst_efence_get_type (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_EFENCE_H__ */
