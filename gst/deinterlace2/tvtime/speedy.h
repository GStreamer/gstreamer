/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifndef SPEEDY_H_INCLUDED
#define SPEEDY_H_INCLUDED

#if defined (__SVR4) && defined (__sun)
# include <sys/int_types.h>
#else
# include <stdint.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Speedy is a collection of optimized functions plus their C fallbacks.
 * This includes a simple system to select which functions to use
 * at runtime.
 *
 * The optimizations are done with the help of the mmx.h system, from
 * libmpeg2 by Michel Lespinasse and Aaron Holtzman.
 *
 * The library is a collection of function pointers which must be first
 * initialized by setup_speedy_calls() to point at the fastest available
 * implementation of each function.
 */

/**
 * Struct for pulldown detection metrics.
 */
typedef struct pulldown_metrics_s {
    /* difference: total, even lines, odd lines */
    int d, e, o;
    /* noise: temporal, spacial (current), spacial (past) */
    int t, s, p;
} pulldown_metrics_t;

/**
 * Interpolates a packed 4:2:2 scanline using linear interpolation.
 */
extern void (*interpolate_packed422_scanline)( uint8_t *output, uint8_t *top,
                                               uint8_t *bot, int width );

/**
 * Blits a colour to a packed 4:2:2 scanline.
 */
extern void (*blit_colour_packed422_scanline)( uint8_t *output,
                                               int width, int y, int cb, int cr );

/**
 * Blits a colour to a packed 4:4:4:4 scanline.  I use luma/cb/cr instead of
 * RGB but this will of course work for either.
 */
extern void (*blit_colour_packed4444_scanline)( uint8_t *output,
                                                int width, int alpha, int luma,
                                                int cb, int cr );

/**
 * Blit from and to packed 4:2:2 scanline.
 */
extern void (*blit_packed422_scanline)( uint8_t *dest, const uint8_t *src, int width );

/**
 * Composites a premultiplied 4:4:4:4 pixel onto a packed 4:2:2 scanline.
 */
extern void (*composite_colour4444_alpha_to_packed422_scanline)( uint8_t *output, uint8_t *input,
                                                                 int af, int y, int cb, int cr,
                                                                 int width, int alpha );

/**
 * Composites a packed 4:4:4:4 scanline onto a packed 4:2:2 scanline.
 * Chroma is downsampled by dropping samples (nearest neighbour).
 */
extern void (*composite_packed4444_to_packed422_scanline)( uint8_t *output,
                                                           uint8_t *input,
                                                           uint8_t *foreground,
                                                           int width );

/**
 * Composites a packed 4:4:4:4 scanline onto a packed 4:2:2 scanline.
 * Chroma is downsampled by dropping samples (nearest neighbour).  The
 * alpha value provided is in the range 0-256 and is first applied to
 * the input (for fadeouts).
 */
extern void (*composite_packed4444_alpha_to_packed422_scanline)( uint8_t *output,
                                                                 uint8_t *input,
                                                                 uint8_t *foreground,
                                                                 int width, int alpha );

/**
 * Takes an alphamask and the given colour (in Y'CbCr) and composites it
 * onto a packed 4:4:4:4 scanline.
 */
extern void (*composite_alphamask_to_packed4444_scanline)( uint8_t *output,
                                                           uint8_t *input,
                                                           uint8_t *mask, int width,
                                                           int textluma, int textcb,
                                                           int textcr );

/**
 * Takes an alphamask and the given colour (in Y'CbCr) and composites it
 * onto a packed 4:4:4:4 scanline.  The alpha value provided is in the
 * range 0-256 and is first applied to the input (for fadeouts).
 */
extern void (*composite_alphamask_alpha_to_packed4444_scanline)( uint8_t *output,
                                                                 uint8_t *input,
                                                                 uint8_t *mask, int width,
                                                                 int textluma, int textcb,
                                                                 int textcr, int alpha );

/**
 * Premultiplies the colour by the alpha channel in a packed 4:4:4:4
 * scanline.
 */
extern void (*premultiply_packed4444_scanline)( uint8_t *output, uint8_t *input, int width );

/**
 * Blend between two packed 4:2:2 scanline.  Pos is the fade value in
 * the range 0-256.  A value of 0 gives 100% src1, and a value of 256
 * gives 100% src2.  Anything in between gives the appropriate faded
 * version.
 */
extern void (*blend_packed422_scanline)( uint8_t *output, uint8_t *src1,
                                         uint8_t *src2, int width, int pos );

/**
 * Calculates the 'difference factor' for two scanlines.  This is a
 * metric where higher values indicate that the two scanlines are more
 * different.
 */
extern unsigned int (*diff_factor_packed422_scanline)( uint8_t *cur, uint8_t *old, int width );

/**
 * Calculates the 'comb factor' for a set of three scanlines.  This is a
 * metric where higher values indicate a more likely chance that the two
 * fields are at separate points in time.
 */
extern unsigned int (*comb_factor_packed422_scanline)( uint8_t *top, uint8_t *mid,
                                                       uint8_t *bot, int width );

