/* GStreamer mplex (mjpegtools) wrapper
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstmplexoutputstream.hh: gstreamer/mplex output stream wrapper
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

#ifndef __GST_MPLEXOUTPUTSTREAM_H__
#define __GST_MPLEXOUTPUTSTREAM_H__

#include <gst/gst.h>
#include <mjpeg_types.h>
#include <outputstrm.hpp>

class GstMplexOutputStream : public OutputStream {
public:
  GstMplexOutputStream (GstElement *element,
			GstPad     *pad);

  /* open/close. Basically 'no-op's (close() sets EOS). */
  int  Open  (void);
  void Close (void);

  /* get size of current segment */
  off_t SegmentSize (void);

  /* next segment */
  void NextSegment (void);

  /* write data */
  void Write (guint8 *data,
	      guint   len);

private:
  GstElement *element;
  GstPad *pad;
  guint64 size;
};

#endif /* __GST_MPLEXOUTPUTSTREAM_H__ */
