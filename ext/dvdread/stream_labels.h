/* GStreamer
 * Copyright (C) <2005> Stephane Loeuillet <stephane.loeuillet@tiscali.fr>
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

#include <dvdread/ifo_types.h>

#include "dvdreadsrc.h"

GHashTable * dvdreadsrc_init_languagelist (void);
void dvdreadsrc_get_audio_stream_labels (ifo_handle_t *vts_file, GHashTable * languagelist);
void dvdreadsrc_get_subtitle_stream_labels (ifo_handle_t *vts_file, GHashTable * languagelist);
