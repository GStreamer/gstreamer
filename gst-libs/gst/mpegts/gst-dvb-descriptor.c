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
 *   * GST_MTS_DESC_DVB_CAROUSEL_IDENTIFIER
 *   * GST_MTS_DESC_DVB_FREQUENCY_LIST
 */

#define BCD_UN(a) ((a) & 0x0f)
#define BCD_DEC(a) (((a) >> 4) & 0x0f)
#define BCD(a) (BCD_UN(a) + 10 * BCD_DEC(a))
#define BCD_16(a) (BCD(a[1]) + 100 * BCD(a[0]))
#define BCD_28(a) (BCD_DEC(a[3]) + 10 * BCD(a[2]) + 1000 * BCD(a[1]) + 100000 * BCD(a[0]))
#define BCD_32(a) (BCD(a[3]) + 100 * BCD(a[2]) + 10000 * BCD(a[1]) + 1000000 * BCD(a[0]))

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
  g_return_val_if_fail (descriptor != NULL && name != NULL, FALSE);
  /* We need at least one byte of data for the string */
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_NETWORK_NAME, 1, FALSE);

  *name = get_encoding_and_convert ((gchar *) descriptor->data + 2,
      descriptor->data[1]);
  return TRUE;
}

/**
 * gst_mpegts_descriptor_from_dvb_network_name:
 * @name: the network name to set
 *
 * Fills a #GstMpegTsDescriptor to be a %GST_MTS_DESC_DVB_NETWORK_NAME,
 * with the network name @name. The data field of the #GstMpegTsDescriptor
 * will be allocated, and transferred to the caller.
 *
 * Returns: (transfer full): the #GstMpegTsDescriptor or %NULL on fail
 */
GstMpegTsDescriptor *
gst_mpegts_descriptor_from_dvb_network_name (const gchar * name)
{
  GstMpegTsDescriptor *descriptor;
  guint8 *converted_name;
  gsize size;

  g_return_val_if_fail (name != NULL, NULL);

  converted_name = dvb_text_from_utf8 (name, &size);

  g_return_val_if_fail (size < 256, NULL);

  if (!converted_name) {
    GST_WARNING ("Could not find proper encoding for string `%s`", name);
    return NULL;
  }

  descriptor = _new_descriptor (GST_MTS_DESC_DVB_NETWORK_NAME, size);
  memcpy (descriptor->data + 2, converted_name, size);
  g_free (converted_name);

  return descriptor;
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

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  /* This descriptor is always 11 bytes long */
  __common_desc_checks_exact (descriptor,
      GST_MTS_DESC_DVB_SATELLITE_DELIVERY_SYSTEM, 11, FALSE);

  data = (guint8 *) descriptor->data + 2;

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
    case 0x02:
      res->modulation_type = GST_MPEGTS_MODULATION_PSK_8;
      break;
    case 0x03:
      res->modulation_type = GST_MPEGTS_MODULATION_QAM_16;
      break;
    default:
      res->modulation_type = GST_MPEGTS_MODULATION_QAM_AUTO;
      break;
  }
  data += 1;
  /* symbol_rate is in Msymbols/ (decimal point occurs after 3rd character) */
  /* So direct BCD gives us units of (Msymbol / 10 000) = 100 sym/s */
  res->symbol_rate = BCD_28 (data) * 100;
  data += 3;
  /* fec_inner */
  switch (*data >> 4) {
    case 0x01:
      res->fec_inner = GST_MPEGTS_FEC_1_2;
      break;
    case 0x02:
      res->fec_inner = GST_MPEGTS_FEC_2_3;
      break;
    case 0x03:
      res->fec_inner = GST_MPEGTS_FEC_3_4;
      break;
    case 0x04:
      res->fec_inner = GST_MPEGTS_FEC_5_6;
      break;
    case 0x05:
      res->fec_inner = GST_MPEGTS_FEC_7_8;
      break;
    case 0x06:
      res->fec_inner = GST_MPEGTS_FEC_8_9;
      break;
    case 0x07:
      res->fec_inner = GST_MPEGTS_FEC_3_5;
      break;
    case 0x08:
      res->fec_inner = GST_MPEGTS_FEC_4_5;
      break;
    case 0x09:
      res->fec_inner = GST_MPEGTS_FEC_9_10;
      break;
    case 0x0f:
      res->fec_inner = GST_MPEGTS_FEC_NONE;
      break;
    default:
      res->fec_inner = GST_MPEGTS_FEC_AUTO;
      break;
  }


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

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  /* This descriptor is always 11 bytes long */
  __common_desc_checks_exact (descriptor,
      GST_MTS_DESC_DVB_CABLE_DELIVERY_SYSTEM, 11, FALSE);

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
  switch (*data & 0xf) {
    case 0x00:
      res->fec_inner = GST_MPEGTS_FEC_AUTO;
      break;
    case 0x01:
      res->fec_inner = GST_MPEGTS_FEC_1_2;
      break;
    case 0x02:
      res->fec_inner = GST_MPEGTS_FEC_2_3;
      break;
    case 0x03:
      res->fec_inner = GST_MPEGTS_FEC_3_4;
      break;
    case 0x04:
      res->fec_inner = GST_MPEGTS_FEC_5_6;
      break;
    case 0x05:
      res->fec_inner = GST_MPEGTS_FEC_7_8;
      break;
    case 0x06:
      res->fec_inner = GST_MPEGTS_FEC_8_9;
      break;
    case 0x07:
      res->fec_inner = GST_MPEGTS_FEC_3_5;
      break;
    case 0x08:
      res->fec_inner = GST_MPEGTS_FEC_4_5;
      break;
    case 0x09:
      res->fec_inner = GST_MPEGTS_FEC_9_10;
      break;
    case 0x0f:
      res->fec_inner = GST_MPEGTS_FEC_NONE;
      break;
    default:
      res->fec_inner = GST_MPEGTS_FEC_AUTO;
      break;
  }

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

  g_return_val_if_fail (descriptor != NULL, FALSE);
  /* Need at least 3 bytes (type and 2 bytes for the string length) */
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_SERVICE, 3, FALSE);

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

