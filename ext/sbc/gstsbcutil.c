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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>
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
 */
const gchar *
gst_sbc_get_mode_from_list (const GValue * list, gint channels)
{
  unsigned int i;
  const GValue *value;
  const gchar *aux;
  gboolean joint, stereo, dual, mono;
  guint size = gst_value_list_get_size (list);

  joint = stereo = dual = mono = FALSE;

  for (i = 0; i < size; i++) {
    value = gst_value_list_get_value (list, i);
    aux = g_value_get_string (value);
    if (strcmp ("joint", aux) == 0)
      joint = TRUE;
    else if (strcmp ("stereo", aux) == 0)
      stereo = TRUE;
    else if (strcmp ("dual", aux) == 0)
      dual = TRUE;
    else if (strcmp ("mono", aux) == 0)
      mono = TRUE;
  }

  if (channels == 1 && mono)
    return "mono";
  else if (channels == 2) {
    if (joint)
      return "joint";
    else if (stereo)
      return "stereo";
    else if (dual)
      return "dual";
  }

  return NULL;
}

gint
gst_sbc_parse_rate_from_sbc (gint frequency)
{
  switch (frequency) {
    case SBC_FREQ_16000:
      return 16000;
    case SBC_FREQ_32000:
      return 32000;
    case SBC_FREQ_44100:
      return 44100;
    case SBC_FREQ_48000:
      return 48000;
    default:
      return 0;
  }
}

gint
gst_sbc_parse_rate_to_sbc (gint rate)
{
  switch (rate) {
    case 16000:
      return SBC_FREQ_16000;
    case 32000:
      return SBC_FREQ_32000;
    case 44100:
      return SBC_FREQ_44100;
    case 48000:
      return SBC_FREQ_48000;
    default:
      return -1;
  }
}

gint
gst_sbc_get_channel_number (gint mode)
{
  switch (mode) {
    case SBC_MODE_JOINT_STEREO:
    case SBC_MODE_STEREO:
    case SBC_MODE_DUAL_CHANNEL:
      return 2;
    case SBC_MODE_MONO:
      return 1;
    default:
      return 0;
  }
}

gint
gst_sbc_parse_subbands_from_sbc (gint subbands)
{
  switch (subbands) {
    case SBC_SB_4:
      return 4;
    case SBC_SB_8:
      return 8;
    default:
      return 0;
  }
}

gint
gst_sbc_parse_subbands_to_sbc (gint subbands)
{
  switch (subbands) {
    case 4:
      return SBC_SB_4;
    case 8:
      return SBC_SB_8;
    default:
      return -1;
  }
}

gint
gst_sbc_parse_blocks_from_sbc (gint blocks)
{
  switch (blocks) {
    case SBC_BLK_4:
      return 4;
    case SBC_BLK_8:
      return 8;
    case SBC_BLK_12:
      return 12;
    case SBC_BLK_16:
      return 16;
    default:
      return 0;
  }
}

gint
gst_sbc_parse_blocks_to_sbc (gint blocks)
{
  switch (blocks) {
    case 4:
      return SBC_BLK_4;
    case 8:
      return SBC_BLK_8;
    case 12:
      return SBC_BLK_12;
    case 16:
      return SBC_BLK_16;
    default:
      return -1;
  }
}

const gchar *
gst_sbc_parse_mode_from_sbc (gint mode)
{
  switch (mode) {
    case SBC_MODE_MONO:
      return "mono";
    case SBC_MODE_DUAL_CHANNEL:
      return "dual";
    case SBC_MODE_STEREO:
      return "stereo";
    case SBC_MODE_JOINT_STEREO:
    case SBC_MODE_AUTO:
      return "joint";
    default:
      return NULL;
  }
}

gint
gst_sbc_parse_mode_to_sbc (const gchar * mode)
{
  if (g_ascii_strcasecmp (mode, "joint") == 0)
    return SBC_MODE_JOINT_STEREO;
  else if (g_ascii_strcasecmp (mode, "stereo") == 0)
    return SBC_MODE_STEREO;
  else if (g_ascii_strcasecmp (mode, "dual") == 0)
    return SBC_MODE_DUAL_CHANNEL;
  else if (g_ascii_strcasecmp (mode, "mono") == 0)
    return SBC_MODE_MONO;
  else if (g_ascii_strcasecmp (mode, "auto") == 0)
    return SBC_MODE_JOINT_STEREO;
  else
    return -1;
}

const gchar *
gst_sbc_parse_allocation_from_sbc (gint alloc)
{
  switch (alloc) {
    case SBC_AM_LOUDNESS:
      return "loudness";
    case SBC_AM_SNR:
      return "snr";
    case SBC_AM_AUTO:
      return "loudness";
    default:
      return NULL;
  }
}

