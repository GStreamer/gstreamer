/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <mpegconsts.h>
#include <quantize.hh>
#include <ratectl.hh>
#include <seqencoder.hh>
#include <mpeg2coder.hh>

#include "gstmpeg2encoder.hh"

/*
 * Class init stuff.
 */

GstMpeg2Encoder::GstMpeg2Encoder (GstMpeg2EncOptions *options,
				  GstPad             *sinkpad,
				  const GstCaps      *caps,
				  GstPad             *srcpad) :
  MPEG2Encoder (*options)
{
  MPEG2EncInVidParams strm;

  /* I/O */
  reader = new GstMpeg2EncPictureReader (sinkpad, caps, &parms);
  reader->StreamPictureParams (strm);
  if (options->SetFormatPresets (strm)) {
    g_warning ("Eek! Format presets failed. This is really bad!");
  }
  writer = new GstMpeg2EncStreamWriter (srcpad, &parms);

  /* encoding internals */
  quantizer = new Quantizer (parms);
  coder = new MPEG2Coder (parms, *writer);
  bitrate_controller = new OnTheFlyRateCtl (parms);

  /* sequencer */
  seqencoder = new SeqEncoder (parms, *reader, *quantizer,
			       *writer, *coder, *bitrate_controller);

  parms.Init (*options);
  reader->Init ();
  quantizer->Init ();
}

/*
 * One image.
 */

void
GstMpeg2Encoder::encodePicture ()
{
  /* hm, this is all... eek! */
  seqencoder->Encode ();
}

/*
 * Get current output format.
 */

GstCaps *
GstMpeg2Encoder::getFormat ()
{
  gdouble fps = Y4M_RATIO_DBL (mpeg_framerate (options.frame_rate));

  return gst_caps_new_simple ("video/mpeg",
			      "systemstream", G_TYPE_BOOLEAN, FALSE,
			      "mpegversion",  G_TYPE_INT, options.mpeg,
			      "width",        G_TYPE_INT, options.in_img_width,
			      "height",       G_TYPE_INT, options.in_img_height,
			      "framerate",    G_TYPE_DOUBLE, fps,
			      NULL);
}