/**
 * gst_mpegts_descriptor_from_dvb_service:
 * @service_type: Service type defined as a #GstMpegTsDVBServiceType
 * @service_name: (allow-none): Name of the service
 * @service_provider: (allow-none): Name of the service provider
 *
 * Fills a #GstMpegTsDescriptor to be a %GST_MTS_DESC_DVB_SERVICE.
 * The data field of the #GstMpegTsDescriptor will be allocated,
 * and transferred to the caller.
 *
 * Returns: (transfer full): the #GstMpgTsDescriptor or %NULL on fail
 */
GstMpegTsDescriptor *
gst_mpegts_descriptor_from_dvb_service (GstMpegTsDVBServiceType service_type,
    const gchar * service_name, const gchar * service_provider)
{
  GstMpegTsDescriptor *descriptor;
  guint8 *conv_provider_name = NULL, *conv_service_name = NULL;
  gsize provider_size = 0, service_size = 0;
  guint8 *data;

  if (service_provider) {
    conv_provider_name = dvb_text_from_utf8 (service_provider, &provider_size);

    if (!conv_provider_name) {
      GST_WARNING ("Could not find proper encoding for string `%s`",
          service_provider);
      return NULL;
    }
  }

  if (provider_size >= 256) {
    g_free (conv_provider_name);
    g_return_val_if_reached (NULL);
  }

  if (service_name) {
    conv_service_name = dvb_text_from_utf8 (service_name, &service_size);

    if (!conv_service_name) {
      GST_WARNING ("Could not find proper encoding for string `%s`",
          service_name);
      return NULL;
    }
  }

  if (service_size >= 256) {
    g_free (conv_provider_name);
    g_free (conv_service_name);
    g_return_val_if_reached (NULL);
  }

  descriptor =
      _new_descriptor (GST_MTS_DESC_DVB_SERVICE,
      3 + provider_size + service_size);

  data = descriptor->data + 2;
  *data++ = service_type;
  *data++ = provider_size;
  memcpy (data, conv_provider_name, provider_size);
  data += provider_size;
  *data++ = service_size;
  memcpy (data, conv_service_name, service_size);

  g_free (conv_provider_name);
  g_free (conv_service_name);

  return descriptor;
}

/* GST_MTS_DESC_DVB_LINKAGE (0x4A) */
static void
_gst_mpegts_dvb_linkage_extened_event_free (GstMpegTsDVBLinkageExtendedEvent *
    item)
{
  g_slice_free (GstMpegTsDVBLinkageExtendedEvent, item);
}

