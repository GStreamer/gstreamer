/* 
 * Copyright (C) 2009 Alex Ugarte <augarte@vicomtech.org>
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

#include <gst/gst.h>

#include <cairo.h>

void
gst_videomixer_blend_bgra_bgra (guint8 * src, gint xpos, gint ypos,
    gint src_width, gint src_height, gdouble src_alpha,
    guint8 * dest, gint dest_width, gint dest_height)
{
  cairo_surface_t *srcSurface = 0;
  cairo_surface_t *destSurface = 0;
  cairo_t *cairo = 0;

  srcSurface =
      cairo_image_surface_create_for_data (src, CAIRO_FORMAT_ARGB32, src_width,
      src_height, src_width * 4);
  g_assert (srcSurface);
  destSurface =
      cairo_image_surface_create_for_data (dest, CAIRO_FORMAT_ARGB32,
      dest_width, dest_height, dest_width * 4);
  g_assert (destSurface);
  cairo = cairo_create (destSurface);
  g_assert (cairo);

  //copy source buffer in destiation
  cairo_translate (cairo, xpos, ypos);
  cairo_set_source_surface (cairo, srcSurface, 0, 0);
  cairo_paint (cairo);

  cairo_surface_finish (srcSurface);
  cairo_surface_finish (destSurface);
  cairo_surface_destroy (srcSurface);
  cairo_surface_destroy (destSurface);
  cairo_destroy (cairo);

}

/* fill a buffer with a checkerboard pattern */
void
gst_videomixer_fill_bgra_checker (guint8 * dest, gint width, gint height)
{
  gint i, j;
  static int tab[] = { 80, 160, 80, 160 };

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       //blue
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       //green
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       //red    
      *dest++ = 0xFF;           //alpha
    }
  }
}

void
gst_videomixer_fill_bgra_color (guint8 * dest, gint width, gint height,
    gint colY, gint colU, gint colV)
{
  gint red, green, blue;
  gint i, j;

//check this conversion 
  red = 1.164 * (colY - 16) + 1.596 * (colV - 128);
  green = 1.164 * (colY - 16) - 0.813 * (colV - 128) - 0.391 * (colU - 128);
  blue = 1.164 * (colY - 16) + 2.018 * (colU - 128);


  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = 0xff;
      *dest++ = colY;
      *dest++ = colU;
      *dest++ = colV;
    }
  }
}
