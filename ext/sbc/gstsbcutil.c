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


/* common functions for gstreamer sbc related plugins */


#include "gstsbcutil.h"
#include "ipc.h"

/*
 * Selects one rate from a list of possible rates
 * TODO - use a better approach to this (it is selecting the last element)
 */
gint gst_sbc_select_rate_from_list(const GValue *value)
{
	guint size = gst_value_list_get_size(value);
	return g_value_get_int(gst_value_list_get_value(value, size-1));
}

/*
 * Selects one rate from a range of possible rates
 * TODO - use a better approach to this (it is selecting the maximum value)
 */
gint gst_sbc_select_rate_from_range(const GValue *value)
{
	return gst_value_get_int_range_max(value);
}

/*
 * Selects one number of channels from a list of possible numbers
 * TODO - use a better approach to this (it is selecting the last element)
 */
gint gst_sbc_select_channels_from_list(const GValue *value)
{
	guint size = gst_value_list_get_size(value);
	return g_value_get_int(gst_value_list_get_value(value, size-1));
}

/*
 * Selects one number of channels option from a range of possible numbers
 * TODO - use a better approach to this (it is selecting the maximum value)
 */
gint gst_sbc_select_channels_from_range(const GValue *value)
{
	return gst_value_get_int_range_max(value);
}

/*
 * Selects one number of blocks from a list of possible blocks
 * TODO - use a better approach to this (it is selecting the last element)
 */
gint gst_sbc_select_blocks_from_list(const GValue *value)
{
	guint size = gst_value_list_get_size(value);
	return g_value_get_int(gst_value_list_get_value(value, size-1));
}

/*
 * Selects one blocks option from a range of possible blocks
 * TODO - use a better approach to this (it is selecting the maximum value)
 */
gint gst_sbc_select_blocks_from_range(const GValue *value)
{
	return gst_value_get_int_range_max(value);
}

/*
 * Selects one number of subbands from a list
 * TODO - use a better approach to this (it is selecting the last element)
 */
gint gst_sbc_select_subbands_from_list(const GValue *value)
{
	guint size = gst_value_list_get_size(value);
	return g_value_get_int(gst_value_list_get_value(value, size-1));
}

/*
 * Selects one subbands option from a range
 * TODO - use a better approach to this (it is selecting the maximum value)
 */
gint gst_sbc_select_subbands_from_range(const GValue *value)
{
	return gst_value_get_int_range_max(value);
}

/*
 * Selects one allocation mode from the ones on the list
 * TODO - use a better approach
 */
const gchar* gst_sbc_get_allocation_from_list(const GValue *value)
{
	guint size = gst_value_list_get_size(value);
	return g_value_get_string(gst_value_list_get_value(value, size-1));
}

/*
 * Selects one mode from the ones on the list
 * TODO - use a better aproach
 */
const gchar* gst_sbc_get_mode_from_list(const GValue *value)
{
	guint size = gst_value_list_get_size(value);
	return g_value_get_string(gst_value_list_get_value(value, size-1));
}

gint gst_sbc_get_allocation_mode_int(const gchar* allocation)
{
	if (g_ascii_strcasecmp(allocation, "loudness") == 0)
		return CFG_ALLOCATION_LOUDNESS;
	else if (g_ascii_strcasecmp(allocation, "snr") == 0)
		return CFG_ALLOCATION_SNR;
	else if (g_ascii_strcasecmp(allocation, "auto") == 0)
		return CFG_ALLOCATION_AUTO;
	else
		return -1;
}

gint gst_sbc_get_mode_int(const gchar* mode)
{
	if (g_ascii_strcasecmp(mode, "joint") == 0)
		return CFG_MODE_JOINT_STEREO;
	else if (g_ascii_strcasecmp(mode, "stereo") == 0)
		return CFG_MODE_STEREO;
	else if (g_ascii_strcasecmp(mode, "dual") == 0)
		return CFG_MODE_DUAL_CHANNEL;
	else if (g_ascii_strcasecmp(mode, "mono") == 0)
		return CFG_MODE_MONO;
	else if (g_ascii_strcasecmp(mode, "auto") == 0)
		return CFG_MODE_AUTO;
	else
		return -1;
}

const gchar* gst_sbc_get_mode_string(int joint)
{
	switch (joint) {
	case CFG_MODE_MONO:
		return "mono";
	case CFG_MODE_DUAL_CHANNEL:
		return "dual";
	case CFG_MODE_STEREO:
		return "stereo";
	case CFG_MODE_JOINT_STEREO:
		return "joint";
	case CFG_MODE_AUTO:
		return NULL; /* TODO what should be selected here? */
	default:
		return NULL;
	}
}

const gchar* gst_sbc_get_allocation_string(int alloc)
{
	switch (alloc) {
	case CFG_ALLOCATION_LOUDNESS:
		return "loudness";
	case CFG_ALLOCATION_SNR:
		return "snr";
	case CFG_ALLOCATION_AUTO:
		return NULL; /* TODO what should be selected here? */
	default:
		return NULL;
	}
}