/**
 * gst_mpegts_descriptor_parse_dvb_linkage:
 * @descriptor: a %GST_MTS_DESC_DVB_LINKAGE #GstMpegTsDescriptor
 * @res: (out) (transfer none): the #GstMpegTsDVBLinkageDescriptor to fill
 *
 * Extracts the DVB linkage information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_linkage (const GstMpegTsDescriptor * descriptor,
    GstMpegTsDVBLinkageDescriptor * res)
{
  guint8 *data, *end;

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_LINKAGE, 7, FALSE);

  data = (guint8 *) descriptor->data + 2;
  end = data + descriptor->length;

  res->transport_stream_id = GST_READ_UINT16_BE (data);
  data += 2;

  res->original_network_id = GST_READ_UINT16_BE (data);
  data += 2;

  res->service_id = GST_READ_UINT16_BE (data);
  data += 2;

  res->linkage_type = *data;
  data += 1;

  switch (res->linkage_type) {
    case GST_MPEGTS_DVB_LINKAGE_MOBILE_HAND_OVER:{
      GstMpegTsDVBLinkageMobileHandOver *hand_over;

      if (end - data < 1)
        return FALSE;

      hand_over = g_slice_new0 (GstMpegTsDVBLinkageMobileHandOver);
      res->linkage_data = (gpointer) hand_over;

      hand_over->origin_type = (*data >> 7) & 0x01;
      hand_over->hand_over_type = *data & 0x0f;
      data += 1;

      if (hand_over->hand_over_type ==
          GST_MPEGTS_DVB_LINKAGE_HAND_OVER_IDENTICAL
          || hand_over->hand_over_type ==
          GST_MPEGTS_DVB_LINKAGE_HAND_OVER_LOCAL_VARIATION
          || hand_over->hand_over_type ==
          GST_MPEGTS_DVB_LINKAGE_HAND_OVER_ASSOCIATED) {
        if (end - data < 2) {
          g_slice_free (GstMpegTsDVBLinkageMobileHandOver, hand_over);
          res->linkage_data = NULL;
          return FALSE;
        }

        hand_over->network_id = GST_READ_UINT16_BE (data);
        data += 2;
      }

      if (hand_over->origin_type == 0) {
        if (end - data < 2) {
          g_slice_free (GstMpegTsDVBLinkageMobileHandOver, hand_over);
          res->linkage_data = NULL;
          return FALSE;
        }

        hand_over->initial_service_id = GST_READ_UINT16_BE (data);
        data += 2;
      }
      break;
    }
    case GST_MPEGTS_DVB_LINKAGE_EVENT:{
      GstMpegTsDVBLinkageEvent *event;

      if (end - data < 3)
        return FALSE;

      event = g_slice_new0 (GstMpegTsDVBLinkageEvent);
      res->linkage_data = (gpointer) event;

      event->target_event_id = GST_READ_UINT16_BE (data);
      data += 2;
      event->target_listed = *data & 0x01;
      event->event_simulcast = (*data >> 1) & 0x01;
      data += 1;
      break;
    }
    case GST_MPEGTS_DVB_LINKAGE_EXTENDED_EVENT:{
      GPtrArray *ext_events;
      ext_events = g_ptr_array_new_with_free_func ((GDestroyNotify)
          _gst_mpegts_dvb_linkage_extened_event_free);

      res->linkage_data = (gpointer) ext_events;

      for (guint8 i = 0; i < *data++;) {
        GstMpegTsDVBLinkageExtendedEvent *ext_event;

        if (end - data < 3)
          goto fail;

        ext_event = g_slice_new0 (GstMpegTsDVBLinkageExtendedEvent);
        g_ptr_array_add (res->linkage_data, ext_event);

        ext_event->target_event_id = GST_READ_UINT16_BE (data);
        data += 2;
        i += 2;

        ext_event->target_listed = *data & 0x01;
        ext_event->event_simulcast = (*data >> 1) & 0x01;
        ext_event->link_type = (*data >> 3) & 0x03;
        ext_event->target_id_type = (*data >> 5) & 0x03;
        ext_event->original_network_id_flag = (*data >> 6) & 0x01;
        ext_event->service_id_flag = (*data >> 7) & 0x01;
        data += 1;
        i += 1;

        if (ext_event->target_id_type == 3) {
          if (end - data < 2)
            goto fail;

          ext_event->user_defined_id = GST_READ_UINT16_BE (data);
          data += 2;
          i += 2;
        } else {
          if (ext_event->target_id_type == 1) {
            if (end - data < 2)
              goto fail;

            ext_event->target_transport_stream_id = GST_READ_UINT16_BE (data);
            data += 2;
            i += 2;
          }
          if (ext_event->original_network_id_flag) {
            if (end - data < 2)
              goto fail;

            ext_event->target_original_network_id = GST_READ_UINT16_BE (data);
            data += 2;
            i += 2;
          }
          if (ext_event->service_id_flag) {
            if (end - data < 2)
              goto fail;

            ext_event->target_service_id = GST_READ_UINT16_BE (data);
            data += 2;
            i += 2;
          }
        }
      }
      break;
    }
    default:
      break;
  }

  res->private_data_length = end - data;
  res->private_data_bytes = g_memdup (data, res->private_data_length);

  return TRUE;

fail:
  g_ptr_array_unref (res->linkage_data);
  res->linkage_data = NULL;
  return FALSE;
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

  g_return_val_if_fail (descriptor != NULL, FALSE);
  /* Need at least 5 bytes (3 for language code, 2 for each string length) */
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_SHORT_EVENT, 5, FALSE);

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

/* GST_MTS_DESC_DVB_TELETEXT (0x56) */
/**
 * gst_mpegts_descriptor_parse_dvb_teletext_idx:
 * @descriptor: a %GST_MTS_DESC_DVB_TELETEXT #GstMpegTsDescriptor
 * @idx: The id of the teletext to get
 * @language_code: (out) (allow-none): a 4-byte gchar array to hold language
 * @teletext_type: (out) (allow-none): #GstMpegTsDVBTeletextType
 * @magazine_number: (out) (allow-none):
 * @page_number: (out) (allow-none):
 *
 * Parses teletext number @idx in the @descriptor. The language is in ISO639 format.
 *
 * Returns: FALSE on out-of-bounds and errors
 */
gboolean
gst_mpegts_descriptor_parse_dvb_teletext_idx (const GstMpegTsDescriptor *
    descriptor, guint idx, gchar (*language_code)[4],
    GstMpegTsDVBTeletextType * teletext_type, guint8 * magazine_number,
    guint8 * page_number)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_TELETEXT, 0, FALSE);

  if (descriptor->length / 5 <= idx)
    return FALSE;

  data = (guint8 *) descriptor->data + 2 + idx * 5;

  if (language_code) {
    memcpy (language_code, data, 3);
    (*language_code)[3] = 0;
  }

  if (teletext_type)
    *teletext_type = data[3] >> 3;

  if (magazine_number)
    *magazine_number = data[3] & 0x07;

  if (page_number)
    *page_number = data[4];

  return TRUE;
}

/**
 * gst_mpegts_descriptor_parse_dvb_teletext_nb:
 * @descriptor: a %GST_MTS_DESC_DVB_TELETEXT #GstMpegTsDescriptor
 *
 * Find the number of teletext entries in @descriptor
 *
 * Returns: Number of teletext entries
 */
guint
gst_mpegts_descriptor_parse_dvb_teletext_nb (const GstMpegTsDescriptor *
    descriptor)
{
  g_return_val_if_fail (descriptor != NULL, 0);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_TELETEXT, 0, 0);

  return descriptor->length / 5;
}

/* GST_MTS_DESC_DVB_SUBTITLING (0x59) */

