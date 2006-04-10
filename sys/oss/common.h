/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000,2005 Wim Taymans <wim@fluendo.com>
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

#define SET_PARAM(_oss, _name, _val, _detail)   \
G_STMT_START {                                  \
  int _tmp = _val;                              \
  if (ioctl(_oss->fd, _name, &_tmp) == -1) {    \
    GST_ELEMENT_ERROR (_oss, RESOURCE, SETTINGS,\
        (NULL),					\
        ("Unable to set param " _detail ": %s", \
                   g_strerror (errno)));        \
    return FALSE;                               \
  }                                             \
  GST_DEBUG_OBJECT(_oss, _detail " %d", _tmp);  \
} G_STMT_END

#define GET_PARAM(_oss, _name, _val, _detail)   \
G_STMT_START {                                  \
  if (ioctl(oss->fd, _name, _val) == -1) {      \
    GST_ELEMENT_ERROR (oss, RESOURCE, SETTINGS, \
        (NULL),					\
        ("Unable to get param " _detail ": %s", \
                   g_strerror (errno)));        \
    return FALSE;                               \
  }                                             \
} G_STMT_END
