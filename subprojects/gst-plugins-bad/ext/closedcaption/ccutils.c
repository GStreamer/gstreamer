/*
 * GStreamer
 * Copyright (C) 2022 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "ccutils.h"

static const struct cdp_fps_entry cdp_fps_table[] = {
  {0x1f, 24000, 1001, 25, 22, 3 /* FIXME: alternating max cea608 count! */ },
  {0x2f, 24, 1, 25, 22, 2},
  {0x3f, 25, 1, 24, 22, 2},
  {0x4f, 30000, 1001, 20, 18, 2},
  {0x5f, 30, 1, 20, 18, 2},
  {0x6f, 50, 1, 12, 11, 1},
  {0x7f, 60000, 1001, 10, 9, 1},
  {0x8f, 60, 1, 10, 9, 1},
};
const struct cdp_fps_entry null_fps_entry = { 0, 0, 0, 0 };

const struct cdp_fps_entry *
cdp_fps_entry_from_fps (guint fps_n, guint fps_d)
{
  int i;
  for (i = 0; i < G_N_ELEMENTS (cdp_fps_table); i++) {
    if (cdp_fps_table[i].fps_n == fps_n && cdp_fps_table[i].fps_d == fps_d)
      return &cdp_fps_table[i];
  }
  return &null_fps_entry;
}

const struct cdp_fps_entry *
cdp_fps_entry_from_id (guint8 id)
{
  int i;
  for (i = 0; i < G_N_ELEMENTS (cdp_fps_table); i++) {
    if (cdp_fps_table[i].fps_idx == id)
      return &cdp_fps_table[i];
  }
  return &null_fps_entry;
}
