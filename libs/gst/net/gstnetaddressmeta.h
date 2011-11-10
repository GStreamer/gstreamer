/* GStreamer
 * Copyright (C) <2011> Wim Taymans <wim.taymans@gmail.com>
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

#ifndef __GST_NET_ADDRESS_META_H__
#define __GST_NET_ADDRESS_META_H__

#include <gst/gst.h>

G_BEGIN_DECLS

typedef struct _GstNetAddress GstNetAddress;

/**
 * GstNetType:
 * @GST_NET_TYPE_UNKNOWN: unknown address type
 * @GST_NET_TYPE_IP4: an IPv4 address type
 * @GST_NET_TYPE_IP6: and IPv6 address type
 *
 * The Address type used in #GstNetAddress.
 */
typedef enum {
  GST_NET_TYPE_UNKNOWN,
  GST_NET_TYPE_IP4,
  GST_NET_TYPE_IP6,
} GstNetType;

/**
 * GST_NETADDRESS_MAX_LEN:
 *
 * The maximum length of a string representation of a GstNetAddress as produced
 * by gst_net_address_to_string().
 *
 * Since: 0.10.24
 */
#define GST_NETADDRESS_MAX_LEN	64

/**
 * GstNetAddress:
 *
 * An opaque network address as used in #GstNetAddressMeta.
 */
struct _GstNetAddress {
  /*< private >*/
  GstNetType    type;
  union {
    guint8        ip6[16];
    guint32       ip4;
  } address;
  guint16       port;
  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

typedef struct _GstNetAddressMeta GstNetAddressMeta;

/**
 * GstNetAddressMeta:
 *
 * Buffer metadata for network addresses.
 */
struct _GstNetAddressMeta {
  GstMeta       meta;

  GstNetAddress naddr;
};

const GstMetaInfo *gst_net_address_meta_get_info (void);
#define GST_NET_ADDRESS_META_INFO (gst_net_address_meta_get_info())

#define gst_buffer_get_net_address_meta(b) \
  ((GstNetAddressMeta*)gst_buffer_get_meta((b),GST_NET_ADDRESS_META_INFO))
#define gst_buffer_add_net_address_meta(b) \
  ((GstNetAddressMeta*)gst_buffer_add_meta((b),GST_NET_ADDRESS_META_INFO,NULL))

/* address operations */
void            gst_net_address_set_ip4_address   (GstNetAddress *naddr, guint32 address, guint16 port);
void            gst_net_address_set_ip6_address   (GstNetAddress *naddr, guint8 address[16], guint16 port);
gint            gst_net_address_set_address_bytes (GstNetAddress *naddr, GstNetType type,
                                                   guint8 address[16], guint16 port);

GstNetType      gst_net_address_get_net_type      (const GstNetAddress *naddr);
gboolean        gst_net_address_get_ip4_address   (const GstNetAddress *naddr, guint32 *address, guint16 *port);
gboolean        gst_net_address_get_ip6_address   (const GstNetAddress *naddr, guint8 address[16], guint16 *port);
gint            gst_net_address_get_address_bytes (const GstNetAddress *naddr, guint8 address[16], guint16 *port);

gboolean        gst_net_address_equal             (const GstNetAddress *naddr1,
                                                   const GstNetAddress *naddr2);

gint            gst_net_address_to_string         (const GstNetAddress *naddr, gchar *dest, gsize len);

G_END_DECLS

#endif /* __GST_NET_ADDRESS_META_H__ */

