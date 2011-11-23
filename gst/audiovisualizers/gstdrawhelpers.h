/* GStreamer
 * Copyright (C) <2011> Stefan Sauer <ensonic@users.sf.net>
 *
 * gstdrawhelpers.h: simple drawing helpers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
 
#define draw_dot(_vd, _x, _y, _st, _c) G_STMT_START {                          \
  _vd[(_y * _st) + _x] = _c;                                                   \
} G_STMT_END

#define draw_line(_vd, _x1, _x2, _y1, _y2, _st, _c) G_STMT_START {             \
  guint _i, _j, _x, _y;                                                        \
  gint _dx = _x2 - _x1, _dy = _y2 - _y1;                                       \
  gfloat _f;                                                                   \
                                                                               \
  _j = abs (_dx) > abs (_dy) ? abs (_dx) : abs (_dy);                          \
  for (_i = 0; _i < _j; _i++) {                                                \
    _f = (gfloat) _i / (gfloat) _j;                                            \
    _x = _x1 + _dx * _f;                                                       \
    _y = _y1 + _dy * _f;                                                       \
    draw_dot (_vd, _x, _y, _st, _c);                                           \
  }                                                                            \
} G_STMT_END
