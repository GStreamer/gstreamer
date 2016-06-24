/* GStreamer JPEG 2000 Parser
 * Copyright (C) <2016> Grok Image Compression Inc.
 *  @author Aaron Boxer <boxerab@gmail.com>
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

#ifndef __GST_JPEG2000_PARSE_H__
#define __GST_JPEG2000_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstbaseparse.h>
#include <gst/codecparsers/gstjpeg2000sampling.h>

G_BEGIN_DECLS
#define GST_TYPE_JPEG2000_PARSE \
  (gst_jpeg2000_parse_get_type())
#define GST_JPEG2000_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_JPEG2000_PARSE,GstJPEG2000Parse))
#define GST_JPEG2000_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_JPEG2000_PARSE,GstJPEG2000ParseClass))
#define GST_IS_JPEG2000_PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_JPEG2000_PARSE))
#define GST_IS_JPEG2000_PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_JPEG2000_PARSE))
    GType gst_jpeg2000_parse_get_type (void);

typedef struct _GstJPEG2000Parse GstJPEG2000Parse;
typedef struct _GstJPEG2000ParseClass GstJPEG2000ParseClass;

#define GST_JPEG2000_PARSE_MAX_SUPPORTED_COMPONENTS 4

typedef enum
{
  GST_JPEG2000_PARSE_NO_CODEC,
  GST_JPEG2000_PARSE_JPC,       /* jpeg 2000 code stream */
  GST_JPEG2000_PARSE_J2C,       /* jpeg 2000 contiguous code stream box plus code stream */
  GST_JPEG2000_PARSE_JP2,       /* jpeg 2000 part I file format */

} GstJPEG2000ParseFormats;


struct _GstJPEG2000Parse
{
  GstBaseParse baseparse;


  guint width;
  guint height;

  GstJPEG2000Sampling sampling;
  GstJPEG2000Colorspace colorspace;
  GstJPEG2000ParseFormats codec_format;
};

struct _GstJPEG2000ParseClass
{
  GstBaseParseClass parent_class;


};


G_END_DECLS
#endif
