#ifndef __GST_BYTESTREAM2_H__
#define __GST_BYTESTREAM2_H__

#include <gst/gstpad.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstByteStream2 GstByteStream2;

struct _GstByteStream2 {
  GstPad *pad;

  GSList *buflist;
  guint32 headbufavail;
  guint32 listavail;
};

GstByteStream2 *	gst_bytestream2_new		(GstPad *pad);

GstBuffer *		gst_bytestream2_read		(GstByteStream2 *bs, guint32 len);
GstBuffer *		gst_bytestream2_peek		(GstByteStream2 *bs, guint32 len);
gboolean		gst_bytestream2_flush		(GstByteStream2 *bs, guint32 len);
guint8 *		gst_bytestream2_peek_bytes	(GstByteStream2 *bs, guint32 len);
void gst_bytestream2_print_status(GstByteStream2 *bs);

#endif /* __GST_BYTESTREAM2_H__ */
