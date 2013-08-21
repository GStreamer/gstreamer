/*
 * gstmpegtsdescriptor.c - 
 * Copyright (C) 2013 Edward Hervey
 * 
 * Authors:
 *   Edward Hervey <edward@collabora.com>
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
#include <stdlib.h>
#include <string.h>

#include "mpegts.h"
#include "gstmpegts-private.h"


/**
 * SECTION:gst-dvb-descriptor
 * @title: DVB variants of MPEG-TS descriptors
 * @short_description: Descriptors for the various DVB specifications
 * @include: gst/mpegts/mpegts.h
 *
 */

/*
 * TODO
 *
 * * Add common validation code for data presence and minimum/maximum expected
 *   size.
 * * Add parsing methods for the following descriptors that were previously 
 *   handled in mpegtsbase:
 *   * GST_MTS_DESC_DVB_DATA_BROADCAST_ID
 *   * GST_MTS_DESC_DVB_DATA_BROADCAST
 *   * GST_MTS_DESC_DVB_CAROUSEL_IDENTIFIER
 *   * GST_MTS_DESC_DVB_STREAM_IDENTIFIER
 *   * GST_MTS_DESC_DVB_EXTENDED_EVENT
 *   * GST_MTS_DESC_DVB_COMPONENT
 *   * GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM
 *   * GST_MTS_DESC_DVB_FREQUENCY_LIST
 */


/* GST_MTS_DESC_DVB_NETWORK_NAME (0x40) */
/**
 * gst_mpegts_descriptor_parse_dvb_network_name:
 * @descriptor: a %GST_MTS_DESC_DVB_NETWORK_NAME #GstMpegTsDescriptor
 * @name: (out) (transfer full): the extracted name
 *
 * Parses out the dvb network name from the @descriptor:
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_network_name (const GstMpegTsDescriptor *
    descriptor, gchar ** name)
{
  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0x40, FALSE);

  *name = get_encoding_and_convert ((gchar *) descriptor->data + 2,
      descriptor->data[1]);
  return TRUE;
}

/* GST_MTS_DESC_DVB_SATELLITE_DELIVERY_SYSTEM (0x43) */
/**
 * gst_mpegts_descriptor_parse_satellite_delivery_system:
 * @descriptor: a %GST_MTS_DESC_DVB_SATELLITE_DELIVERY_SYSTEM #GstMpegTsDescriptor
 * @res: (out) (transfer none): the #GstMpegTsSatelliteDeliverySystemDescriptor to fill
 *
 * Extracts the satellite delivery system information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_satellite_delivery_system (const GstMpegTsDescriptor
    * descriptor, GstMpegTsSatelliteDeliverySystemDescriptor * res)
{
  guint8 *data;
  guint8 tmp;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0x43, FALSE);

  data = (guint8 *) descriptor->data + 2;

#define BCD_UN(a) ((a) & 0x0f)
#define BCD_DEC(a) (((a) >> 4) & 0x0f)
#define BCD(a) (BCD_UN(a) + 10 * BCD_DEC(a))
#define BCD_16(a) (BCD(a[1]) + 100 * BCD(a[0]))
#define BCD_28(a) (BCD_DEC(a[3]) + 10 * BCD(a[2]) + 1000 * BCD(a[1]) + 100000 * BCD(a[0]))
#define BCD_32(a) (BCD(a[3]) + 100 * BCD(a[2]) + 10000 * BCD(a[1]) + 1000000 * BCD(a[0]))

  /* BCD coded frequency in GHz (decimal point occurs after the 3rd character)
   * So direct BCD gives us units of (GHz / 100 000) = 10 kHz*/
  res->frequency = BCD_32 (data) * 10;
  data += 4;
  /* BCD codec position in degrees (float pointer after the 3rd character) */
  res->orbital_position = (BCD_16 (data)) / 10.0;
  data += 2;

  tmp = *data;
  res->west_east = (tmp & 0x80) == 0x80;
  res->polarization = (tmp >> 7) & 0x03;
  res->modulation_system = (tmp & 0x04) == 0x04;
  if (res->modulation_system)
    res->roll_off = (tmp >> 3 & 0x03);
  else
    res->roll_off = GST_MPEGTS_ROLLOFF_AUTO;
  switch (tmp & 0x03) {
    case 0x00:
      res->modulation_type = GST_MPEGTS_MODULATION_QAM_AUTO;
      break;
    case 0x01:
      res->modulation_type = GST_MPEGTS_MODULATION_QPSK;
      break;
    case 0x10:
      res->modulation_type = GST_MPEGTS_MODULATION_PSK_8;
      break;
    case 0x11:
      res->modulation_type = GST_MPEGTS_MODULATION_QAM_16;
      break;
    default:
      break;
  }
  res->modulation_type = tmp & 0x03;
  data += 1;
  /* symbol_rate is in Msymbols/ (decimal point occurs after 3rd character) */
  /* So direct BCD gives us units of (Msymbol / 10 000) = 100 sym/s */
  res->symbol_rate = BCD_28 (data) * 100;
  data += 3;
  /* fec_inner */
  res->fec_inner = *data >> 4;


  return TRUE;
}


/* GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM (0x44) */
/**
 * gst_mpegts_descriptor_parse_cable_delivery_system:
 * @descriptor: a %GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM #GstMpegTsDescriptor
 * @res: (out) (transfer none): the #GstMpegTsCableDeliverySystemDescriptor to fill
 *
 * Extracts the cable delivery system information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_cable_delivery_system (const GstMpegTsDescriptor *
    descriptor, GstMpegTsCableDeliverySystemDescriptor * res)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (res != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0x44, FALSE);

  data = (guint8 *) descriptor->data + 2;
  /* BCD in MHz, decimal place after the fourth character */
  res->frequency = BCD_32 (data) * 100;
  data += 5;
  /* fec_out (4bits) */
  res->outer_fec = *data++ & 0x0f;
  switch (*data) {
    case 0x00:
      res->modulation = GST_MPEGTS_MODULATION_NONE;
      break;
    case 0x01:
      res->modulation = GST_MPEGTS_MODULATION_QAM_16;
      break;
    case 0x02:
      res->modulation = GST_MPEGTS_MODULATION_QAM_32;
      break;
    case 0x03:
      res->modulation = GST_MPEGTS_MODULATION_QAM_64;
      break;
    case 0x04:
      res->modulation = GST_MPEGTS_MODULATION_QAM_128;
      break;
    case 0x05:
      res->modulation = GST_MPEGTS_MODULATION_QAM_256;
      break;
    default:
      GST_WARNING ("Unsupported cable modulation type: 0x%02x", *data);
      res->modulation = GST_MPEGTS_MODULATION_NONE;
      break;
  }

  data += 1;
  /* symbol_rate is in Msymbols/ (decimal point occurs after 3rd character) */
  /* So direct BCD gives us units of (Msymbol / 10 000) = 100 sym/s */
  res->symbol_rate = BCD_28 (data) * 100;
  data += 3;
  /* fec_inner */
  res->fec_inner = *data & 0x0f;

  return TRUE;
}

/* GST_MTS_DESC_DVB_SERVICE (0x48) */
/**
 * gst_mpegts_descriptor_parse_dvb_service:
 * @descriptor: a %GST_MTS_DESC_DVB_SERVICE #GstMpegTsDescriptor
 * @service_type: (out) (allow-none): the service type
 * @service_name: (out) (transfer full) (allow-none): the service name
 * @provider_name: (out) (transfer full) (allow-none): the provider name
 *
 * Extracts the dvb service information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_service (const GstMpegTsDescriptor *
    descriptor, GstMpegTsDVBServiceType * service_type, gchar ** service_name,
    gchar ** provider_name)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0x48, FALSE);

  data = (guint8 *) descriptor->data + 2;

  if (service_type)
    *service_type = *data;
  data += 1;
  if (provider_name)
    *provider_name = get_encoding_and_convert ((const gchar *) data + 1, *data);
  data += *data + 1;
  if (service_name)
    *service_name = get_encoding_and_convert ((const gchar *) data + 1, *data);

  return TRUE;
}

/* GST_MTS_DESC_DVB_SHORT_EVENT (0x4D) */
/**
 * gst_mpegts_descriptor_parse_dvb_short_event:
 * @descriptor: a %GST_MTS_DESC_DVB_SHORT_EVENT #GstMpegTsDescriptor
 * @language_code: (out) (transfer full) (allow-none): the language code
 * @event_name: (out) (transfer full) (allow-none): the event name
 * @text: (out) (transfer full) (allow-none): the event text
 *
 * Extracts the DVB short event information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_short_event (const GstMpegTsDescriptor *
    descriptor, gchar ** language_code, gchar ** event_name, gchar ** text)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && descriptor->data != NULL, FALSE);
  g_return_val_if_fail (descriptor->tag == 0x4D, FALSE);

  data = (guint8 *) descriptor->data + 2;

  if (language_code) {
    *language_code = g_malloc0 (4);
    memcpy (*language_code, data, 3);
  }
  data += 3;
  if (event_name)
    *event_name = get_encoding_and_convert ((const gchar *) data + 1, *data);
  data += *data + 1;
  if (text)
    *text = get_encoding_and_convert ((const gchar *) data + 1, *data);
  return TRUE;
}
