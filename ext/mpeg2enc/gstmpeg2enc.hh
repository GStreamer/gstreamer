/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmpeg2enc.hh: object definition
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_MPEG2ENC_H__
#define __GST_MPEG2ENC_H__

#include <gst/gst.h>
#include "gstmpeg2encoptions.hh"
#include "gstmpeg2encoder.hh"

G_BEGIN_DECLS

#define GST_TYPE_MPEG2ENC \
  (gst_mpeg2enc_get_type ())
#define GST_MPEG2ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MPEG2ENC, GstMpeg2enc))
#define GST_MPEG2ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MPEG2ENC, GstMpeg2enc))
#define GST_IS_MPEG2ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MPEG2ENC))
#define GST_IS_MPEG2ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MPEG2ENC))

typedef struct _GstMpeg2enc {
  GstElement parent;

  /* pads */
  GstPad *sinkpad, *srcpad;

  /* options wrapper */
  GstMpeg2EncOptions *options;

  /* general encoding object (contains rest) */
  GstMpeg2Encoder *encoder;
} GstMpeg2enc;

typedef struct _GstMpeg2encClass {
  GstElementClass parent;
} GstMpeg2encClass;

GType    gst_mpeg2enc_get_type    (void);

G_END_DECLS

#endif /* __GST_MPEG2ENC_H__ */
