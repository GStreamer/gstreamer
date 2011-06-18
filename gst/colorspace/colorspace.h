/* Colorspace conversion functions
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

typedef struct _ColorspaceConvert ColorspaceConvert;
typedef struct _ColorspaceFrame ColorspaceComponent;

typedef enum {
  COLOR_SPEC_NONE = 0,
  COLOR_SPEC_RGB,
  COLOR_SPEC_GRAY,
  COLOR_SPEC_YUV_BT470_6,
  COLOR_SPEC_YUV_BT709,
  COLOR_SPEC_YUV_JPEG
} ColorSpaceColorSpec;

typedef enum {
  DITHER_NONE,
  DITHER_VERTERR,
  DITHER_HALFTONE
} ColorSpaceDitherMethod;

struct _ColorspaceComponent {
  int offset;
  int stride;
};

struct _ColorspaceConvert {
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

  int dest_offset[4];
  int dest_stride[4];
  int src_offset[4];
  int src_stride[4];

  void (*convert) (ColorspaceConvert *convert, guint8 *dest, const guint8 *src);
  void (*getline) (ColorspaceConvert *convert, guint8 *dest, const guint8 *src, int j);
  void (*putline) (ColorspaceConvert *convert, guint8 *dest, const guint8 *src, int j);
  void (*matrix) (ColorspaceConvert *convert);

  void (*getline16) (ColorspaceConvert *convert, guint16 *dest, const guint8 *src, int j);
  void (*putline16) (ColorspaceConvert *convert, guint8 *dest, const guint16 *src, int j);
  void (*matrix16) (ColorspaceConvert *convert);
  void (*dither16) (ColorspaceConvert *convert, int j);
};

ColorspaceConvert * colorspace_convert_new (GstVideoFormat to_format,
    ColorSpaceColorSpec from_spec, GstVideoFormat from_format,
    ColorSpaceColorSpec to_spec, int width, int height);
void colorspace_convert_set_dither (ColorspaceConvert * convert, int type);
void colorspace_convert_set_interlaced (ColorspaceConvert *convert,
    gboolean interlaced);
void colorspace_convert_set_palette (ColorspaceConvert *convert,
    const guint32 *palette);
const guint32 * colorspace_convert_get_palette (ColorspaceConvert *convert);
void colorspace_convert_free (ColorspaceConvert * convert);
void colorspace_convert_convert (ColorspaceConvert * convert,
    guint8 *dest, const guint8 *src);


G_END_DECLS

#endif /* __GST_COLORSPACE_H__ */
