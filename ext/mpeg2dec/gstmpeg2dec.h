/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef __GST_MPEG2DEC_H__
#define __GST_MPEG2DEC_H__


#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <mpeg2.h>

G_BEGIN_DECLS

#define GST_TYPE_MPEG2DEC \
  (gst_mpeg2dec_get_type())
#define GST_MPEG2DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MPEG2DEC,GstMpeg2dec))
#define GST_MPEG2DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MPEG2DEC,GstMpeg2decClass))
#define GST_IS_MPEG2DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MPEG2DEC))
#define GST_IS_MPEG2DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MPEG2DEC))

#define MPEG_TIME_TO_GST_TIME(time) ((time) == -1 ? -1 : ((time) * (GST_MSECOND/10)) / G_GINT64_CONSTANT(9))
#define GST_TIME_TO_MPEG_TIME(time) ((time) == -1 ? -1 : ((time) * G_GINT64_CONSTANT(9)) / (GST_MSECOND/10))

typedef struct _GstMpeg2dec GstMpeg2dec;
typedef struct _GstMpeg2decClass GstMpeg2decClass;

typedef enum
{
  MPEG2DEC_DISC_NONE            = 0,
  MPEG2DEC_DISC_NEW_PICTURE,
  MPEG2DEC_DISC_NEW_KEYFRAME
} DiscontState;

struct _GstMpeg2dec {
  GstVideoDecoder element;

  mpeg2dec_t    *decoder;
  const mpeg2_info_t *info;

  gboolean       closed;
  gboolean       have_fbuf;

  /* Buffer lifetime management */
  GList         *buffers;

  /* FIXME This should not be necessary. It is used to prevent image
   * corruption when the parser does not behave the way it should.
   * See https://bugzilla.gnome.org/show_bug.cgi?id=674238
   */
  DiscontState   discont_state;

  /* video state */
  GstVideoCodecState *input_state;
  GstVideoInfo        decoded_info;
  gboolean       need_cropping;
  gboolean       has_cropping;

  guint8        *dummybuf[4];
};

struct _GstMpeg2decClass {
  GstVideoDecoderClass parent_class;
};

GType gst_mpeg2dec_get_type(void);

G_END_DECLS

#endif /* __GST_MPEG2DEC_H__ */
