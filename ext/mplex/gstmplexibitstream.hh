/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmplexibitstream.hh: gstreamer/mplex input bitstream wrapper
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

#ifndef __GST_MPLEXIBITSTREAM_H__
#define __GST_MPLEXIBITSTREAM_H__

#include <gst/gst.h>
#include <gst/bytestream/bytestream.h>
#include <mjpeg_types.h>
#include <bits.hpp>

class GstMplexIBitStream : public IBitStream {
public:
  GstMplexIBitStream (GstPad *pad, 
		      guint   buf_size = BUFFER_SIZE);
  ~GstMplexIBitStream (void);

protected:
  /* read data */
  size_t ReadStreamBytes (uint8_t *buf,
			  size_t   number);

  /* are we at EOS? */
  bool EndOfStream (void);

private:
  GstPad *pad;
  GstByteStream *bs;
  gboolean eos;
};

#endif /* __GST_MPLEXIBITSTREAM_H__ */
