/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2004-2010  Marcel Holtmann <marcel@holtmann.org>
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
#include <string.h>

#define SBC_AM_AUTO 0x02
#define SBC_MODE_AUTO 0x04

gint gst_sbc_select_rate_from_list(const GValue *value);

gint gst_sbc_select_channels_from_range(const GValue *value);

gint gst_sbc_select_blocks_from_list(const GValue *value);

gint gst_sbc_select_subbands_from_list(const GValue *value);

gint gst_sbc_select_bitpool_from_range(const GValue *value);

const gchar *gst_sbc_get_allocation_from_list(const GValue *value);

const gchar *gst_sbc_get_mode_from_list(const GValue *value, gint channels);

gint gst_sbc_get_channel_number(gint mode);
gint gst_sbc_parse_rate_from_sbc(gint frequency);
gint gst_sbc_parse_rate_to_sbc(gint rate);

gint gst_sbc_parse_subbands_from_sbc(gint subbands);
gint gst_sbc_parse_subbands_to_sbc(gint subbands);

gint gst_sbc_parse_blocks_from_sbc(gint blocks);
gint gst_sbc_parse_blocks_to_sbc(gint blocks);

const gchar *gst_sbc_parse_mode_from_sbc(gint mode);
gint gst_sbc_parse_mode_to_sbc(const gchar *mode);

const gchar *gst_sbc_parse_allocation_from_sbc(gint alloc);
gint gst_sbc_parse_allocation_to_sbc(const gchar *allocation);

GstCaps* gst_sbc_parse_caps_from_sbc(sbc_t *sbc);

GstCaps* gst_sbc_util_caps_fixate(GstCaps *caps, gchar** error_message);

void gst_sbc_util_set_structure_int_param(GstStructure *structure,
			const gchar* field, gint field_value,
			GValue *value);

void gst_sbc_util_set_structure_string_param(GstStructure *structure,
			const gchar* field, const gchar* field_value,
			GValue *value);

gboolean gst_sbc_util_fill_sbc_params(sbc_t *sbc, GstCaps *caps);

