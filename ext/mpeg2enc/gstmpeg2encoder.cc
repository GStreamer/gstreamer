/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Mark Nauwelaerts <manauw@skynet.be>
 *
 * gstmpeg2encoder.cc: gstreamer/mpeg2enc encoder class
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

#include <mpegconsts.h>
#include <quantize.hh>
#if GST_MJPEGTOOLS_API >= 20000
#include <ontheflyratectlpass1.hh>
#include <ontheflyratectlpass2.hh>
#elif GST_MJPEGTOOLS_API >= 10900
#include <ontheflyratectl.hh>
#include <pass1ratectl.hh>
#include <pass2ratectl.hh>
#else
#include <ratectl.hh>
#endif
#include <seqencoder.hh>
#include <mpeg2coder.hh>

#include "gstmpeg2enc.hh"
#include "gstmpeg2encoder.hh"

/*
 * Class init stuff.
 */

GstMpeg2Encoder::GstMpeg2Encoder (GstMpeg2EncOptions * options, GstElement * in_element, GstCaps * in_caps):
MPEG2Encoder (*options)
{
  element = in_element;
  gst_object_ref (element);
  caps = in_caps;
  gst_caps_ref (in_caps);
  init_done = FALSE;
}

GstMpeg2Encoder::~GstMpeg2Encoder ()
{
  gst_caps_unref (caps);
  gst_object_unref (element);
}

gboolean GstMpeg2Encoder::setup ()
{
  MPEG2EncInVidParams
      strm;
  GstMpeg2enc *
      enc;

  enc = GST_MPEG2ENC (element);

  /* I/O */
  reader = new GstMpeg2EncPictureReader (element, caps, &parms);
  reader->StreamPictureParams (strm);
#if GST_MJPEGTOOLS_API == 10800
  /* chain thread caters for reading, do not need another thread for this */
  options.allow_parallel_read = FALSE;
#endif
  if (options.SetFormatPresets (strm)) {
    return FALSE;
  }
  writer = new GstMpeg2EncStreamWriter (enc->srcpad, &parms);

  /* encoding internals */
  quantizer = new Quantizer (parms);
#if GST_MJPEGTOOLS_API < 10900
  bitrate_controller = new OnTheFlyRateCtl (parms);
#else
  pass1ratectl = new OnTheFlyPass1 (parms);
  pass2ratectl = new OnTheFlyPass2 (parms);
#endif
#if GST_MJPEGTOOLS_API >= 10900
  /* sequencer */
  seqencoder = new SeqEncoder (parms, *reader, *quantizer,
      *writer, *pass1ratectl, *pass2ratectl);
#elif GST_MJPEGTOOLS_API >= 10800
  /* sequencer */
  seqencoder = new SeqEncoder (parms, *reader, *quantizer,
      *writer, *bitrate_controller);
#else
  coder = new MPEG2Coder (parms, *writer);
  /* sequencer */
  seqencoder = new SeqEncoder (parms, *reader, *quantizer,
      *writer, *coder, *bitrate_controller);
#endif

  return TRUE;
}

void
GstMpeg2Encoder::init ()
{
  if (!init_done) {
    parms.Init (options);
    reader->Init ();
    quantizer->Init ();
#if GST_MJPEGTOOLS_API >= 10800
    seqencoder->Init ();
#endif
    init_done = TRUE;
  }
}

/*
 * Process all input provided by the reader until it signals eos.
 */

void
GstMpeg2Encoder::encode ()
{
  /* hm, this is all... eek! */
#if GST_MJPEGTOOLS_API >= 10800
  seqencoder->EncodeStream ();
#else
  seqencoder->Encode ();
#endif
}

/*
 * Get current output format.
 */

GstCaps *
GstMpeg2Encoder::getFormat ()
{
  y4m_ratio_t fps = mpeg_framerate (options.frame_rate);

  return gst_caps_new_simple ("video/mpeg",
      "systemstream", G_TYPE_BOOLEAN, FALSE,
      "mpegversion", G_TYPE_INT, options.mpeg,
      "width", G_TYPE_INT, options.in_img_width,
      "height", G_TYPE_INT, options.in_img_height,
      "framerate", GST_TYPE_FRACTION, fps.n, fps.d, NULL);
}
