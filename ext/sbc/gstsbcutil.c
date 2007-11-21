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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "ipc.h"
#include "gstsbcutil.h"

/*
 * Selects one rate from a list of possible rates
 * TODO - use a better approach to this (it is selecting the last element)
 */
gint
gst_sbc_select_rate_from_list (const GValue * value)
{
  guint size = gst_value_list_get_size (value);
  return g_value_get_int (gst_value_list_get_value (value, size - 1));
}

/*
 * Selects one number of channels option from a range of possible numbers
 * TODO - use a better approach to this (it is selecting the maximum value)
 */
gint
gst_sbc_select_channels_from_range (const GValue * value)
{
  return gst_value_get_int_range_max (value);
}

/*
 * Selects one number of blocks from a list of possible blocks
 * TODO - use a better approach to this (it is selecting the last element)
 */
gint
gst_sbc_select_blocks_from_list (const GValue * value)
{
  guint size = gst_value_list_get_size (value);
  return g_value_get_int (gst_value_list_get_value (value, size - 1));
}

/*
 * Selects one number of subbands from a list
 * TODO - use a better approach to this (it is selecting the last element)
 */
gint
gst_sbc_select_subbands_from_list (const GValue * value)
{
  guint size = gst_value_list_get_size (value);
  return g_value_get_int (gst_value_list_get_value (value, size - 1));
}

/*
 * Selects one bitpool option from a range
 * TODO - use a better approach to this (it is selecting the maximum value)
 */
gint
gst_sbc_select_bitpool_from_range (const GValue * value)
{
  return gst_value_get_int_range_max (value);
}

/*
 * Selects one allocation mode from the ones on the list
 * TODO - use a better approach
 */
const gchar *
gst_sbc_get_allocation_from_list (const GValue * value)
{
  guint size = gst_value_list_get_size (value);
  return g_value_get_string (gst_value_list_get_value (value, size - 1));
}

/*
 * Selects one mode from the ones on the list
 * TODO - use a better aproach
 */
const gchar *
gst_sbc_get_mode_from_list (const GValue * value)
{
  guint size = gst_value_list_get_size (value);
  return g_value_get_string (gst_value_list_get_value (value, size - 1));
}

gint
gst_sbc_get_allocation_mode_int (const gchar * allocation)
{
  if (g_ascii_strcasecmp (allocation, "loudness") == 0)
    return BT_A2DP_ALLOCATION_LOUDNESS;
  else if (g_ascii_strcasecmp (allocation, "snr") == 0)
    return BT_A2DP_ALLOCATION_SNR;
  else if (g_ascii_strcasecmp (allocation, "auto") == 0)
    return BT_A2DP_ALLOCATION_AUTO;
  else
    return -1;
}

gint
gst_sbc_get_mode_int (const gchar * mode)
{
  if (g_ascii_strcasecmp (mode, "joint") == 0)
    return BT_A2DP_CHANNEL_MODE_JOINT_STEREO;
  else if (g_ascii_strcasecmp (mode, "stereo") == 0)
    return BT_A2DP_CHANNEL_MODE_STEREO;
  else if (g_ascii_strcasecmp (mode, "dual") == 0)
    return BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL;
  else if (g_ascii_strcasecmp (mode, "mono") == 0)
    return BT_A2DP_CHANNEL_MODE_MONO;
  else if (g_ascii_strcasecmp (mode, "auto") == 0)
    return BT_A2DP_CHANNEL_MODE_AUTO;
  else
    return -1;
}

const gchar *
gst_sbc_get_mode_string (int joint)
{
  switch (joint) {
    case BT_A2DP_CHANNEL_MODE_MONO:
      return "mono";
    case BT_A2DP_CHANNEL_MODE_DUAL_CHANNEL:
      return "dual";
    case BT_A2DP_CHANNEL_MODE_STEREO:
      return "stereo";
    case BT_A2DP_CHANNEL_MODE_JOINT_STEREO:
      return "joint";
    case BT_A2DP_CHANNEL_MODE_AUTO:
      return NULL;              /* TODO what should be selected here? */
    default:
      return NULL;
  }
}

const gchar *
gst_sbc_get_allocation_string (int alloc)
{
  switch (alloc) {
    case BT_A2DP_ALLOCATION_LOUDNESS:
      return "loudness";
    case BT_A2DP_ALLOCATION_SNR:
      return "snr";
    case BT_A2DP_ALLOCATION_AUTO:
      return "loudness";        /* TODO what should be selected here? */
    default:
      return NULL;
  }
}