/**
 * gst_mpegts_descriptor_parse_dvb_subtitling_idx:
 * @descriptor: a %GST_MTS_DESC_DVB_SUBTITLING #GstMpegTsDescriptor
 * @idx: Table id of the entry to parse
 * @lang: (out) (transfer none): 4-byte gchar array to hold the language code
 * @type: (out) (transfer none) (allow-none): the type of subtitling
 * @composition_page_id: (out) (transfer none) (allow-none): the composition page id
 * @ancillary_page_id: (out) (transfer none) (allow-none): the ancillary page id
 *
 * Extracts the DVB subtitling informatio from specific table id in @descriptor.
 *
 * Note: Use #gst_tag_get_language_code if you want to get the the
 * ISO 639-1 language code from the returned ISO 639-2 one.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_subtitling_idx (const GstMpegTsDescriptor *
    descriptor, guint idx, gchar (*lang)[4], guint8 * type,
    guint16 * composition_page_id, guint16 * ancillary_page_id)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && lang != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_SUBTITLING, 0, FALSE);

  /* If we went too far, return FALSE */
  if (descriptor->length / 8 <= idx)
    return FALSE;

  data = (guint8 *) descriptor->data + 2 + idx * 8;

  memcpy (lang, data, 3);
  (*lang)[3] = 0;

  data += 3;

  if (type)
    *type = *data;
  data += 1;
  if (composition_page_id)
    *composition_page_id = GST_READ_UINT16_BE (data);
  data += 2;
  if (ancillary_page_id)
    *ancillary_page_id = GST_READ_UINT16_BE (data);

  return TRUE;
}

/**
 * gst_mpegts_descriptor_parse_dvb_subtitling_nb:
 * @descriptor: a %GST_MTS_DESC_DVB_SUBTITLING #GstMpegTsDescriptor
 *
 * Returns: The number of entries in @descriptor
 */
guint
gst_mpegts_descriptor_parse_dvb_subtitling_nb (const GstMpegTsDescriptor *
    descriptor)
{
  g_return_val_if_fail (descriptor != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_SUBTITLING, 0, FALSE);

  return descriptor->length / 8;
}

/**
 * gst_mpegts_descriptor_from_dvb_subtitling:
 * @lang: (transfer none): a string containing the ISO639 language
 * @type: subtitling type
 * @composition: composition page id
 * @ancillary: ancillary page id
 */
GstMpegTsDescriptor *
gst_mpegts_descriptor_from_dvb_subtitling (const gchar * lang,
    guint8 type, guint16 composition, guint16 ancillary)
{
  GstMpegTsDescriptor *descriptor;
  guint8 *data;

  g_return_val_if_fail (lang != NULL, NULL);

  descriptor = _new_descriptor (GST_MTS_DESC_DVB_SUBTITLING, 8);

  data = descriptor->data + 2;

  memcpy (data, lang, 3);
  data += 3;

  *data++ = type;

  GST_WRITE_UINT16_BE (data, composition);
  data += 2;

  GST_WRITE_UINT16_BE (data, ancillary);

  return descriptor;
}

/* GST_MTS_DESC_DVB_EXTENDED_EVENT (0x4E) */

static void
_gst_mpegts_extended_event_item_free (GstMpegTsExtendedEventItem * item)
{
  g_slice_free (GstMpegTsExtendedEventItem, item);
}

/**
 * gst_mpegts_descriptor_parse_dvb_extended_event:
 * @descriptor: a %GST_MTS_DESC_DVB_EXTENDED_EVENT #GstMpegTsDescriptor
 * @res: (out) (transfer none): the #GstMpegTsExtendedEventDescriptor to fill
 *
 * Extracts the DVB extended event information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_extended_event (const GstMpegTsDescriptor
    * descriptor, GstMpegTsExtendedEventDescriptor * res)
{
  guint8 *data, *desc_data;
  guint8 tmp, len_item;
  GstMpegTsExtendedEventItem *item;
  guint i;

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  /* Need at least 6 bytes (1 for desc number, 3 for language code, 2 for the loop length) */
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_EXTENDED_EVENT, 6, FALSE);

  data = (guint8 *) descriptor->data + 2;

  tmp = *data;
  res->descriptor_number = tmp >> 4;
  res->last_descriptor_number = tmp & 0x0f;

  data += 1;

  memcpy (data, res->language_code, 3);

  data += 3;

  len_item = *data;

  data += 1;

  res->nb_items = 0;
  res->items = g_ptr_array_new_with_free_func ((GDestroyNotify)
      _gst_mpegts_extended_event_item_free);

  for (i = 0; i < len_item;) {
    desc_data = data;
    item = g_slice_new0 (GstMpegTsExtendedEventItem);
    item->item_description =
        get_encoding_and_convert ((const gchar *) desc_data + 1, *desc_data);

    desc_data += *desc_data + 1;
    i += *desc_data + 1;

    item->item =
        get_encoding_and_convert ((const gchar *) desc_data + 1, *desc_data);

    desc_data += *desc_data + 1;
    i += *desc_data + 1;

    g_ptr_array_add (res->items, item);
    res->nb_items += 1;
  }
  data += len_item;
  res->text = get_encoding_and_convert ((const gchar *) data + 1, *data);

  return TRUE;
}

/* GST_MTS_DESC_DVB_COMPONENT (0x50) */
/**
 * gst_mpegts_descriptor_parse_dvb_component:
 * @descriptor: a %GST_MTS_DESC_DVB_COMPONENT #GstMpegTsDescriptor
 * @res: (out) (transfer none): the #GstMpegTsComponentDescriptor to fill
 *
 * Extracts the DVB component information from @descriptor.
 *
 * Returns: %TRUE if parsing succeeded, else %FALSE.
 */

