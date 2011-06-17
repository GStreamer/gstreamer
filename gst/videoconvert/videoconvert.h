/* Video conversion functions
 * Copyright (C) 2010 David Schleef <ds@schleef.org>
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

#ifndef __COLORSPACE_H__
#define __COLORSPACE_H__

#include <gst/video/video.h>

G_BEGIN_DECLS

typedef struct _VideoConvert VideoConvert;

typedef enum {
  COLOR_SPEC_NONE = 0,
  COLOR_SPEC_RGB,
  COLOR_SPEC_GRAY,
  COLOR_SPEC_YUV_BT470_6,
  COLOR_SPEC_YUV_BT709
} ColorSpaceColorSpec;

typedef enum {
  DITHER_NONE,
  DITHER_VERTERR,
  DITHER_HALFTONE
} ColorSpaceDitherMethod;

struct _VideoConvert {
  gint width, height;
  gboolean interlaced;
  gboolean use_16bit;
  gboolean dither;

  GstVideoFormat from_format;
  ColorSpaceColorSpec from_spec;
  GstVideoFormat to_format;
  ColorSpaceColorSpec to_spec;
  guint32 *palette;

  guint8 *tmpline;
  guint16 *tmpline16;
  guint16 *errline;

  void (*convert) (VideoConvert *convert, GstVideoFrame *dest, const GstVideoFrame *src);
  void (*getline) (VideoConvert *convert, guint8 *dest, const GstVideoFrame *src, int j);
  void (*putline) (VideoConvert *convert, GstVideoFrame *dest, const guint8 *src, int j);
  void (*matrix) (VideoConvert *convert);

  void (*getline16) (VideoConvert *convert, guint16 *dest, const GstVideoFrame *src, int j);
  void (*putline16) (VideoConvert *convert, GstVideoFrame *dest, const guint16 *src, int j);
  void (*matrix16) (VideoConvert *convert);
  void (*dither16) (VideoConvert *convert, int j);
};

VideoConvert *   videoconvert_convert_new            (GstVideoFormat to_format,
                                                      ColorSpaceColorSpec from_spec,
                                                      GstVideoFormat from_format,
                                                      ColorSpaceColorSpec to_spec,
                                                      int width, int height);
void             videoconvert_convert_free           (VideoConvert * convert);

void             videoconvert_convert_set_dither     (VideoConvert * convert, int type);
void             videoconvert_convert_set_interlaced (VideoConvert *convert,
                                                      gboolean interlaced);

void             videoconvert_convert_set_palette    (VideoConvert *convert,
                                                      const guint32 *palette);
const guint32 *  videoconvert_convert_get_palette    (VideoConvert *convert);

void             videoconvert_convert_convert        (VideoConvert * convert,
                                                      GstVideoFrame *dest, const GstVideoFrame *src);


G_END_DECLS

#endif /* __GST_COLORSPACE_H__ */
