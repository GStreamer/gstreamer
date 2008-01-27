/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmpeg2encpicturereader.cc: GStreamer/mpeg2enc input wrapper
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <encoderparams.hh>

#ifdef GST_MJPEGTOOLS_19x
#include <imageplanes.hh>
#endif

#include "gstmpeg2enc.hh"
#include "gstmpeg2encpicturereader.hh"

/*
 * Class init stuff.
 */

GstMpeg2EncPictureReader::GstMpeg2EncPictureReader (GstElement * in_element, GstCaps * in_caps, EncoderParams * params):
PictureReader (*params)
{
  element = in_element;
  gst_object_ref (element);
  caps = in_caps;
  gst_caps_ref (caps);
}

GstMpeg2EncPictureReader::~GstMpeg2EncPictureReader ()
{
  gst_caps_unref (caps);
  gst_object_unref (element);
}

/*
 * Get input picture parameters (width/height etc.).
 */

void
GstMpeg2EncPictureReader::StreamPictureParams (MPEG2EncInVidParams & strm)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint width, height;
  const GValue *fps_val;
  y4m_ratio_t fps;

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  fps_val = gst_structure_get_value (structure, "framerate");
  fps.n = gst_value_get_fraction_numerator (fps_val);
  fps.d = gst_value_get_fraction_denominator (fps_val);

  strm.horizontal_size = width;
  strm.vertical_size = height;
  strm.frame_rate_code = mpeg_framerate_code (fps);
  strm.interlacing_code = Y4M_ILACE_NONE;
  /* FIXME perhaps involve pixel-aspect-ratio for 'better' sar */
  strm.aspect_ratio_code = mpeg_guess_mpeg_aspect_code (2, y4m_sar_SQUARE,
      strm.horizontal_size, strm.vertical_size);
}

/*
 * Read a frame. Return true means EOS or error.
 */

bool
#ifdef GST_MJPEGTOOLS_19x
    GstMpeg2EncPictureReader::LoadFrame (ImagePlanes & image)
#else
    GstMpeg2EncPictureReader::LoadFrame ()
#endif
{

#ifndef GST_MJPEGTOOLS_19x
  gint n;
#endif
  gint i, x, y;
  guint8 *frame;
  GstMpeg2enc *enc;

  enc = GST_MPEG2ENC (element);

  GST_MPEG2ENC_MUTEX_LOCK (enc);

  /* hang around until the element provides us with a buffer */
  while (!enc->buffer) {
    if (enc->eos) {
      GST_MPEG2ENC_MUTEX_UNLOCK (enc);
      /* inform the mpeg encoding loop that it can give up */
      return TRUE;
    }
    GST_MPEG2ENC_WAIT (enc);
  }

  frame = GST_BUFFER_DATA (enc->buffer);
#ifndef GST_MJPEGTOOLS_19x
  n = frames_read % input_imgs_buf_size;
#endif
  x = encparams.horizontal_size;
  y = encparams.vertical_size;

  for (i = 0; i < y; i++) {
#ifdef GST_MJPEGTOOLS_19x
    memcpy (image.Plane (0) + i * encparams.phy_width, frame, x);
#else
    memcpy (input_imgs_buf[n][0] + i * encparams.phy_width, frame, x);
#endif
    frame += x;
  }
#ifndef GST_MJPEGTOOLS_19x
  lum_mean[n] = LumMean (input_imgs_buf[n][0]);
#endif
  x >>= 1;
  y >>= 1;
  for (i = 0; i < y; i++) {
#ifdef GST_MJPEGTOOLS_19x
    memcpy (image.Plane (1) + i * encparams.phy_chrom_width, frame, x);
#else
    memcpy (input_imgs_buf[n][1] + i * encparams.phy_chrom_width, frame, x);
#endif
    frame += x;
  }
  for (i = 0; i < y; i++) {
#ifdef GST_MJPEGTOOLS_19x
    memcpy (image.Plane (2) + i * encparams.phy_chrom_width, frame, x);
#else
    memcpy (input_imgs_buf[n][2] + i * encparams.phy_chrom_width, frame, x);
#endif
    frame += x;
  }
  gst_buffer_unref (enc->buffer);
  enc->buffer = NULL;

  /* inform the element the buffer has been processed */
  GST_MPEG2ENC_SIGNAL (enc);
  GST_MPEG2ENC_MUTEX_UNLOCK (enc);

  return FALSE;
}
