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

#include "sbc.h"

#include "gstsbcdec.h"

GST_DEBUG_CATEGORY_STATIC (sbc_dec_debug);
#define GST_CAT_DEFAULT sbc_dec_debug

GST_BOILERPLATE (GstSbcDec, gst_sbc_dec, GstElement, GST_TYPE_ELEMENT);

static const GstElementDetails sbc_dec_details =
GST_ELEMENT_DETAILS ("Bluetooth SBC decoder",
    "Codec/Decoder/Audio",
    "Decode a SBC audio stream",
    "Marcel Holtmann <marcel@holtmann.org>");

static void
gst_sbc_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &sbc_dec_details);
}

static void
gst_sbc_dec_class_init (GstSbcDecClass * klass)
{
  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (sbc_dec_debug, "sbcdec", 0, "SBC decoding element");
}

static void
gst_sbc_dec_init (GstSbcDec * sbcdec, GstSbcDecClass * klass)
{
}