/**
 * Vertical [1 2 1] chroma filter.
 */
extern void (*vfilter_chroma_121_packed422_scanline)( uint8_t *output, int width,
                                                      uint8_t *m, uint8_t *t, uint8_t *b );

/**
 * Vertical [3 3 2] chroma filter.
 */
extern void (*vfilter_chroma_332_packed422_scanline)( uint8_t *output, int width,
                                                      uint8_t *m, uint8_t *t, uint8_t *b );

/**
 * Sets the chroma of the scanline to neutral (128) in-place.
 */
extern void (*kill_chroma_packed422_inplace_scanline)( uint8_t *data, int width );

/**
 * Mirrors the scanline in-place.
 */
extern void (*mirror_packed422_inplace_scanline)( uint8_t *data, int width );

/**
 * Inverts the colours on a scanline in-place.
 */
extern void (*invert_colour_packed422_inplace_scanline)( uint8_t *data, int width );

/**
 * Fast memcpy function, used by all of the blit functions.  Won't blit
 * anything if dest == src.
 */
extern void (*speedy_memcpy)( void *output, const void *input, size_t size );

/**
 * Calculates the block difference metrics for dalias' pulldown
 * detection algorithm.
 */
extern void (*diff_packed422_block8x8)( pulldown_metrics_t *m, uint8_t *old,
                                        uint8_t *new, int os, int ns );

/**
 * Takes an alpha mask and subpixelly blits it using linear
 * interpolation.
 */
extern void (*a8_subpix_blit_scanline)( uint8_t *output, uint8_t *input,
                                        int lasta, int startpos, int width );

/**
 * 1/4 vertical subpixel blit for packed 4:2:2 scanlines using linear
 * interpolation.
 */
extern void (*quarter_blit_vertical_packed422_scanline)( uint8_t *output, uint8_t *one,
                                                         uint8_t *three, int width );

/**
 * Vertical subpixel blit for packed 4:2:2 scanlines using linear
 * interpolation.
 */
extern void (*subpix_blit_vertical_packed422_scanline)( uint8_t *output, uint8_t *top,
                                                        uint8_t *bot, int subpixpos, int width );

/**
 * Simple function to convert a 4:4:4 scanline to a 4:4:4:4 scanline by
 * adding an alpha channel.  Result is non-premultiplied.
 */
extern void (*packed444_to_nonpremultiplied_packed4444_scanline)( uint8_t *output, 
                                                                  uint8_t *input,
                                                                  int width, int alpha );

/**
 * I think this function needs to be rethought and renamed, but here
 * it is for now.  This function horizontally resamples a scanline
 * using linear interpolation to compensate for a change in pixel
 * aspect ratio.
 */
extern void (*aspect_adjust_packed4444_scanline)( uint8_t *output,
                                                  uint8_t *input, 
                                                  int width,
                                                  double pixel_aspect );

/**
 * Convert a packed 4:4:4 surface to a packed 4:2:2 surface using
 * nearest neighbour chroma downsampling.
 */
extern void (*packed444_to_packed422_scanline)( uint8_t *output,
                                                uint8_t *input,
                                                int width );

/**
 * Converts packed 4:2:2 to packed 4:4:4 scanlines using nearest
 * neighbour chroma upsampling.
 */
extern void (*packed422_to_packed444_scanline)( uint8_t *output,
                                                uint8_t *input,
                                                int width );

/**
 * This filter actually does not meet the spec so calling it rec601
 * is a bit of a lie.  I got the filter from Poynton's site.  This
 * converts a scanline from packed 4:2:2 to packed 4:4:4.  But this
 * function should point at some high quality to-the-spec resampler.
 */
extern void (*packed422_to_packed444_rec601_scanline)( uint8_t *dest,
                                                       uint8_t *src,
                                                       int width );

/**
 * Conversions between Y'CbCr and R'G'B'.  We use Rec.601 numbers
 * since our source is broadcast video, but I think there is an
 * argument to be made for switching to Rec.709.
 */
extern void (*packed444_to_rgb24_rec601_scanline)( uint8_t *output,
                                                   uint8_t *input,
                                                   int width );
extern void (*rgb24_to_packed444_rec601_scanline)( uint8_t *output,
                                                   uint8_t *input,
                                                   int width );
extern void (*rgba32_to_packed4444_rec601_scanline)( uint8_t *output,
                                                     uint8_t *input,
                                                     int width );

/**
 * Convert from 4:2:2 with UYVY ordering to 4:2:2 with YUYV ordering.
 */
extern void (*convert_uyvy_to_yuyv_scanline)( uint8_t *uyvy_buf,
                                              uint8_t *yuyv_buf, int width );

/**
 * Sets up the function pointers to point at the fastest function
 * available.  Requires accelleration settings (see mm_accel.h).
 */
void setup_speedy_calls( uint32_t accel, int verbose );

/**
 * Returns a bitfield of what accellerations were used when speedy was
 * initialized.  See mm_accel.h.
 */
uint32_t speedy_get_accel( void );

#ifdef __cplusplus
};
#endif
#endif /* SPEEDY_H_INCLUDED */
