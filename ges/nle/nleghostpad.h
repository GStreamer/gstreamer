/* GStreamer
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
 *
 * nleghostpad.h: Header for helper ghostpad
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


#ifndef __NLE_GHOSTPAD_H__
#define __NLE_GHOSTPAD_H__

#include <gst/gst.h>

#include "nletypes.h"

G_BEGIN_DECLS

GstPad *nle_object_ghost_pad (NleObject * object,
    const gchar * name, GstPad * target);

GstPad *nle_object_ghost_pad_no_target (NleObject * object,
    const gchar * name, GstPadDirection dir, GstPadTemplate *templ);

gboolean nle_object_ghost_pad_set_target (NleObject * object,
    GstPad * ghost, GstPad * target);

void nle_object_remove_ghost_pad (NleObject * object, GstPad * ghost);
GstEvent * nle_object_translate_incoming_seek (NleObject * object, GstEvent * event);

void nle_init_ghostpad_category (void);

G_END_DECLS

#endif /* __NLE_GHOSTPAD_H__ */
