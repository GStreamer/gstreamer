/* Copyright (C) 2007 Brian Koropoff <bkoropoff at gmail com>
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

#include <glib.h>

typedef struct
{
	gchar *title, *game, *artist, *album, *publisher;
	gchar *dumper, *comment;
	enum { EMU_SNES9X = 2, EMU_ZSNES = 1, EMU_UNKNOWN = 0 } emulator;
	guint8 track, disc, muted, loop_count;
	guint16 year;
	guint32 time_seconds, time_fade_milliseconds;
	guint32 time_intro, time_loop, time_end, time_fade;
	guint32 amplification;
	GDate *dump_date;
} spc_tag_info;

void spc_tag_clear(spc_tag_info* info);
void spc_tag_get_info(guchar* data, guint length, spc_tag_info* info);
void spc_tag_free(spc_tag_info* info);
