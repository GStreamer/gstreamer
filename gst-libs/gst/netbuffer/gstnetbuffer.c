/* GStreamer
 * Copyright (C) <2005> Wim Taymans <wim@fluendo.com>
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
 * SECTION:gstnetbuffer
 * @short_description: Buffer for use in network sources and sinks
 *
 * #GstNetBuffer is a subclass of a normal #GstBuffer that contains two
 * additional metadata fields of type #GstNetAddress named 'to' and 'from'. The
 * buffer can be used to store additional information about the origin of the
 * buffer data and is used in various network elements to track the to and from
 * addresses.
 *
 * Last reviewed on 2006-08-21 (0.10.10)
 */

#include <string.h>

#include "gstnetbuffer.h"

static void gst_netbuffer_finalize (GstNetBuffer * nbuf);
static GstNetBuffer *gst_netbuffer_copy (GstNetBuffer * nbuf);

static GstBufferClass *parent_class;

G_DEFINE_TYPE (GstNetBuffer, gst_netbuffer, GST_TYPE_BUFFER);

static void
gst_netbuffer_class_init (GstNetBufferClass * netbuffer_class)
{
  GstMiniObjectClass *mo_class = GST_MINI_OBJECT_CLASS (netbuffer_class);

  parent_class = g_type_class_peek_parent (netbuffer_class);

  mo_class->copy = (GstMiniObjectCopyFunction) gst_netbuffer_copy;
  mo_class->finalize = (GstMiniObjectFinalizeFunction) gst_netbuffer_finalize;
}

static void
gst_netbuffer_init (GstNetBuffer * instance)
{
}

static void
gst_netbuffer_finalize (GstNetBuffer * nbuf)
{
  GST_MINI_OBJECT_CLASS (parent_class)->finalize (GST_MINI_OBJECT (nbuf));
}

static GstNetBuffer *
gst_netbuffer_copy (GstNetBuffer * nbuf)
{
  GstNetBuffer *copy;

  copy = gst_netbuffer_new ();

  /* we simply copy everything from our parent */
  GST_BUFFER_DATA (copy) =
      g_memdup (GST_BUFFER_DATA (nbuf), GST_BUFFER_SIZE (nbuf));
  /* make sure it gets freed (even if the parent is subclassed, we return a
     normal buffer) */
  GST_BUFFER_MALLOCDATA (copy) = GST_BUFFER_DATA (copy);
  GST_BUFFER_SIZE (copy) = GST_BUFFER_SIZE (nbuf);

  memcpy (&copy->to, &nbuf->to, sizeof (nbuf->to));
  memcpy (&copy->from, &nbuf->from, sizeof (nbuf->from));

  /* copy metadata */
  gst_buffer_copy_metadata (GST_BUFFER_CAST (copy),
      GST_BUFFER_CAST (nbuf), GST_BUFFER_COPY_ALL);

  return copy;
}

/**
 * gst_netbuffer_new:
 *
 * Create a new network buffer.
 *
 * Returns: a new #GstNetBuffer.
 */
GstNetBuffer *
gst_netbuffer_new (void)
{
  GstNetBuffer *buf;

  buf = (GstNetBuffer *) gst_mini_object_new (GST_TYPE_NETBUFFER);

  return buf;
}

/**
 * gst_netaddress_set_ip4_address:
 * @naddr: a network address
 * @address: an IPv4 network address.
 * @port: a port number to set.
 *
 * Set @naddr with the IPv4 @address and @port pair.
 *
 * Note that @port and @address must be expressed in network byte order,
 * use g_htons() and g_htonl() to convert them to network byte order.
 */
void
gst_netaddress_set_ip4_address (GstNetAddress * naddr, guint32 address,
    guint16 port)
{
  g_return_if_fail (naddr != NULL);

  naddr->type = GST_NET_TYPE_IP4;
  naddr->address.ip4 = address;
  naddr->port = port;
}

/**
 * gst_netaddress_set_ip6_address:
 * @naddr: a network address
 * @address: an IPv6 network address.
 * @port: a port number to set.
 *
 * Set @naddr with the IPv6 @address and @port pair.
 *
 * Note that @port must be expressed in network byte order, use g_htons() to convert
 * it to network byte order.
 */
