/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmpeg2encstreamwriter.cc: GStreamer/mpeg2enc output wrapper
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

#include "gstmpeg2encstreamwriter.hh"

#define BUFSIZE (128*1024)

/*
 * Class init stuff.
 */

GstMpeg2EncStreamWriter::GstMpeg2EncStreamWriter (GstPad * in_pad, EncoderParams * params):
ElemStrmWriter (*params)
{
  pad = in_pad;
  buf = NULL;
}

/*
 * Output functions.
 */

void
GstMpeg2EncStreamWriter::PutBits (guint32 val, gint n)
{
  /* only relevant bits. Note that (according to Andrew),
   * some CPUs do bitshifts modulo wordsize (32), which
   * means that we have to check for n != 32 before
   * bitshifting to the relevant bits (i.e. 0xffffffff <<
   * 32 == 0xffffffff). */
  if (n != 32)
    val &= ~(0xffffffffU << n);

  /* write data */
  while (n >= outcnt) {
    if (!buf) {
      buf = gst_buffer_new_and_alloc (BUFSIZE);
      GST_BUFFER_SIZE (buf) = 0;
    }

    outbfr = (outbfr << outcnt) | (val >> (n - outcnt));
    GST_BUFFER_DATA (buf)[GST_BUFFER_SIZE (buf)++] = outbfr;
    n -= outcnt;
    outcnt = 8;
    bytecnt++;

    if (GST_BUFFER_SIZE (buf) >= GST_BUFFER_MAXSIZE (buf))
      FrameFlush ();
  }

  /* cache remaining bits */
  if (n != 0) {
    outbfr = (outbfr << n) | val;
    outcnt -= n;
  }
}

void
GstMpeg2EncStreamWriter::FrameBegin ()
{
}

void
GstMpeg2EncStreamWriter::FrameFlush ()
{
  if (buf) {
    gst_pad_push (pad, GST_DATA (buf));
    buf = NULL;
  }
}

void
GstMpeg2EncStreamWriter::FrameDiscard ()
{
}
