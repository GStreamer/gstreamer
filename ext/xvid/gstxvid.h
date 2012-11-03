/* GStreamer xvid encoder/decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_XVID_H__
#define __GST_XVID_H__

#include <gst/gst.h>

G_BEGIN_DECLS

#define gst_xvid_init_struct(s) \
  do { \
    memset (&s, 0, sizeof(s)); \
    s.version = XVID_VERSION; \
  } while (0);

#define RGB_24_32_STATIC_CAPS(bpp, r_mask,g_mask,b_mask) \
  "video/x-raw-rgb, " \
  "width = (int) [ 0, MAX ], " \
  "height = (int) [ 0, MAX], " \
  "framerate = (fraction) [ 0, MAX], " \
  "depth = (int) 24, " \
  "bpp = (int) " G_STRINGIFY (bpp) ", " \
  "endianness = (int) BIG_ENDIAN, " \
  "red_mask = (int) " G_STRINGIFY (r_mask) ", " \
  "green_mask = (int) " G_STRINGIFY (g_mask) ", " \
  "blue_mask = (int) " G_STRINGIFY (b_mask)

extern const gchar *gst_xvid_error (int errorcode);
extern gboolean	    gst_xvid_init  (void);

extern gint         gst_xvid_structure_to_csp (GstStructure *structure);
extern GstCaps *    gst_xvid_csp_to_caps      (gint csp, gint w, gint h);
extern gint         gst_xvid_image_get_size (gint csp,
                                             gint width, gint height);
extern gint         gst_xvid_image_fill (xvid_image_t * im, void * ptr, gint csp,
                                         gint width, gint height);

G_END_DECLS

#endif /* __GST_XVID_H__ */
