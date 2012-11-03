/* GStreamer mpeg2enc (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Mark Nauwelaerts <manauw@skynet.be>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpeg2enc.hh"
#include "gstmpeg2encstreamwriter.hh"
#include <string.h>

#if GST_MJPEGTOOLS_API >= 10800

/*
 * Class init stuff.
 */

GstMpeg2EncStreamWriter::GstMpeg2EncStreamWriter (GstPad * in_pad,
    EncoderParams * params)
{
  pad = in_pad;
  gst_object_ref (pad);
  buf = NULL;
}

GstMpeg2EncStreamWriter::~GstMpeg2EncStreamWriter ()
{
  gst_object_unref (pad);
}

void
GstMpeg2EncStreamWriter::WriteOutBufferUpto (const guint8 * buffer,
    const guint32 flush_upto)
{
  GstBuffer *buf, *inbuf;
  GstMpeg2enc *enc = GST_MPEG2ENC (GST_PAD_PARENT (pad));

  buf = gst_buffer_new_and_alloc (flush_upto);
  gst_buffer_fill (buf, 0, buffer, flush_upto);
  flushed += flush_upto;

  /* this should not block anything else (e.g. chain), but if it does,
   * it's ok as mpeg2enc is not really a loop-based element, but push-based */
  GST_MPEG2ENC_MUTEX_LOCK (enc);
  /* best effort at giving output some meaningful time metadata
   * no mpeg2enc specs on this though, but it might help getting the output
   * into container formats that really do like timestamps (unlike mplex) */
  if ((inbuf = (GstBuffer *) g_queue_pop_head (enc->time))) {
    GST_BUFFER_TIMESTAMP (buf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (buf) = GST_BUFFER_DURATION (inbuf);
    gst_buffer_unref (inbuf);
  }
  enc->srcresult = gst_pad_push (pad, buf);
  GST_MPEG2ENC_MUTEX_UNLOCK (enc);
}

guint64
GstMpeg2EncStreamWriter::BitCount ()
{
  return flushed * 8ll;
}

#else

#define BUFSIZE (128*1024)

/*
 * Class init stuff.
 */

GstMpeg2EncStreamWriter::GstMpeg2EncStreamWriter (GstPad * in_pad, EncoderParams * params):
ElemStrmWriter (*params)
{
  pad = in_pad;
  gst_object_ref (pad);
  buf = NULL;
}

GstMpeg2EncStreamWriter::~GstMpeg2EncStreamWriter ()
{
  gst_object_unref (pad);
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

    if (GST_BUFFER_SIZE (buf) >= BUFSIZE)
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
  GstMpeg2enc *enc = GST_MPEG2ENC (GST_PAD_PARENT (pad));

  if (buf) {
    /* this should not block anything else (e.g. chain), but if it does,
     * it's ok as mpeg2enc is not really a loop-based element, but push-based */
    GST_MPEG2ENC_MUTEX_LOCK (enc);
    gst_buffer_set_caps (buf, GST_PAD_CAPS (pad));
    enc->srcresult = gst_pad_push (pad, buf);
    GST_MPEG2ENC_MUTEX_UNLOCK (enc);
    buf = NULL;
  }
}

void
GstMpeg2EncStreamWriter::FrameDiscard ()
{
}
#endif /* GST_MJPEGTOOLS_API >= 10800 */
