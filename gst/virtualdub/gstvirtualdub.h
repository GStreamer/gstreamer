/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
 *
 * EffecTV is free software. We release this product under the terms of the
 * GNU General Public License version 2. The license is included in the file
 * COPYING.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more details.
 */

#include <gst/gst.h>

typedef unsigned int    Pixel;
typedef unsigned int    Pixel32;
typedef unsigned char   Pixel8;
typedef int             PixCoord;
typedef int             PixDim;
typedef int             PixOffset;


#define R_MASK  (0x00ff0000)
#define G_MASK  (0x0000ff00)
#define B_MASK  (0x000000ff)
#define R_SHIFT         (16)
#define G_SHIFT          (8)
#define B_SHIFT          (0)


GType gst_xsharpen_get_type (void);
extern GstElementDetails gst_xsharpen_details;

extern GstPadTemplate *gst_virtualdub_sink_factory ();
extern GstPadTemplate *gst_virtualdub_src_factory ();
