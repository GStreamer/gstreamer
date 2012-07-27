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
#include "gstcms.h"

G_BEGIN_DECLS

typedef struct _VideoConvert VideoConvert;

typedef enum {
  DITHER_NONE,
  DITHER_VERTERR,
  DITHER_HALFTONE
} ColorSpaceDitherMethod;

struct _VideoConvert {
  GstVideoInfo in_info;
  GstVideoInfo out_info;

  gint width;
  gint height;

  gint in_bits;
  gint out_bits;
  gint cmatrix[4][4];

  guint32 *palette;

  ColorSpaceDitherMethod dither;

  guint8 *tmpline;
  guint16 *tmpline16;
  guint16 *errline;

  void (*convert) (VideoConvert *convert, GstVideoFrame *dest, const GstVideoFrame *src);
  void (*matrix) (VideoConvert *convert, guint8 * pixels);
  void (*matrix16) (VideoConvert *convert, guint16 * pixels);
  void (*dither16) (VideoConvert *convert, guint16 * pixels, int j);
};

VideoConvert *   videoconvert_convert_new            (GstVideoInfo *in_info,
                                                      GstVideoInfo *out_info);
void             videoconvert_convert_free           (VideoConvert * convert);

void             videoconvert_convert_set_dither     (VideoConvert * convert, int type);

void             videoconvert_convert_convert        (VideoConvert * convert,
                                                      GstVideoFrame *dest, const GstVideoFrame *src);


G_END_DECLS

#endif /* __GST_COLORSPACE_H__ */
