/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "paint.h"
#include "gstmask.h"

static void
gst_bar_wipe_lr_update (GstMask *mask,
		        GstClockTime position,
		        GstClockTime duration)
{
  gint width = mask->width;
  gint height = mask->height;
  gint split = (position * width) / duration;

  gst_smpte_paint_rect (mask->data, width, split, 0, width - split, height, 0);
  gst_smpte_paint_rect (mask->data, width, 0,     0, split,         height, 255);
}

static void
gst_bar_wipe_tb_update (GstMask *mask,
		        GstClockTime position,
		        GstClockTime duration)
{
  gint width = mask->width;
  gint height = mask->height;
  gint split = (position * height) / duration;

  gst_smpte_paint_rect (mask->data, width, 0, 0,     width, split,          255);
  gst_smpte_paint_rect (mask->data, width, 0, split, width, height - split, 0);
}

static void
gst_box_wipe_update (GstMask *mask,
		     GstClockTime position,
		     GstClockTime duration)
{
  static gint box_wipe_impacts[8][4] = 
  {
    /* 3 -> 6 */
    { 0, 0, 0, 0 },
    { 2, 2, 0, 0 },
    { 2, 2, 2, 2 },
    { 0, 0, 2, 2 },
    /* 23 -> 26 */
    { 1, 1, 0, 0 },
    { 2, 2, 1, 1 },
    { 1, 1, 2, 2 },
    { 0, 0, 1, 1 },
  };
  gint *impacts = box_wipe_impacts[(mask->type & 0x0F) - 3];
  gint width = mask->width;
  gint height = mask->height;
  gint splitx = (position * width) / duration;
  gint splity = (position * height) / duration;

  gst_smpte_paint_rect (mask->data, width, 0, 0, width,  height, 0);
  gst_smpte_paint_rect (mask->data, width, 
		        (impacts[0] * width)/2  - (impacts[1] * splitx)/2,
		        (impacts[2] * height)/2 - (impacts[3] * splity)/2,
		        splitx, splity, 255);
}

static void
gst_fourc_box_wipe_update (GstMask *mask,
		           GstClockTime position,
		           GstClockTime duration)
{
  static gint box_wipe_impacts[8][4] = 
  {
    { 0, 0, 0, 0 },
    { 1, 1, 1, 1 },
    { 4, 2, 0, 0 },
    { 3, 1, 1, 1 },
    { 4, 2, 4, 2 },
    { 1, 1, 3, 1 },
    { 0, 0, 4, 2 },
    { 3, 1, 3, 1 },
  };
  gint *impacts;
  gint width = mask->width;
  gint height = mask->height;
  gint splitx = (position * width/2) / duration;
  gint splity = (position * height/2) / duration;
  gint i;

  gst_smpte_paint_rect (mask->data, width, 0, 0, width,  height, 0);

  for (i = 7; i > 0; i -= 2) {
    impacts = box_wipe_impacts[mask->type - i];
    gst_smpte_paint_rect (mask->data, width, 
		          (impacts[0] * width)/4  - (impacts[1] * splitx)/2,
		          (impacts[2] * height)/4 - (impacts[3] * splity)/2,
		          splitx, splity, 255);
  }
}

static GstMaskDefinition definitions[] = { 
 { 1,  "bar_wipe_lr", "A bar moves from left to right", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_bar_wipe_lr_update },
 { 2,  "bar_wipe_tb", "A bar moves from top to bottom", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_bar_wipe_tb_update },
 { 3,  "box_wipe_tl", "A box expands from the upper-left corner to the lower-right corner", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 4,  "box_wipe_tr", "A box expands from the upper-right corner to the lower-left corner", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 5,  "box_wipe_br", "A box expands from the lower-right corner to the upper-left corner", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 6,  "box_wipe_bl", "A box expands from the lower-left corner to the upper-right corner", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 23, "box_wipe_tc", "A box expands from the top edge's midpoint to the bottom corners", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 24, "box_wipe_rc", "A box expands from the right edge's midpoint to the left corners", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 25, "box_wipe_bc", "A box expands from the bottom edge's midpoint to the top corners", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 26, "box_wipe_lc", "A box expands from the left edge's midpoint to the right corners", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_box_wipe_update },
 { 7 , "four_box_wipe_ci", "A box shape expands from each of the four corners toward the center", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_fourc_box_wipe_update },
 { 8 , "four_box_wipe_co", "A box shape expands from the center of each quadrant toward the corners of each quadrant", 
	 		_gst_mask_default_new, _gst_mask_default_destroy, gst_fourc_box_wipe_update },
 { 0, NULL, NULL, NULL }
};

void
_gst_barboxwipes_register (void)
{
  gint i = 0;

  while (definitions[i].short_name) {
    _gst_mask_register (&definitions[i]);
    i++;
  }
}

