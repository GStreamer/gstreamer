/**
 * Copyright (c) 2002 Billy Biggs <vektor@dumbterm.net>.
 * Copyright (c) 2002 Doug Bell <drbell@users.sourceforge.net>.
 *
 * Modified and adapted to GStreamer by
 * David I. Lehn <dlehn@users.sourceforge.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef HAVE_VBISCREEN_H
#define HAVE_VBISCREEN_H

typedef struct vbiscreen_s vbiscreen_t;

vbiscreen_t *vbiscreen_new( int video_width, int video_height, 
                            double video_aspect, int verbose, void *user_data );
void vbiscreen_delete( vbiscreen_t *vs );
void vbiscreen_set_verbose( vbiscreen_t *vs, int verbose );
void vbiscreen_set_mode( vbiscreen_t *vs, int caption, int style);
void vbiscreen_new_caption( vbiscreen_t *vs, int indent, int ital,
                            unsigned int colour, int row );
void vbiscreen_tab( vbiscreen_t *vs, int cols );
void vbiscreen_delete_to_end( vbiscreen_t *vs );
void vbiscreen_backspace( vbiscreen_t *vs );
void vbiscreen_erase_displayed( vbiscreen_t *vs );
void vbiscreen_erase_non_displayed( vbiscreen_t *vs );
void vbiscreen_carriage_return( vbiscreen_t *vs );
void vbiscreen_end_of_caption( vbiscreen_t *vs );
void vbiscreen_print( vbiscreen_t *vs, char c1, char c2 );
void vbiscreen_composite_packed422_scanline( vbiscreen_t *vs,
                                             unsigned char *output,
                                             int width, int xpos, 
                                             int scanline );
void vbiscreen_dump_screen_text( vbiscreen_t *vs );
void vbiscreen_reset( vbiscreen_t *vs );

#endif