void
gst_netaddress_set_ip6_address (GstNetAddress * naddr, guint8 address[16],
    guint16 port)
{
  g_return_if_fail (naddr != NULL);

  naddr->type = GST_NET_TYPE_IP6;
  memcpy (&naddr->address.ip6, address, 16);
  naddr->port = port;
}

/**
 * gst_netaddress_get_net_type:
 * @naddr: a network address
 *
 * Get the type of address stored in @naddr.
 *
 * Returns: the network type stored in @naddr.
 */
GstNetType
gst_netaddress_get_net_type (const GstNetAddress * naddr)
{
  g_return_val_if_fail (naddr != NULL, GST_NET_TYPE_UNKNOWN);

  return naddr->type;
}

/**
 * gst_netaddress_get_ip4_address:
 * @naddr: a network address
 * @address: a location to store the address.
 * @port: a location to store the port.
 *
 * Get the IPv4 address stored in @naddr into @address. This function requires
 * that the address type of @naddr is of type #GST_NET_TYPE_IP4.
 *
 * Note that @port and @address are expressed in network byte order, use
 * g_ntohs() and g_ntohl() to convert them to host order.
 *
 * Returns: TRUE if the address could be retrieved.
 */
gboolean
gst_netaddress_get_ip4_address (const GstNetAddress * naddr, guint32 * address,
    guint16 * port)
{
  g_return_val_if_fail (naddr != NULL, FALSE);

  if (naddr->type == GST_NET_TYPE_UNKNOWN || naddr->type == GST_NET_TYPE_IP6)
    return FALSE;

  if (address)
    *address = naddr->address.ip4;
  if (port)
    *port = naddr->port;

  return TRUE;
}

/**
 * gst_netaddress_get_ip6_address:
 * @naddr: a network address
 * @address: a location to store the result.
 * @port: a location to store the port.
 *
 * Get the IPv6 address stored in @naddr into @address.
 *
 * If @naddr is of type GST_NET_TYPE_IP4, the transitional IP6 address is
 * returned.
 *
 * Note that @port is expressed in network byte order, use g_ntohs() to convert
 * it to host order.
 *
 * Returns: TRUE if the address could be retrieved.
 */
gboolean
gst_netaddress_get_ip6_address (const GstNetAddress * naddr, guint8 address[16],
    guint16 * port)
{
  static guint8 ip4_transition[16] =
      { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF };
  g_return_val_if_fail (naddr != NULL, FALSE);

  if (naddr->type == GST_NET_TYPE_UNKNOWN)
    return FALSE;

  if (address) {
    if (naddr->type == GST_NET_TYPE_IP6) {
      memcpy (address, naddr->address.ip6, 16);
    } else {                    /* naddr->type == GST_NET_TYPE_IP4 */
      memcpy (address, ip4_transition, 12);
      memcpy (address + 12, (guint8 *) & (naddr->address.ip4), 4);
    }
  }
  if (port)
    *port = naddr->port;

  return TRUE;
}

/**
 * gst_netaddress_get_address_bytes:
 * @naddr: a network address
 * @address: a location to store the result.
 * @port: a location to store the port.
 *
 * Get just the address bytes stored in @naddr into @address.
 *
 * Note that @port is expressed in network byte order, use g_ntohs() to convert
 * it to host order. IP4 addresses are also stored in network byte order.
 *
 * Returns: number of bytes actually copied
 *
 * Since: 0.10.22
 */
gint
gst_netaddress_get_address_bytes (const GstNetAddress * naddr,
    guint8 address[16], guint16 * port)
{
  gint ret = 0;

  g_return_val_if_fail (naddr != NULL, FALSE);

  if (naddr->type == GST_NET_TYPE_UNKNOWN)
    return 0;

  if (address) {
    if (naddr->type == GST_NET_TYPE_IP6) {
      memcpy (address, naddr->address.ip6, 16);
      ret = 16;
    } else {                    /* naddr->type == GST_NET_TYPE_IP4 */
      memcpy (address, (guint8 *) & (naddr->address.ip4), 4);
      ret = 4;
    }
  }
  if (port)
    *port = naddr->port;

  return ret;
}

