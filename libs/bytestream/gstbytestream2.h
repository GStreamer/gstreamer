#ifndef __GST_BYTESTREAM2_H__
#define __GST_BYTESTREAM2_H__

#include <gst/gstpad.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstByteStream2 GstByteStream2;

struct _GstByteStream2 {
  GstPad *pad;

  guint64 readptr;
  guint64 flushptr;
  guint64 size;

  GstBuffer *curbuf;
  guint32 curbufavail;

  GSList *buflist;
  gint listcount;
  guint32 listavail;
};

GstByteStream2 *	gst_bytestream2_new		(GstPad *pad);

GstBuffer *		gst_bytestream2_read		(GstByteStream2 *bs, guint32 len);
GstBuffer *		gst_bytestream2_peek		(GstByteStream2 *bs, guint32 len);
gboolean		gst_bytestream2_flush		(GstByteStream2 *bs, guint32 len);
guint8 *		gst_bytestream2_peek_bytes	(GstByteStream2 *bs, guint32 len);

#endif /* __GST_BYTESTREAM2_H__ */