gboolean
gst_mpegts_descriptor_parse_dvb_component (const GstMpegTsDescriptor
    * descriptor, GstMpegTsComponentDescriptor * res)
{
  guint8 *data;
  guint8 len;

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  /* Need 6 bytes at least (1 for content, 1 for type, 1 for tag, 3 for language code) */
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_COMPONENT, 6, FALSE);

  data = (guint8 *) descriptor->data + 2;

  res->stream_content = *data & 0x0f;
  data += 1;

  res->component_type = *data;
  data += 1;

  res->component_tag = *data;
  data += 1;

  memcpy (data, res->language_code, 3);
  data += 3;

  len = descriptor->length - 6;
  if (len)
    res->text = get_encoding_and_convert ((const gchar *) data, len);

  return TRUE;
}

/* GST_MTS_DESC_DVB_STREAM_IDENTIFIER (0x52) */
/**
 * gst_mpegts_descriptor_parse_dvb_stream_identifier:
 * @descriptor: a %GST_MTS_DESC_DVB_CONTENT #GstMpegTsDescriptor
 * @component_tag: (out) (transfer none): the component tag
 *
 * Extracts the component tag from @descriptor.
 *
 * Returns: %TRUE if the parsing happended correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_stream_identifier (const GstMpegTsDescriptor
    * descriptor, guint8 * component_tag)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && component_tag != NULL, FALSE);
  __common_desc_checks_exact (descriptor, GST_MTS_DESC_DVB_STREAM_IDENTIFIER,
      1, FALSE);

  data = (guint8 *) descriptor->data + 2;

  *component_tag = *data;

  return TRUE;
}

/* GST_MTS_DESC_DVB_CA_IDENTIFIER (0x53) */
/**
 * gst_mpegts_descriptor_parse_dvb_ca_identifier:
 * @descriptor: a %GST_MTS_DESC_DVB_CA_IDENTIFIER #GstMpegTsDescriptor
 * @list: (out) (transfer none) (element-type guint16): This 16-bit field
 * identifies the CA system. Allocations of the value of this field are found
 * in http://www.dvbservices.com
 *
 * Extracts ca id's from @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_ca_identifier (const GstMpegTsDescriptor *
    descriptor, GArray ** list)
{
  guint8 *data;
  guint16 tmp;

  g_return_val_if_fail (descriptor != NULL && list != NULL, FALSE);
  /* 2 bytes = one entry */
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_CA_IDENTIFIER, 2, FALSE);

  data = (guint8 *) descriptor->data + 2;

  *list = g_array_new (FALSE, FALSE, sizeof (guint16));

  for (guint i = 0; i < descriptor->length - 1; i += 2) {
    tmp = GST_READ_UINT16_BE (data);
    g_array_append_val (*list, tmp);
    data += 2;
  }

  return TRUE;
}

/* GST_MTS_DESC_DVB_CONTENT (0x54) */
static void
_gst_mpegts_content_free (GstMpegTsContent * content)
{
  g_slice_free (GstMpegTsContent, content);
}

/**
 * gst_mpegts_descriptor_parse_dvb_content:
 * @descriptor: a %GST_MTS_DESC_DVB_CONTENT #GstMpegTsDescriptor
 * @content: (out) (transfer none) (element-type GstMpegTsContent): #GstMpegTsContent
 *
 * Extracts the DVB content information from @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_content (const GstMpegTsDescriptor
    * descriptor, GPtrArray ** content)
{
  guint8 *data;
  guint8 len, tmp;
  guint8 i;

  g_return_val_if_fail (descriptor != NULL && content != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_CONTENT, 0, FALSE);

  data = (guint8 *) descriptor->data + 2;
  len = descriptor->length;

  *content = g_ptr_array_new_with_free_func ((GDestroyNotify)
      _gst_mpegts_content_free);
  for (i = 0; i < len;) {
    GstMpegTsContent *cont = g_slice_new0 (GstMpegTsContent);
    tmp = *data;
    cont->content_nibble_1 = (tmp & 0xf0) >> 4;
    cont->content_nibble_2 = tmp & 0x0f;
    data += 1;
    cont->user_byte = *data;
    data += 1;
    i += 2;
    g_ptr_array_add (*content, cont);
  }

  return TRUE;
}

/* GST_MTS_DESC_DVB_PARENTAL_RATING (0x55) */
static void
_gst_mpegts_dvb_parental_rating_item_free (GstMpegTsDVBParentalRatingItem *
    item)
{
  g_slice_free (GstMpegTsDVBParentalRatingItem, item);
}

/**
 * gst_mpegts_descriptor_parse_dvb_parental_rating:
 * @descriptor: a %GST_MTS_DESC_DVB_PARENTAL_RATING #GstMpegTsDescriptor
 * @rating: (out) (transfer none) (element-type GstMpegTsDVBParentalRatingItem):
 * #GstMpegTsDVBParentalRatingItem
 *
 * Extracts the DVB parental rating information from @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_parental_rating (const GstMpegTsDescriptor
    * descriptor, GPtrArray ** rating)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && rating != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_PARENTAL_RATING, 0, FALSE);

  data = (guint8 *) descriptor->data + 2;

  *rating = g_ptr_array_new_with_free_func ((GDestroyNotify)
      _gst_mpegts_dvb_parental_rating_item_free);

  for (guint8 i = 0; i < descriptor->length - 3; i += 4) {
    GstMpegTsDVBParentalRatingItem *item =
        g_slice_new0 (GstMpegTsDVBParentalRatingItem);
    g_ptr_array_add (*rating, item);

    memcpy (data, item->country_code, 3);
    data += 3;

    if (g_strcmp0 (item->country_code, "BRA") == 0) {
      /* brasil */
      switch (*data & 0xf) {
        case 1:
          item->rating = 6;
          break;
        case 2:
          item->rating = 10;
          break;
        case 3:
          item->rating = 12;
          break;
        case 4:
          item->rating = 14;
          break;
        case 5:
          item->rating = 16;
          break;
        case 6:
          item->rating = 18;
          break;
        default:
          item->rating = 0;
          break;
      }
    } else {
      item->rating = (*data & 0xf) + 3;
    }

    data += 1;
  }

  return TRUE;
}

/* GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM (0x5A) */
/**
 * gst_mpegts_descriptor_parse_terrestrial_delivery_system:
 * @descriptor: a %GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM #GstMpegTsDescriptor
 * @res: (out) (transfer none): #GstMpegTsTerrestrialDeliverySystemDescriptor
 *
 * Parses out the terrestrial delivery system from the @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_terrestrial_delivery_system (const
    GstMpegTsDescriptor * descriptor,
    GstMpegTsTerrestrialDeliverySystemDescriptor * res)
{
  guint8 *data;
  guint8 tmp;

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  /* Descriptor is always 11 bytes long */
  __common_desc_checks_exact (descriptor,
      GST_MTS_DESC_DVB_TERRESTRIAL_DELIVERY_SYSTEM, 11, FALSE);

  data = (guint8 *) descriptor->data + 2;

  res->frequency = 0;
  res->frequency = GST_READ_UINT32_BE (data);
  res->frequency *= 10;

  data += 4;

  tmp = *data;
  switch ((tmp >> 5) & 0x07) {
    case 0:
      res->bandwidth = 8000000;
      break;
    case 1:
      res->bandwidth = 7000000;
      break;
    case 2:
      res->bandwidth = 6000000;
      break;
    case 3:
      res->bandwidth = 5000000;
      break;
    default:
      res->bandwidth = 0;
      break;
  }

  res->priority = (tmp >> 4) & 0x01;
  res->time_slicing = (tmp >> 3) & 0x01;
  res->mpe_fec = (tmp >> 2) & 0x01;
  data += 1;

  tmp = *data;
  switch ((tmp >> 6) & 0x03) {
    case 0:
      res->constellation = GST_MPEGTS_MODULATION_QPSK;
      break;
    case 1:
      res->constellation = GST_MPEGTS_MODULATION_QAM_16;
      break;
    case 2:
      res->constellation = GST_MPEGTS_MODULATION_QAM_64;
      break;
    default:
      break;
  }

  switch ((tmp >> 3) & 0x07) {
    case 0:
      res->hierarchy = GST_MPEGTS_HIERARCHY_NONE;
      break;
    case 1:
      res->hierarchy = GST_MPEGTS_HIERARCHY_1;
      break;
    case 2:
      res->hierarchy = GST_MPEGTS_HIERARCHY_2;
      break;
    case 3:
      res->hierarchy = GST_MPEGTS_HIERARCHY_4;
      break;
    case 4:
      res->hierarchy = GST_MPEGTS_HIERARCHY_NONE;
      break;
    case 5:
      res->hierarchy = GST_MPEGTS_HIERARCHY_1;
      break;
    case 6:
      res->hierarchy = GST_MPEGTS_HIERARCHY_2;
      break;
    case 7:
      res->hierarchy = GST_MPEGTS_HIERARCHY_4;
      break;
    default:
      break;
  }

  switch (tmp & 0x07) {
    case 0:
      res->code_rate_hp = GST_MPEGTS_FEC_1_2;
      break;
    case 1:
      res->code_rate_hp = GST_MPEGTS_FEC_2_3;
      break;
    case 2:
      res->code_rate_hp = GST_MPEGTS_FEC_3_4;
      break;
    case 3:
      res->code_rate_hp = GST_MPEGTS_FEC_5_6;
      break;
    case 4:
      res->code_rate_hp = GST_MPEGTS_FEC_7_8;
      break;
    default:
      break;
  }
  data += 1;

  tmp = *data;
  switch ((tmp >> 5) & 0x07) {
    case 0:
      res->code_rate_lp = GST_MPEGTS_FEC_1_2;
      break;
    case 1:
      res->code_rate_lp = GST_MPEGTS_FEC_2_3;
      break;
    case 2:
      res->code_rate_lp = GST_MPEGTS_FEC_3_4;
      break;
    case 3:
      res->code_rate_lp = GST_MPEGTS_FEC_5_6;
      break;
    case 4:
      res->code_rate_lp = GST_MPEGTS_FEC_7_8;
      break;
    default:
      break;
  }

  switch ((tmp >> 3) & 0x03) {
    case 0:
      res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_32;
      break;
    case 1:
      res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_16;
      break;
    case 2:
      res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_8;
      break;
    case 3:
      res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_4;
      break;
    default:
      break;
  }

  switch ((tmp >> 1) & 0x03) {
    case 0:
      res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_2K;
      break;
    case 1:
      res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_8K;
      break;
    case 2:
      res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_4K;
      break;
    default:
      break;
  }
  res->other_frequency = tmp & 0x01;

  return TRUE;
}

