/* GStreamer
 * Copyright (C) <2012> Wim Taymans <wim.taymans@gmail.com>
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

/**
 * SECTION:gstrtpexthdr
 * @short_description: Helper methods for dealing with RTP header extensions
 * @see_also: #GstRTPBasePayload, #GstRTPBaseDepayload, gstrtpbuffer
 *
 * <refsect2>
 * <para>
 * </para>
 * </refsect2>
 */

#include "gstrtphdrext.h"

#include <stdlib.h>
#include <string.h>

/**
 * gst_rtp_hdrext_set_ntp_64:
 * @data: the data to write to
 * @size: the size of @data
 * @ntptime: the NTP time
 *
 * Writes the NTP time in @ntptime to the format required for the NTP-64 header
 * extension. @data must hold at least #GST_RTP_HDREXT_NTP_64_SIZE bytes.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtp_hdrext_set_ntp_64 (gpointer data, guint size, guint64 ntptime)
{
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size >= GST_RTP_HDREXT_NTP_64_SIZE, FALSE);

  GST_WRITE_UINT64_BE (data, ntptime);

  return TRUE;
}

/**
 * gst_rtp_hdrext_get_ntp_64:
 * @data: the data to read from
 * @size: the size of @data
 * @ntptime: the result NTP time
 *
 * Reads the NTP time from the @size NTP-64 extension bytes in @data and store the
 * result in @ntptime.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtp_hdrext_get_ntp_64 (gpointer data, guint size, guint64 * ntptime)
{
  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size >= GST_RTP_HDREXT_NTP_64_SIZE, FALSE);

  if (ntptime)
    *ntptime = GST_READ_UINT64_BE (data);

  return TRUE;
}

/**
 * gst_rtp_hdrext_set_ntp_56:
 * @data: the data to write to
 * @size: the size of @data
 * @ntptime: the NTP time
 *
 * Writes the NTP time in @ntptime to the format required for the NTP-56 header
 * extension. @data must hold at least #GST_RTP_HDREXT_NTP_56_SIZE bytes.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtp_hdrext_set_ntp_56 (gpointer data, guint size, guint64 ntptime)
{
  guint8 *d = data;
  gint i;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size >= GST_RTP_HDREXT_NTP_56_SIZE, FALSE);

  for (i = 0; i < 7; i++) {
    d[6 - i] = ntptime & 0xff;
    ntptime >>= 8;
  }
  return TRUE;
}

/**
 * gst_rtp_hdrext_get_ntp_56:
 * @data: the data to read from
 * @size: the size of @data
 * @ntptime: the result NTP time
 *
 * Reads the NTP time from the @size NTP-56 extension bytes in @data and store the
 * result in @ntptime.
 *
 * Returns: %TRUE on success.
 */
gboolean
gst_rtp_hdrext_get_ntp_56 (gpointer data, guint size, guint64 * ntptime)
{
  guint8 *d = data;

  g_return_val_if_fail (data != NULL, FALSE);
  g_return_val_if_fail (size >= GST_RTP_HDREXT_NTP_56_SIZE, FALSE);

  if (ntptime) {
    gint i;

    *ntptime = 0;
    for (i = 0; i < 7; i++) {
      *ntptime <<= 8;
      *ntptime |= d[i];
    }
  }
  return TRUE;
}