GstCaps *
gst_sbc_caps_from_sbc (sbc_capabilities_t * sbc, gint channels)
{
  GstCaps *caps;
  const gchar *mode_str;
  const gchar *allocation_str;

  mode_str = gst_sbc_get_mode_string (sbc->channel_mode);
  allocation_str = gst_sbc_get_allocation_string (sbc->allocation_method);

  caps = gst_caps_new_simple ("audio/x-sbc",
      "rate", G_TYPE_INT, sbc->frequency,
      "channels", G_TYPE_INT, channels,
      "mode", G_TYPE_STRING, mode_str,
      "subbands", G_TYPE_INT, sbc->subbands,
      "blocks", G_TYPE_INT, sbc->block_length,
      "allocation", G_TYPE_STRING, allocation_str,
      "bitpool", G_TYPE_INT, sbc->max_bitpool, NULL);

  return caps;
}

/*
 * Given a GstCaps, this will return a fixed GstCaps on sucessfull conversion.
 * If an error occurs, it will return NULL and error_message will contain the
 * error message.
 *
 * error_message must be passed NULL, if an error occurs, the caller has the
 * ownership of the error_message, it must be freed after use.
 */
GstCaps *
gst_sbc_util_caps_fixate (GstCaps * caps, gchar ** error_message)
{
  GstCaps *result;
  GstStructure *structure;
  const GValue *value;
  gboolean error = FALSE;
  gint temp, rate, channels, blocks, subbands, bitpool;
  const gchar *allocation = NULL;
  const gchar *mode = NULL;

  g_assert (*error_message == NULL);

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_has_field (structure, "rate")) {
    error = TRUE;
    *error_message = g_strdup ("no rate");
    goto error;
  } else {
    value = gst_structure_get_value (structure, "rate");
    if (GST_VALUE_HOLDS_LIST (value))
      temp = gst_sbc_select_rate_from_list (value);
    else
      temp = g_value_get_int (value);
    rate = temp;
  }

  if (!gst_structure_has_field (structure, "channels")) {
    error = TRUE;
    *error_message = g_strdup ("no channels");
    goto error;
  } else {
    value = gst_structure_get_value (structure, "channels");
    if (GST_VALUE_HOLDS_INT_RANGE (value))
      temp = gst_sbc_select_channels_from_range (value);
    else
      temp = g_value_get_int (value);
    channels = temp;
  }

  if (!gst_structure_has_field (structure, "blocks")) {
    error = TRUE;
    *error_message = g_strdup ("no blocks.");
    goto error;
  } else {
    value = gst_structure_get_value (structure, "blocks");
    if (GST_VALUE_HOLDS_LIST (value))
      temp = gst_sbc_select_blocks_from_list (value);
    else
      temp = g_value_get_int (value);
    blocks = temp;
  }

  if (!gst_structure_has_field (structure, "subbands")) {
    error = TRUE;
    *error_message = g_strdup ("no subbands");
    goto error;
  } else {
    value = gst_structure_get_value (structure, "subbands");
    if (GST_VALUE_HOLDS_LIST (value))
      temp = gst_sbc_select_subbands_from_list (value);
    else
      temp = g_value_get_int (value);
    subbands = temp;
  }

  if (!gst_structure_has_field (structure, "bitpool")) {
    error = TRUE;
    *error_message = g_strdup ("no bitpool");
    goto error;
  } else {
    value = gst_structure_get_value (structure, "bitpool");
    if (GST_VALUE_HOLDS_INT_RANGE (value))
      temp = gst_sbc_select_bitpool_from_range (value);
    else
      temp = g_value_get_int (value);
    bitpool = temp;
  }

  if (!gst_structure_has_field (structure, "allocation")) {
    error = TRUE;
    *error_message = g_strdup ("no allocation");
    goto error;
  } else {
    value = gst_structure_get_value (structure, "allocation");
    if (GST_VALUE_HOLDS_LIST (value))
      allocation = gst_sbc_get_allocation_from_list (value);
    else
      allocation = g_value_get_string (value);
  }

  if (!gst_structure_has_field (structure, "mode")) {
    error = TRUE;
    *error_message = g_strdup ("no mode");
    goto error;
  } else {
    value = gst_structure_get_value (structure, "mode");
    if (GST_VALUE_HOLDS_LIST (value))
      mode = gst_sbc_get_mode_from_list (value);
    else
      mode = g_value_get_string (value);
  }

error:
  if (error)
    return NULL;

  result = gst_caps_new_simple ("audio/x-sbc",
      "rate", G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels,
      "mode", G_TYPE_STRING, mode,
      "blocks", G_TYPE_INT, blocks,
      "subbands", G_TYPE_INT, subbands,
      "allocation", G_TYPE_STRING, allocation,
      "bitpool", G_TYPE_INT, bitpool, NULL);

  return result;
}