gint
gst_sbc_parse_allocation_to_sbc (const gchar * allocation)
{
  if (g_ascii_strcasecmp (allocation, "loudness") == 0)
    return SBC_AM_LOUDNESS;
  else if (g_ascii_strcasecmp (allocation, "snr") == 0)
    return SBC_AM_SNR;
  else
    return SBC_AM_LOUDNESS;
}

GstCaps *
gst_sbc_parse_caps_from_sbc (sbc_t * sbc)
{
  GstCaps *caps;
  const gchar *mode_str;
  const gchar *allocation_str;

  mode_str = gst_sbc_parse_mode_from_sbc (sbc->mode);
  allocation_str = gst_sbc_parse_allocation_from_sbc (sbc->allocation);
  caps = gst_caps_new_simple ("audio/x-sbc",
      "rate", G_TYPE_INT,
      gst_sbc_parse_rate_from_sbc (sbc->frequency),
      "channels", G_TYPE_INT,
      gst_sbc_get_channel_number (sbc->mode),
      "mode", G_TYPE_STRING, mode_str,
      "subbands", G_TYPE_INT,
      gst_sbc_parse_subbands_from_sbc (sbc->subbands),
      "blocks", G_TYPE_INT,
      gst_sbc_parse_blocks_from_sbc (sbc->blocks),
      "allocation", G_TYPE_STRING, allocation_str,
      "bitpool", G_TYPE_INT, sbc->bitpool, NULL);

  return caps;
}

/*
 * Given a GstCaps, this will return a fixed GstCaps on successful conversion.
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
    if (GST_VALUE_HOLDS_LIST (value)) {
      mode = gst_sbc_get_mode_from_list (value, channels);
    } else
      mode = g_value_get_string (value);
  }

  /* perform validation
   * if channels is 1, we must have channel mode = mono
   * if channels is 2, we can't have channel mode = mono */
  if ((channels == 1 && (strcmp (mode, "mono") != 0)) ||
      (channels == 2 && (strcmp (mode, "mono") == 0))) {
    *error_message = g_strdup_printf ("Invalid combination of "
        "channels (%d) and channel mode (%s)", channels, mode);
    error = TRUE;
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

/**
 * Sets the int field_value to the  param "field" on the structure.
 * value is used to do the operation, it must be a uninitialized (zero-filled)
 * GValue, it will be left unitialized at the end of the function.
 */
void
gst_sbc_util_set_structure_int_param (GstStructure * structure,
    const gchar * field, gint field_value, GValue * value)
{
  value = g_value_init (value, G_TYPE_INT);
  g_value_set_int (value, field_value);
  gst_structure_set_value (structure, field, value);
  g_value_unset (value);
}

/**
 * Sets the string field_value to the  param "field" on the structure.
 * value is used to do the operation, it must be a uninitialized (zero-filled)
 * GValue, it will be left unitialized at the end of the function.
 */
void
gst_sbc_util_set_structure_string_param (GstStructure * structure,
    const gchar * field, const gchar * field_value, GValue * value)
{
  value = g_value_init (value, G_TYPE_STRING);
  g_value_set_string (value, field_value);
  gst_structure_set_value (structure, field, value);
  g_value_unset (value);
}

gboolean
gst_sbc_util_fill_sbc_params (sbc_t * sbc, GstCaps * caps)
{
  GstStructure *structure;
  gint rate, channels, subbands, blocks, bitpool;
  const gchar *mode;
  const gchar *allocation;

  g_assert (gst_caps_is_fixed (caps));

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "rate", &rate))
    return FALSE;
  if (!gst_structure_get_int (structure, "channels", &channels))
    return FALSE;
  if (!gst_structure_get_int (structure, "subbands", &subbands))
    return FALSE;
  if (!gst_structure_get_int (structure, "blocks", &blocks))
    return FALSE;
  if (!gst_structure_get_int (structure, "bitpool", &bitpool))
    return FALSE;

  if (!(mode = gst_structure_get_string (structure, "mode")))
    return FALSE;
  if (!(allocation = gst_structure_get_string (structure, "allocation")))
    return FALSE;

  if (channels == 1 && strcmp (mode, "mono") != 0)
    return FALSE;

  sbc->frequency = gst_sbc_parse_rate_to_sbc (rate);
  sbc->blocks = gst_sbc_parse_blocks_to_sbc (blocks);
  sbc->subbands = gst_sbc_parse_subbands_to_sbc (subbands);
  sbc->bitpool = bitpool;
  sbc->mode = gst_sbc_parse_mode_to_sbc (mode);
  sbc->allocation = gst_sbc_parse_allocation_to_sbc (allocation);

  return TRUE;
}
