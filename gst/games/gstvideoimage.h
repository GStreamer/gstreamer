/* GStreamer
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 *               <2004> Benjamin Otte <otte@gnome.org
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

#include <gst/gst.h>

#ifndef __GST_VIDEO_IMAGE_H__
#define __GST_VIDEO_IMAGE_H__

G_BEGIN_DECLS


typedef struct _GstVideoColor GstVideoColor;
typedef struct _GstVideoImage GstVideoImage;
typedef struct _GstVideoFormat GstVideoFormat;

struct _GstVideoColor {
  int           Y, U, V;
  int           R, G, B;
};
extern const GstVideoColor GST_VIDEO_COLOR_WHITE;
extern const GstVideoColor GST_VIDEO_COLOR_YELLOW;
extern const GstVideoColor GST_VIDEO_COLOR_CYAN;
extern const GstVideoColor GST_VIDEO_COLOR_GREEN;
extern const GstVideoColor GST_VIDEO_COLOR_MAGENTA;
extern const GstVideoColor GST_VIDEO_COLOR_RED;
extern const GstVideoColor GST_VIDEO_COLOR_BLUE;
extern const GstVideoColor GST_VIDEO_COLOR_BLACK;
extern const GstVideoColor GST_VIDEO_COLOR_NEG_I;
extern const GstVideoColor GST_VIDEO_COLOR_POS_Q;
extern const GstVideoColor GST_VIDEO_COLOR_SUPER_BLACK;
extern const GstVideoColor GST_VIDEO_COLOR_DARK_GREY;


struct _GstVideoImage
{
  guint8 *      dest;           /* pointer to first byte of video data */
  guint8 *      yp, *up, *vp;   /* pointers to first byte of each component
                                 * for both packed/planar YUV and RGB */
  guint8 *      endptr;         /* pointer to byte beyond last video data */
  guint         ystride;
  guint         ustride;
  guint         vstride;
  guint         width;
  guint         height;
  const GstVideoFormat * format;
};

struct _GstVideoFormat
{
  char *        fourcc;
  char *        name;
  int           bitspp;
  void          (* paint_setup) (GstVideoImage * p, char *dest);
  void          (* paint_hline) (GstVideoImage * p, int x, int y, int w, const GstVideoColor *c);
  void          (* copy_hline) (GstVideoImage * dest, int destx, int desty,
      GstVideoImage * src, int srcx, int srcy, int w);
  int           ext_caps;
  int           depth;
  guint         red_mask;
  guint         green_mask;
  guint         blue_mask;
};

const GstVideoFormat *  gst_video_format_find_by_fourcc         (int find_fourcc);
const GstVideoFormat *  gst_video_format_find_by_name           (const char *name);
const GstVideoFormat *  gst_video_format_find_by_structure      (const GstStructure *structure);
GstStructure *          gst_video_format_get_structure          (const GstVideoFormat *format);
guint                   gst_video_format_get_size               (const GstVideoFormat *format, 
                                                                 guint w, guint h);

extern const GstVideoFormat gst_video_format_list[];
extern const guint gst_video_format_count;

void                    gst_video_image_setup                   (GstVideoImage *image, 
                                                                 const GstVideoFormat *format,
                                                                 guint8 *data, guint w, guint h);
                                                                         
/* drawing operations */
void                    gst_video_image_draw_hline              (GstVideoImage *image,
                                                                 gint x, gint y, gint w,
                                                                 const GstVideoColor *c);
void                    gst_video_image_draw_rectangle          (GstVideoImage *image,
                                                                 gint x, gint y, gint w, gint h,
                                                                 const GstVideoColor *c, gboolean filled);
void                    gst_video_image_copy_hline              (GstVideoImage *dest,
                                                                 gint xdest, gint ydest,
                                                                 GstVideoImage *src,
                                                                 gint xsrc, gint ysrc, gint w);
void                    gst_video_image_copy_area               (GstVideoImage *dest,
                                                                 gint xdest, gint ydest,
                                                                 GstVideoImage *src,
                                                                 gint xsrc, gint ysrc, 
                                                                 gint w, gint h);

G_END_DECLS

#endif /* __GST_VIDEO_IMAGE_H__ */