/* GST_MTS_DESC_DVB_PRIVATE_DATA_SPECIFIER (0x5F) */
/**
 * gst_mpegts_descriptor_parse_dvb_private_data_specifier:
 * @descriptor: a %GST_MTS_DESC_DVB_PRIVATE_DATA_SPECIFIER #GstMpegTsDescriptor
 * @private_data_specifier: (out): the private data specifier id
 * registered by http://www.dvbservices.com/
 * @private_data: (out): additional data or NULL
 * @length: (out): length of %private_data
 *
 * Parses out the private data specifier from the @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_private_data_specifier (const
    GstMpegTsDescriptor * descriptor, guint32 * private_data_specifier,
    guint8 ** private_data, guint8 * length)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL
      && private_data_specifier != NULL, FALSE);
  __common_desc_checks (descriptor,
      GST_MTS_DESC_DVB_PRIVATE_DATA_SPECIFIER, 4, FALSE);

  data = (guint8 *) descriptor->data + 2;

  *private_data_specifier = GST_READ_UINT32_BE (data);

  *length = descriptor->length - 4;

  *private_data = g_memdup (data + 4, *length);

  return TRUE;
}

/* GST_MTS_DESC_DVB_FREQUENCY_LIST (0x62) */
/**
 * gst_mpegts_descriptor_parse_dvb_frequency_list:
 * @descriptor: a %GST_MTS_DESC_DVB_FREQUENCY_LIST #GstMpegTsDescriptor
 * @offset: (out): %FALSE in Hz, %TRUE in kHz
 * @list: (out): a list of all frequencies in Hz/kHz depending from %offset
 *
 * Parses out a list of frequencies from the @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_frequency_list (const GstMpegTsDescriptor
    * descriptor, gboolean * offset, GArray ** list)
{
  guint8 *data, type, len;
  guint32 freq;

  g_return_val_if_fail (descriptor != NULL && offset != NULL &&
      list != NULL, FALSE);
  /* 1 byte coding system, 4 bytes each frequency entry */
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_FREQUENCY_LIST, 5, FALSE);

  data = (guint8 *) descriptor->data + 2;

  type = *data & 0x03;
  data += 1;

  if (type == 1) {
    /* satellite */
    *offset = TRUE;
  } else {
    /* cable, terrestrial */
    *offset = FALSE;
  }

  *list = g_array_new (FALSE, FALSE, sizeof (guint32));

  len = descriptor->length - 1;

  for (guint8 i = 0; i < len - 3; i += 4) {
    switch (type) {
      case 1:
        freq = BCD_32 (data) * 10;
        break;
      case 2:
        freq = BCD_32 (data) * 100;
        break;
      case 3:
        freq = GST_READ_UINT32_BE (data) * 10;
        break;
      default:
        break;
    }

    g_array_append_val (*list, freq);
    data += 4;
  }

  return TRUE;
}

/* GST_MTS_DESC_DVB_DATA_BROADCAST (0x64) */
/**
 * gst_mpegts_descriptor_parse_dvb_data_broadcast:
 * @descriptor: a %GST_MTS_DESC_DVB_DATA_BROADCAST #GstMpegTsDescriptor
 * @res: (out) (transfer none): #GstMpegTsDataBroadcastDescriptor
 *
 * Parses out the data broadcast from the @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_data_broadcast (const GstMpegTsDescriptor
    * descriptor, GstMpegTsDataBroadcastDescriptor * res)
{
  guint8 *data;
  guint8 len;

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_DATA_BROADCAST, 8, FALSE);

  data = (guint8 *) descriptor->data + 2;

  res->data_broadcast_id = GST_READ_UINT16_BE (data);
  data += 2;

  res->component_tag = *data;
  data += 1;

  len = *data;
  data += 1;

  res->selector_bytes = g_memdup (data, len);
  data += len;

  memcpy (data, res->language_code, 3);
  data += 3;

  res->text = get_encoding_and_convert ((const gchar *) data + 1, *data);

  return TRUE;
}

/* GST_MTS_DESC_DVB_SCRAMBLING (0x65) */
/**
 * gst_mpegts_descriptor_parse_dvb_scrambling:
 * @descriptor: a %GST_MTS_DESC_DVB_SCRAMBLING #GstMpegTsDescriptor
 * @scrambling_mode: (out): This 8-bit field identifies the selected
 * mode of the scrambling algorithm (#GstMpegTsDVBScramblingModeType).
 * The technical details of the scrambling algorithm are available only
 * to bona-fide users upon signature of a Non Disclosure Agreement (NDA)
 * administered by the DVB Common Scrambling Algorithm Custodian.
 *
 * Parses out the scrambling mode from the @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_scrambling (const GstMpegTsDescriptor *
    descriptor, GstMpegTsDVBScramblingModeType * scrambling_mode)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && scrambling_mode != NULL, FALSE);
  __common_desc_checks_exact (descriptor, GST_MTS_DESC_DVB_SCRAMBLING, 1,
      FALSE);

  data = (guint8 *) descriptor->data + 2;

  *scrambling_mode = *data;

  return TRUE;
}

/* GST_MTS_DESC_DVB_DATA_BROADCAST_ID (0x66) */
/**
 * gst_mpegts_descriptor_parse_dvb_data_broadcast_id:
 * @descriptor: a %GST_MTS_DESC_DVB_DATA_BROADCAST_ID #GstMpegTsDescriptor
 * @data_broadcast_id: (out): the data broadcast id
 * @id_selector_bytes: (out): the selector bytes, if present
 * @len: (out): the length of #id_selector_bytes
 *
 * Parses out the data broadcast id from the @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_data_broadcast_id (const GstMpegTsDescriptor
    * descriptor, guint16 * data_broadcast_id, guint8 ** id_selector_bytes,
    guint8 * len)
{
  guint8 *data;

  g_return_val_if_fail (descriptor != NULL && data_broadcast_id != NULL &&
      id_selector_bytes != NULL, FALSE);
  __common_desc_checks (descriptor, GST_MTS_DESC_DVB_DATA_BROADCAST_ID, 2,
      FALSE);

  data = (guint8 *) descriptor->data + 2;

  *data_broadcast_id = GST_READ_UINT16_BE (data);
  data += 2;

  *len = descriptor->length - 2;

  *id_selector_bytes = g_memdup (data, *len);

  return TRUE;
}

/* GST_MTS_DESC_EXT_DVB_T2_DELIVERY_SYSTEM (0x7F && 0x04) */
static void
    _gst_mpegts_t2_delivery_system_cell_extension_free
    (GstMpegTsT2DeliverySystemCellExtension * ext)
{
  g_slice_free (GstMpegTsT2DeliverySystemCellExtension, ext);
}

