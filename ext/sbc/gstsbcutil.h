/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2007  Marcel Holtmann <marcel@holtmann.org>
 *
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <gst/gst.h>

#include "sbc.h"
#include "ipc.h"

gint gst_sbc_select_rate_from_list(const GValue *value);

gint gst_sbc_select_channels_from_range(const GValue *value);

gint gst_sbc_select_blocks_from_list(const GValue *value);

gint gst_sbc_select_subbands_from_list(const GValue *value);

gint gst_sbc_select_bitpool_from_range(const GValue *value);

gint gst_sbc_select_bitpool_from_range(const GValue *value);

const gchar *gst_sbc_get_allocation_from_list(const GValue *value);
gint gst_sbc_get_allocation_mode_int(const gchar *allocation);
const gchar *gst_sbc_get_allocation_string(int alloc);

const gchar *gst_sbc_get_mode_from_list(const GValue *value);
gint gst_sbc_get_mode_int(const gchar *mode);
const gchar *gst_sbc_get_mode_string(int joint);

GstCaps* gst_sbc_caps_from_sbc(sbc_capabilities_t *sbc, gint channels);

GstCaps* gst_sbc_util_caps_fixate(GstCaps *caps, gchar** error_message);
