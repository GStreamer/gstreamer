/*
 * GStreamer QuickTime video decoder codecs wrapper
 * Copyright <2006, 2007> Fluendo <gstreamer@fluendo.com>
 * Copyright <2006, 2007> Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

#include <string.h>

#include "imagedescription.h"

static ImageDescription *
image_description_for_avc1 (GstBuffer * buf)
{
  ImageDescription *desc = NULL;
  guint8 *pos;

  desc = g_malloc0 (sizeof (ImageDescription) + GST_BUFFER_SIZE (buf) + 8);
  pos = (guint8 *) desc + sizeof (ImageDescription);

  desc->idSize = sizeof (ImageDescription) + GST_BUFFER_SIZE (buf) + 8;
  /* write size in Big-Endian */
  GST_WRITE_UINT32_BE (pos, GST_BUFFER_SIZE (buf) + 8);
  GST_WRITE_UINT32_LE (pos + 4, QT_MAKE_FOURCC_BE ('a', 'v', 'c', 'C'));
  memmove (pos + 8, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));

  return desc;
}

/* image_description_for_mp4v
 *
 * mpeg4 video has an 'esds' atom as extension for the ImageDescription.
 * It is meant to contain the ES Description.
 * We here create a fake one.
 */

static ImageDescription *
image_description_for_mp4v (GstBuffer * buf)
{
  ImageDescription *desc = NULL;
  guint32 offset = sizeof (ImageDescription);
  guint8 *location;

  GST_LOG ("buf %p , size:%d", buf, GST_BUFFER_SIZE (buf));

  /* this image description contains:
   *  ImageDescription  sizeof(ImageDescription)
   *  esds atom         34 bytes
   *  buffer            GST_BUFFER_SIZE (buf)
   *  ending            3 bytes
   */

  desc = g_malloc0 (offset + 37 + GST_BUFFER_SIZE (buf));
  desc->idSize = offset + 37 + GST_BUFFER_SIZE (buf);

  location = (guint8 *) desc + offset;

  /* Fill in ESDS */
  /*  size */
  GST_WRITE_UINT32_BE (location, 37 + GST_BUFFER_SIZE (buf));
  /*  atom */
  GST_WRITE_UINT32_LE (location + 4, GST_MAKE_FOURCC ('e', 's', 'd', 's'));
  /*  version + flags */
  QT_WRITE_UINT32 (location + 8, 0);
  /*  tag */
  QT_WRITE_UINT8 (location + 12, 0x3);
  /*  size (buffsize + 23) */
  QT_WRITE_UINT8 (location + 13, GST_BUFFER_SIZE (buf) + 23);
  /*  ESID */
  QT_WRITE_UINT16 (location + 14, 0);
  /*  priority */
  QT_WRITE_UINT8 (location + 16, 0);
  /*  tag */
  QT_WRITE_UINT8 (location + 17, 0x4);
  /*  size (buffsize + 8) */
  QT_WRITE_UINT8 (location + 18, GST_BUFFER_SIZE (buf) + 15);
  /*  object type */
  QT_WRITE_UINT8 (location + 19, 0x20);
  /*  stream type */
  QT_WRITE_UINT8 (location + 20, 0x11);
  /*  buffersize db */
  QT_WRITE_UINT24 (location + 21, 13640);
  /*  max bitrate */
  QT_WRITE_UINT32 (location + 24, 1849648);
  /*  avg bitrate */
  QT_WRITE_UINT32 (location + 28, 918191);
  /*  tag */
  QT_WRITE_UINT8 (location + 32, 0x05);
  /*  size */
  QT_WRITE_UINT8 (location + 33, GST_BUFFER_SIZE (buf));
  /*  codec data */
  memmove (location + 34, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  /*  end */
  QT_WRITE_UINT8 (location + 34 + GST_BUFFER_SIZE (buf), 0x06);
  QT_WRITE_UINT8 (location + 34 + GST_BUFFER_SIZE (buf) + 1, 0x01);
  QT_WRITE_UINT8 (location + 34 + GST_BUFFER_SIZE (buf) + 2, 0x02);

  return desc;
}

static ImageDescription *
image_description_from_stsd_buffer (GstBuffer * buf)
{
  ImageDescription *desc = NULL;
  guint8 *content;
  guint size;
  gint imds;

  GST_LOG ("buffer %p, size:%u", buf, GST_BUFFER_SIZE (buf));

  /* The buffer contains a full atom, we only need the contents */
  /* This buffer has data in big-endian, we need to read it as such.
   * except for the fourcc which are ALWAYS big-endian. */
  content = GST_BUFFER_DATA (buf) + 16;
  size = GST_BUFFER_SIZE (buf) - 16;

#if DEBUG_DUMP
  GST_LOG ("incoming data in big-endian");
  gst_util_dump_mem (content, size);
#endif

  desc = g_malloc0 (size);
  desc->idSize = size;
  desc->cType = GST_READ_UINT32_BE (content + 4);
  desc->version = QT_UINT16 (content + 16);
  desc->revisionLevel = QT_UINT16 (content + 18);
  desc->vendor = GST_READ_UINT32_BE (content + 20);
  desc->temporalQuality = QT_UINT32 (content + 24);
  desc->spatialQuality = QT_UINT32 (content + 24);
  desc->dataSize = QT_UINT32 (content + 44);
  desc->frameCount = QT_UINT16 (content + 48);
  desc->depth = QT_UINT16 (content + 82);
  desc->clutID = QT_UINT16 (content + 84);

  imds = 86;                    /* sizeof (ImageDescription); */

  if (desc->idSize > imds) {
    GST_LOG ("Copying %d bytes from %p to %p",
        size - imds, content + imds, desc + imds);
    memcpy ((guint8 *) desc + imds, (guint8 *) content + imds, size - imds);
  }
#if DEBUG_DUMP
  GST_LOG ("outgoing data in machine-endian");
  dump_image_description (desc);
#endif

  return desc;
}

ImageDescription *
image_description_from_codec_data (GstBuffer * buf, guint32 codectype)
{
  ImageDescription *desc = NULL;

  GST_LOG ("codectype:%" GST_FOURCC_FORMAT " buf:%p",
      GST_FOURCC_ARGS (codectype), buf);

  if ((GST_BUFFER_SIZE (buf) == GST_READ_UINT32_BE (GST_BUFFER_DATA (buf))) &&
      (QT_MAKE_FOURCC_LE ('s', 't', 's',
              'd') == GST_READ_UINT32_BE (GST_BUFFER_DATA (buf) + 4))) {
    /* We have the full stsd (ImageDescription) in our codec_data */
    desc = image_description_from_stsd_buffer (buf);
  } else {
    switch (codectype) {
      case QT_MAKE_FOURCC_LE ('m', 'p', '4', 'v'):
        desc = image_description_for_mp4v (buf);
        break;
      case QT_MAKE_FOURCC_LE ('a', 'v', 'c', '1'):
        desc = image_description_for_avc1 (buf);
        break;
      default:
        GST_WARNING ("Format not handled !");
    }
  }
  return desc;
}
