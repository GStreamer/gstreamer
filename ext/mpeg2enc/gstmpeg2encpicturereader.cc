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

#include "gstmpeg2encpicturereader.hh"

/*
 * Class init stuff.
 */

GstMpeg2EncPictureReader::GstMpeg2EncPictureReader (GstPad        *in_pad,
						    const GstCaps *in_caps,
						    EncoderParams *params) :
  PictureReader (*params)
{
  pad = in_pad;
  caps = gst_caps_copy (in_caps);
}

GstMpeg2EncPictureReader::~GstMpeg2EncPictureReader ()
{
  gst_caps_free (caps);
}

/*
 * Get input picture parameters (width/height etc.).
 */

void
GstMpeg2EncPictureReader::StreamPictureParams (MPEG2EncInVidParams &strm)
{
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  gint width, height;
  gdouble fps;

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_double (structure, "framerate", &fps);

  strm.horizontal_size = width;
  strm.vertical_size = height;
  strm.frame_rate_code = mpeg_framerate_code (mpeg_conform_framerate (fps));
  strm.interlacing_code = Y4M_ILACE_NONE;
  strm.aspect_ratio_code = mpeg_guess_mpeg_aspect_code (2, y4m_sar_SQUARE,
							strm.horizontal_size,
							strm.vertical_size);

  /* FIXME:
   * strm.interlacing_code = y4m_si_get_interlace(&si);
   * sar = y4m_si_get_sampleaspect(&si);
   * strm.aspect_ratio_code =
   *     mpeg_guess_mpeg_aspect_code(2, sar,
   *                                 strm.horizontal_size,
   *                                 strm.vertical_size);
   */
}

/*
 * Read a frame.
 */

bool
GstMpeg2EncPictureReader::LoadFrame ()
{
  GstData *data;
  GstBuffer *buf = NULL;
  gint i, x, y, n;
  guint8 *frame;

  do {
    if ((data = (GstData *) gst_pad_get_element_private (pad))) {
      gst_pad_set_element_private (pad, NULL);
    } else if (!(data = gst_pad_pull (pad))) {
      return true;
    }
    if (GST_IS_EVENT (data)) {
      if (GST_EVENT_TYPE (data) == GST_EVENT_EOS) {
        gst_pad_event_default (pad, GST_EVENT (data));
        return true;
      }
      gst_pad_event_default (pad, GST_EVENT (data));
    } else {
      buf = GST_BUFFER (data);
    }
  } while (!buf);

  frame = GST_BUFFER_DATA (buf);
  n = frames_read % input_imgs_buf_size;
  x = encparams.horizontal_size;
  y = encparams.vertical_size;

  for (i = 0; i < y; i++) {
    memcpy (input_imgs_buf[n][0]+i*encparams.phy_width, frame, x);
    frame += x;
  }
  lum_mean[n] = LumMean (input_imgs_buf[n][0]);
  x >>= 1;
  y >>= 1;
  for (i = 0; i < y; i++) {
    memcpy (input_imgs_buf[n][1]+i*encparams.phy_chrom_width, frame, x);
    frame += x;
  }
  for (i = 0; i < y; i++) {
    memcpy (input_imgs_buf[n][2]+i*encparams.phy_chrom_width, frame, x);
    frame += x;
  }
  gst_buffer_unref (buf);

  return false;
}
