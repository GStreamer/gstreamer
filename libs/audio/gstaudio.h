/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Library       <2001> Thomas Vander Stichele <thomas@apestaart.org>
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
 * this library defines and implements some helper functions for audio
 * handling
 */

#include <gst/gst.h>

/* get byte size of audio frame (based on caps of pad */
int		gst_audio_frame_byte_size 	(GstPad* pad);

/* get length in frames of buffer */
long		gst_audio_frame_length 		(GstPad* pad, GstBuffer* buf);

/* get frame rate based on caps */
long		gst_audio_frame_rate		(GstPad *pad);

/* calculate length in seconds of audio buffer buf based on caps of pad */
double 		gst_audio_length 		(GstPad* pad, GstBuffer* buf);