static void
_gst_mpegts_t2_delivery_system_cell_free (GstMpegTsT2DeliverySystemCell * cell)
{
  g_ptr_array_unref (cell->sub_cells);
  g_slice_free (GstMpegTsT2DeliverySystemCell, cell);
}

/**
 * gst_mpegts_descriptor_parse_dvb_t2_delivery_system:
 * @descriptor: a %GST_MTS_DESC_EXT_DVB_T2_DELIVERY_SYSTEM #GstMpegTsDescriptor
 * @res: (out) (transfer none): #GstMpegTsT2DeliverySystemDescriptor
 *
 * Parses out the DVB-T2 delivery system from the @descriptor.
 *
 * Returns: %TRUE if the parsing happened correctly, else %FALSE.
 */
gboolean
gst_mpegts_descriptor_parse_dvb_t2_delivery_system (const GstMpegTsDescriptor
    * descriptor, GstMpegTsT2DeliverySystemDescriptor * res)
{
  guint8 *data;
  guint8 len, freq_len, sub_cell_len;
  guint32 tmp_freq;
  guint8 i;

  g_return_val_if_fail (descriptor != NULL && res != NULL, FALSE);
  __common_desc_ext_checks (descriptor, GST_MTS_DESC_EXT_DVB_T2_DELIVERY_SYSTEM,
      4, FALSE);

  data = (guint8 *) descriptor->data + 3;

  res->plp_id = *data;
  data += 1;

  res->t2_system_id = GST_READ_UINT16_BE (data);
  data += 2;

  if (descriptor->length > 4) {
    // FIXME: siso / miso
    res->siso_miso = (*data >> 6) & 0x03;
    switch ((*data >> 2) & 0x0f) {
      case 0:
        res->bandwidth = 8000000;
        break;
      case 1:
        res->bandwidth = 7000000;
        break;
      case 2:
        res->bandwidth = 6000000;
        break;
      case 3:
        res->bandwidth = 5000000;
        break;
      case 4:
        res->bandwidth = 10000000;
        break;
      case 5:
        res->bandwidth = 1712000;
        break;
      default:
        res->bandwidth = 0;
        break;
    }
    data += 1;

    switch ((*data >> 5) & 0x07) {
      case 0:
        res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_32;
        break;
      case 1:
        res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_16;
        break;
      case 2:
        res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_8;
        break;
      case 3:
        res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_4;
        break;
      case 4:
        res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_1_128;
        break;
      case 5:
        res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_19_128;
        break;
      case 6:
        res->guard_interval = GST_MPEGTS_GUARD_INTERVAL_19_256;
        break;
      default:
        break;
    }

    switch ((*data >> 2) & 0x07) {
      case 0:
        res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_2K;
        break;
      case 1:
        res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_8K;
        break;
      case 2:
        res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_4K;
        break;
      case 3:
        res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_1K;
        break;
      case 4:
        res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_16K;
        break;
      case 5:
        res->transmission_mode = GST_MPEGTS_TRANSMISSION_MODE_32K;
        break;
      default:
        break;
    }
    res->other_frequency = (*data >> 1) & 0x01;
    res->tfs = (*data) & 0x01;
    data += 1;

    len = descriptor->length - 6;

    res->cells = g_ptr_array_new_with_free_func ((GDestroyNotify)
        _gst_mpegts_t2_delivery_system_cell_free);

    for (i = 0; i < len;) {
      GstMpegTsT2DeliverySystemCell *cell;
      guint8 j, k;

      cell = g_slice_new0 (GstMpegTsT2DeliverySystemCell);
      g_ptr_array_add (res->cells, cell);

      cell->cell_id = GST_READ_UINT16_BE (data);
      data += 2;
      i += 2;

      cell->centre_frequencies = g_array_new (FALSE, FALSE, sizeof (guint32));

      if (res->tfs == TRUE) {
        freq_len = *data;
        data += 1;
        i += 1;

        for (j = 0; j < freq_len;) {
          tmp_freq = GST_READ_UINT32_BE (data) * 10;
          g_array_append_val (cell->centre_frequencies, tmp_freq);
          data += 4;
          j += 4;
          i += 4;
        }
      } else {
        tmp_freq = GST_READ_UINT32_BE (data) * 10;
        g_array_append_val (cell->centre_frequencies, tmp_freq);
        data += 4;
        i += 4;
      }
      sub_cell_len = (*data);
      data += 1;
      i += 1;

      cell->sub_cells = g_ptr_array_new_with_free_func ((GDestroyNotify)
          _gst_mpegts_t2_delivery_system_cell_extension_free);

      for (k = 0; k < sub_cell_len;) {
        GstMpegTsT2DeliverySystemCellExtension *cell_ext;
        cell_ext = g_slice_new0 (GstMpegTsT2DeliverySystemCellExtension);

        g_ptr_array_add (cell->sub_cells, cell_ext);
        cell_ext->cell_id_extension = *data;
        data += 1;

        cell_ext->transposer_frequency = GST_READ_UINT32_BE (data) * 10;
        data += 4;
        i += 5;
        k += 5;
      }
    }
  }
  return TRUE;
}
