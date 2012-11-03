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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <encoderparams.hh>
#include <string.h>

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
  const GValue *par_val;
  y4m_ratio_t fps;
  y4m_ratio_t par;

  if (!gst_structure_get_int (structure, "width", &width))
    width = -1;

  if (!gst_structure_get_int (structure, "height", &height))
    height = -1;

  fps_val = gst_structure_get_value (structure, "framerate");
  if (fps_val != NULL) {
    fps.n = gst_value_get_fraction_numerator (fps_val);
    fps.d = gst_value_get_fraction_denominator (fps_val);

    strm.frame_rate_code = mpeg_framerate_code (fps);
  } else
    strm.frame_rate_code = 0;

  par_val = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par_val != NULL) {
    par.n = gst_value_get_fraction_numerator (par_val);
    par.d = gst_value_get_fraction_denominator (par_val);
  } else {
    /* By default, assume square pixels */
    par.n = 1;
    par.d = 1;
  }

  strm.horizontal_size = width;
  strm.vertical_size = height;

  strm.interlacing_code = Y4M_ILACE_NONE;

  strm.aspect_ratio_code = mpeg_guess_mpeg_aspect_code (2, par,
      strm.horizontal_size, strm.vertical_size);

  GST_DEBUG_OBJECT (element, "Guessing aspect ratio code for PAR %d/%d "
      "yielded: %d", par.n, par.d, strm.aspect_ratio_code);
}

/*
 * Read a frame. Return true means EOS or error.
 */

bool
#if GST_MJPEGTOOLS_API >= 10900
    GstMpeg2EncPictureReader::LoadFrame (ImagePlanes & image)
#else
    GstMpeg2EncPictureReader::LoadFrame ()
#endif
{
#if GST_MJPEGTOOLS_API < 10900
  gint n;
#endif
  gint i, x, y, s;
  guint8 *frame;
  GstMpeg2enc *enc;
  GstVideoFrame vframe;

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

  gst_video_frame_map (&vframe, &enc->vinfo, enc->buffer, GST_MAP_READ);
//  frame = GST_BUFFER_DATA (enc->buffer);
#if GST_MJPEGTOOLS_API < 10900
  n = frames_read % input_imgs_buf_size;
#endif
  frame = GST_VIDEO_FRAME_COMP_DATA (&vframe, 0);
  s = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 0);
  x = encparams.horizontal_size;
  y = encparams.vertical_size;

  for (i = 0; i < y; i++) {
#if GST_MJPEGTOOLS_API >= 10900
    memcpy (image.Plane (0) + i * encparams.phy_width, frame, x);
#else
    memcpy (input_imgs_buf[n][0] + i * encparams.phy_width, frame, x);
#endif
    frame += s;
  }
#if GST_MJPEGTOOLS_API < 10900
  lum_mean[n] = LumMean (input_imgs_buf[n][0]);
#endif
  frame = GST_VIDEO_FRAME_COMP_DATA (&vframe, 1);
  s = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 1);
  x >>= 1;
  y >>= 1;
  for (i = 0; i < y; i++) {
#if GST_MJPEGTOOLS_API >= 10900
    memcpy (image.Plane (1) + i * encparams.phy_chrom_width, frame, x);
#else
    memcpy (input_imgs_buf[n][1] + i * encparams.phy_chrom_width, frame, x);
#endif
    frame += s;
  }
  frame = GST_VIDEO_FRAME_COMP_DATA (&vframe, 2);
  s = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 2);
  for (i = 0; i < y; i++) {
#if GST_MJPEGTOOLS_API >= 10900
    memcpy (image.Plane (2) + i * encparams.phy_chrom_width, frame, x);
#else
    memcpy (input_imgs_buf[n][2] + i * encparams.phy_chrom_width, frame, x);
#endif
    frame += s;
  }
  gst_video_frame_unmap (&vframe);
  gst_buffer_unref (enc->buffer);
  enc->buffer = NULL;

  /* inform the element the buffer has been processed */
  GST_MPEG2ENC_SIGNAL (enc);
  GST_MPEG2ENC_MUTEX_UNLOCK (enc);

  return FALSE;
}
