#ifndef __GST_BYTESTREAM_H__
#define __GST_BYTESTREAM_H__

#include <gst/gstpad.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef struct _GstByteStream GstByteStream;

struct _GstByteStream {
  GstPad *pad;

  GSList *buflist;
  guint32 headbufavail;
  guint32 listavail;
};

GstByteStream*		gst_bytestream_new		(GstPad *pad);
void			gst_bytestream_destroy		(GstByteStream *bs);

GstBuffer*		gst_bytestream_read		(GstByteStream *bs, guint32 len);
GstBuffer*		gst_bytestream_peek		(GstByteStream *bs, guint32 len);
guint8*			gst_bytestream_peek_bytes	(GstByteStream *bs, guint32 len);
gboolean		gst_bytestream_flush		(GstByteStream *bs, guint32 len);

void 			gst_bytestream_print_status	(GstByteStream *bs);

#endif /* __GST_BYTESTREAM_H__ */