/**
 * gst_netaddress_set_address_bytes:
 * @naddr: a network address
 * @type: the address type (IPv4 or IPV6)
 * @address: a location to store the result.
 * @port: a location to store the port.
 *
 * Set just the address bytes stored in @naddr into @address.
 *
 * Note that @port must be expressed in network byte order, use g_htons() to
 * convert it to network byte order. IP4 address bytes must also be
 * stored in network byte order.
 *
 * Returns: number of bytes actually copied
 *
 * Since: 0.10.22
 */
gint
gst_netaddress_set_address_bytes (GstNetAddress * naddr, GstNetType type,
    guint8 address[16], guint16 port)
{
  gint len = 0;

  g_return_val_if_fail (naddr != NULL, 0);

  naddr->type = type;
  switch (naddr->type) {
    case GST_NET_TYPE_UNKNOWN:
    case GST_NET_TYPE_IP6:
      len = 16;
      memcpy (naddr->address.ip6, address, 16);
      break;
    case GST_NET_TYPE_IP4:
      len = 4;
      memcpy ((guint8 *) & (naddr->address.ip4), address, 4);
      break;
  }

  if (port)
    naddr->port = port;

  return len;
}

/**
 * gst_netaddress_equal:
 * @naddr1: The first #GstNetAddress
 * @naddr2: The second #GstNetAddress
 *
 * Compare two #GstNetAddress structures
 *
 * Returns: TRUE if they are identical, FALSE otherwise
 *
 * Since: 0.10.18
 */
gboolean
gst_netaddress_equal (const GstNetAddress * naddr1,
    const GstNetAddress * naddr2)
{
  g_return_val_if_fail (naddr1 != NULL, FALSE);
  g_return_val_if_fail (naddr2 != NULL, FALSE);

  if (naddr1->type != naddr2->type)
    return FALSE;

  if (naddr1->port != naddr2->port)
    return FALSE;

  switch (naddr1->type) {
    case GST_NET_TYPE_IP4:
      if (naddr1->address.ip4 != naddr2->address.ip4)
        return FALSE;
      break;
    case GST_NET_TYPE_IP6:
      if (memcmp (naddr1->address.ip6, naddr2->address.ip6,
              sizeof (naddr1->address.ip6)))
        return FALSE;
      break;
    default:
      break;
  }
  return TRUE;
}

/**
 * gst_netaddress_to_string:
 * @naddr: a #GstNetAddress
 * @dest: destination
 * @len: len of @dest
 *
 * Copies a string representation of @naddr into @dest. Up to @len bytes are
 * copied.
 *
 * Returns: the number of bytes which would be produced if the buffer was large
 * enough
 *
 * Since: 0.10.24
 */
gint
gst_netaddress_to_string (const GstNetAddress * naddr, gchar * dest, gulong len)
{
  gint result;

  g_return_val_if_fail (naddr != NULL, FALSE);
  g_return_val_if_fail (dest != NULL, FALSE);

  switch (naddr->type) {
    case GST_NET_TYPE_IP4:
    {
      guint32 address;
      guint16 port;

      gst_netaddress_get_ip4_address (naddr, &address, &port);
      address = g_ntohl (address);

      result = g_snprintf (dest, len, "%d.%d.%d.%d:%d", (address >> 24) & 0xff,
          (address >> 16) & 0xff, (address >> 8) & 0xff, address & 0xff,
          g_ntohs (port));
      break;
    }
    case GST_NET_TYPE_IP6:
    {
      guint8 address[16];
      guint16 port;

      gst_netaddress_get_ip6_address (naddr, address, &port);

      result =
          g_snprintf (dest, len, "[%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x]:%d",
          (address[0] << 8) | address[1], (address[2] << 8) | address[3],
          (address[4] << 8) | address[5], (address[6] << 8) | address[7],
          (address[8] << 8) | address[9], (address[10] << 8) | address[11],
          (address[12] << 8) | address[13], (address[14] << 8) | address[15],
          g_ntohs (port));
      break;
    }
    default:
      dest[0] = 0;
      result = 0;
      break;
  }
  return result;
}
