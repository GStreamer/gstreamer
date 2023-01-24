/* GStreamer
 * Copyright (C) 2012 Wim Taymans <wim.taymans at gmail.com>
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
/**
 * SECTION:rtsp-address-pool
 * @short_description: A pool of network addresses
 * @see_also: #GstRTSPStream, #GstRTSPStreamTransport
 *
 * The #GstRTSPAddressPool is an object that maintains a collection of network
 * addresses. It is used to allocate server ports and server multicast addresses
 * but also to reserve client provided destination addresses.
 *
 * A range of addresses can be added with gst_rtsp_address_pool_add_range().
 * Both multicast and unicast addresses can be added.
 *
 * With gst_rtsp_address_pool_acquire_address() an unused address and port range
 * can be acquired from the pool. With gst_rtsp_address_pool_reserve_address() a
 * specific address can be retrieved. Both methods return a boxed
 * #GstRTSPAddress that should be freed with gst_rtsp_address_free() after
 * usage, which brings the address back into the pool.
 *
 * Last reviewed on 2013-07-16 (1.0.0)
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gio/gio.h>

#include "rtsp-address-pool.h"

/**
 * gst_rtsp_address_copy:
 * @addr: a #GstRTSPAddress
 *
 * Make a copy of @addr.
 *
 * Returns: a copy of @addr.
 */
GstRTSPAddress *
gst_rtsp_address_copy (GstRTSPAddress * addr)
{
  GstRTSPAddress *copy;

  g_return_val_if_fail (addr != NULL, NULL);

  copy = g_memdup2 (addr, sizeof (GstRTSPAddress));
  /* only release to the pool when the original is freed. It's a bit
   * weird but this will do for now as it avoid us to use refcounting. */
  copy->pool = NULL;
  copy->address = g_strdup (copy->address);

  return copy;
}

static void gst_rtsp_address_pool_release_address (GstRTSPAddressPool * pool,
    GstRTSPAddress * addr);

/**
 * gst_rtsp_address_free:
 * @addr: a #GstRTSPAddress
 *
 * Free @addr and releasing it back into the pool when owned by a
 * pool.
 */
void
gst_rtsp_address_free (GstRTSPAddress * addr)
{
  g_return_if_fail (addr != NULL);

  if (addr->pool) {
    /* unrefs the pool and sets it to NULL */
    gst_rtsp_address_pool_release_address (addr->pool, addr);
  }
  g_free (addr->address);
  g_free (addr);
}

G_DEFINE_BOXED_TYPE (GstRTSPAddress, gst_rtsp_address,
    (GBoxedCopyFunc) gst_rtsp_address_copy,
    (GBoxedFreeFunc) gst_rtsp_address_free);

GST_DEBUG_CATEGORY_STATIC (rtsp_address_pool_debug);
#define GST_CAT_DEFAULT rtsp_address_pool_debug

#define GST_RTSP_ADDRESS_POOL_GET_PRIVATE(obj)  \
     (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GST_TYPE_RTSP_ADDRESS_POOL, GstRTSPAddressPoolPrivate))

struct _GstRTSPAddressPoolPrivate
{
  GMutex lock;                  /* protects everything in this struct */
  GList *addresses;
  GList *allocated;

  gboolean has_unicast_addresses;
};

#define ADDR_IS_IPV4(a)      ((a)->size == 4)
#define ADDR_IS_IPV6(a)      ((a)->size == 16)
#define ADDR_IS_EVEN_PORT(a) (((a)->port & 1) == 0)

typedef struct
{
  guint8 bytes[16];
  gsize size;
  guint16 port;
} Addr;

typedef struct
{
  Addr min;
  Addr max;
  guint8 ttl;
} AddrRange;

#define RANGE_IS_SINGLE(r) (memcmp ((r)->min.bytes, (r)->max.bytes, (r)->min.size) == 0)

#define gst_rtsp_address_pool_parent_class parent_class
G_DEFINE_TYPE_WITH_PRIVATE (GstRTSPAddressPool, gst_rtsp_address_pool,
    G_TYPE_OBJECT);

static void gst_rtsp_address_pool_finalize (GObject * obj);

static void
gst_rtsp_address_pool_class_init (GstRTSPAddressPoolClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->finalize = gst_rtsp_address_pool_finalize;

  GST_DEBUG_CATEGORY_INIT (rtsp_address_pool_debug, "rtspaddresspool", 0,
      "GstRTSPAddressPool");
}

static void
gst_rtsp_address_pool_init (GstRTSPAddressPool * pool)
{
  pool->priv = gst_rtsp_address_pool_get_instance_private (pool);

  g_mutex_init (&pool->priv->lock);
}

static void
free_range (AddrRange * range)
{
  g_free (range);
}

static void
gst_rtsp_address_pool_finalize (GObject * obj)
{
  GstRTSPAddressPool *pool;

  pool = GST_RTSP_ADDRESS_POOL (obj);

  g_list_free_full (pool->priv->addresses, (GDestroyNotify) free_range);
  g_list_free_full (pool->priv->allocated, (GDestroyNotify) free_range);
  g_mutex_clear (&pool->priv->lock);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

/**
 * gst_rtsp_address_pool_new:
 *
 * Make a new #GstRTSPAddressPool.
 *
 * Returns: (transfer full): a new #GstRTSPAddressPool
 */
GstRTSPAddressPool *
gst_rtsp_address_pool_new (void)
{
  GstRTSPAddressPool *pool;

  pool = g_object_new (GST_TYPE_RTSP_ADDRESS_POOL, NULL);

  return pool;
}

/**
 * gst_rtsp_address_pool_clear:
 * @pool: a #GstRTSPAddressPool
 *
 * Clear all addresses in @pool. There should be no outstanding
 * allocations.
 */
void
gst_rtsp_address_pool_clear (GstRTSPAddressPool * pool)
{
  GstRTSPAddressPoolPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_ADDRESS_POOL (pool));
  g_return_if_fail (pool->priv->allocated == NULL);

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  g_list_free_full (priv->addresses, (GDestroyNotify) free_range);
  priv->addresses = NULL;
  g_mutex_unlock (&priv->lock);
}

static gboolean
fill_address (const gchar * address, guint16 port, Addr * addr,
    gboolean is_multicast)
{
  GInetAddress *inet;

  inet = g_inet_address_new_from_string (address);
  if (inet == NULL)
    return FALSE;

  if (is_multicast != g_inet_address_get_is_multicast (inet)) {
    g_object_unref (inet);
    return FALSE;
  }

  addr->size = g_inet_address_get_native_size (inet);
  memcpy (addr->bytes, g_inet_address_to_bytes (inet), addr->size);
  g_object_unref (inet);
  addr->port = port;

  return TRUE;
}

static gchar *
get_address_string (Addr * addr)
{
  gchar *res;
  GInetAddress *inet;

  inet = g_inet_address_new_from_bytes (addr->bytes,
      addr->size == 4 ? G_SOCKET_FAMILY_IPV4 : G_SOCKET_FAMILY_IPV6);
  res = g_inet_address_to_string (inet);
  g_object_unref (inet);

  return res;
}

/**
 * gst_rtsp_address_pool_add_range:
 * @pool: a #GstRTSPAddressPool
 * @min_address: a minimum address to add
 * @max_address: a maximum address to add
 * @min_port: the minimum port
 * @max_port: the maximum port
 * @ttl: a TTL or 0 for unicast addresses
 *
 * Adds the addresses from @min_addess to @max_address (inclusive)
 * to @pool. The valid port range for the addresses will be from @min_port to
 * @max_port inclusive.
 *
 * When @ttl is 0, @min_address and @max_address should be unicast addresses.
 * @min_address and @max_address can be set to
 * #GST_RTSP_ADDRESS_POOL_ANY_IPV4 or #GST_RTSP_ADDRESS_POOL_ANY_IPV6 to bind
 * to all available IPv4 or IPv6 addresses.
 *
 * When @ttl > 0, @min_address and @max_address should be multicast addresses.
 *
 * Returns: %TRUE if the addresses could be added.
 */
gboolean
gst_rtsp_address_pool_add_range (GstRTSPAddressPool * pool,
    const gchar * min_address, const gchar * max_address,
    guint16 min_port, guint16 max_port, guint8 ttl)
{
  AddrRange *range;
  GstRTSPAddressPoolPrivate *priv;
  gboolean is_multicast;

  g_return_val_if_fail (GST_IS_RTSP_ADDRESS_POOL (pool), FALSE);
  g_return_val_if_fail (min_port <= max_port, FALSE);

  priv = pool->priv;

  is_multicast = ttl != 0;

  range = g_new0 (AddrRange, 1);

  if (!fill_address (min_address, min_port, &range->min, is_multicast))
    goto invalid;
  if (!fill_address (max_address, max_port, &range->max, is_multicast))
    goto invalid;

  if (range->min.size != range->max.size)
    goto invalid;
  if (memcmp (range->min.bytes, range->max.bytes, range->min.size) > 0)
    goto invalid;

  range->ttl = ttl;

  GST_DEBUG_OBJECT (pool, "adding %s-%s:%u-%u ttl %u", min_address, max_address,
      min_port, max_port, ttl);

  g_mutex_lock (&priv->lock);
  priv->addresses = g_list_prepend (priv->addresses, range);

  if (!is_multicast)
    priv->has_unicast_addresses = TRUE;
  g_mutex_unlock (&priv->lock);

  return TRUE;

  /* ERRORS */
invalid:
  {
    GST_ERROR_OBJECT (pool, "invalid address range %s-%s", min_address,
        max_address);
    g_free (range);
    return FALSE;
  }
}

static void
inc_address (Addr * addr, guint count)
{
  gint i;
  guint carry;

  carry = count;
  for (i = addr->size - 1; i >= 0 && carry > 0; i--) {
    carry += addr->bytes[i];
    addr->bytes[i] = carry & 0xff;
    carry >>= 8;
  }
}

/* tells us the number of addresses between min_addr and max_addr */
static guint
diff_address (Addr * max_addr, Addr * min_addr)
{
  gint i;
  guint result = 0;

  g_return_val_if_fail (min_addr->size == max_addr->size, 0);

  for (i = 0; i < min_addr->size; i++) {
    g_return_val_if_fail (result < (1 << 24), result);

    result <<= 8;
    result += max_addr->bytes[i] - min_addr->bytes[i];
  }

  return result;
}

static AddrRange *
split_range (GstRTSPAddressPool * pool, AddrRange * range, guint skip_addr,
    guint skip_port, gint n_ports)
{
  GstRTSPAddressPoolPrivate *priv = pool->priv;
  AddrRange *temp;

  if (skip_addr) {
    temp = g_memdup2 (range, sizeof (AddrRange));
    memcpy (temp->max.bytes, temp->min.bytes, temp->min.size);
    inc_address (&temp->max, skip_addr - 1);
    priv->addresses = g_list_prepend (priv->addresses, temp);

    inc_address (&range->min, skip_addr);
  }

  if (!RANGE_IS_SINGLE (range)) {
    /* min and max are not the same, we have more than one address. */
    temp = g_memdup2 (range, sizeof (AddrRange));
    /* increment the range min address */
    inc_address (&temp->min, 1);
    /* and store back in pool */
    priv->addresses = g_list_prepend (priv->addresses, temp);

    /* adjust range with only the first address */
    memcpy (range->max.bytes, range->min.bytes, range->min.size);
  }

  /* range now contains only one single address */
  if (skip_port > 0) {
    /* make a range with the skipped ports */
    temp = g_memdup2 (range, sizeof (AddrRange));
    temp->max.port = temp->min.port + skip_port - 1;
    /* and store back in pool */
    priv->addresses = g_list_prepend (priv->addresses, temp);

    /* increment range port */
    range->min.port += skip_port;
  }
  /* range now contains single address with desired port number */
  if (range->max.port - range->min.port + 1 > n_ports) {
    /* make a range with the remaining ports */
    temp = g_memdup2 (range, sizeof (AddrRange));
    temp->min.port += n_ports;
    /* and store back in pool */
    priv->addresses = g_list_prepend (priv->addresses, temp);

    /* and truncate port */
    range->max.port = range->min.port + n_ports - 1;
  }
  return range;
}

/**
 * gst_rtsp_address_pool_acquire_address:
 * @pool: a #GstRTSPAddressPool
 * @flags: flags
 * @n_ports: the amount of ports
 *
 * Take an address and ports from @pool. @flags can be used to control the
 * allocation. @n_ports consecutive ports will be allocated of which the first
 * one can be found in @port.
 *
 * Returns: (nullable): a #GstRTSPAddress that should be freed with
 * gst_rtsp_address_free after use or %NULL when no address could be
 * acquired.
 */
GstRTSPAddress *
gst_rtsp_address_pool_acquire_address (GstRTSPAddressPool * pool,
    GstRTSPAddressFlags flags, gint n_ports)
{
  GstRTSPAddressPoolPrivate *priv;
  GList *walk, *next;
  AddrRange *result;
  GstRTSPAddress *addr;

  g_return_val_if_fail (GST_IS_RTSP_ADDRESS_POOL (pool), NULL);
  g_return_val_if_fail (n_ports > 0, NULL);

  priv = pool->priv;
  result = NULL;
  addr = NULL;

  g_mutex_lock (&priv->lock);
  /* go over available ranges */
  for (walk = priv->addresses; walk; walk = next) {
    AddrRange *range;
    gint ports, skip;

    range = walk->data;
    next = walk->next;

    /* check address type when given */
    if (flags & GST_RTSP_ADDRESS_FLAG_IPV4 && !ADDR_IS_IPV4 (&range->min))
      continue;
    if (flags & GST_RTSP_ADDRESS_FLAG_IPV6 && !ADDR_IS_IPV6 (&range->min))
      continue;
    if (flags & GST_RTSP_ADDRESS_FLAG_MULTICAST && range->ttl == 0)
      continue;
    if (flags & GST_RTSP_ADDRESS_FLAG_UNICAST && range->ttl != 0)
      continue;

    /* check for enough ports */
    ports = range->max.port - range->min.port + 1;
    if (flags & GST_RTSP_ADDRESS_FLAG_EVEN_PORT
        && !ADDR_IS_EVEN_PORT (&range->min))
      skip = 1;
    else
      skip = 0;
    if (ports - skip < n_ports)
      continue;

    /* we found a range, remove from the list */
    priv->addresses = g_list_delete_link (priv->addresses, walk);
    /* now split and exit our loop */
    result = split_range (pool, range, 0, skip, n_ports);
    priv->allocated = g_list_prepend (priv->allocated, result);
    break;
  }
  g_mutex_unlock (&priv->lock);

  if (result) {
    addr = g_new0 (GstRTSPAddress, 1);
    addr->pool = g_object_ref (pool);
    addr->address = get_address_string (&result->min);
    addr->n_ports = n_ports;
    addr->port = result->min.port;
    addr->ttl = result->ttl;
    addr->priv = result;

    GST_DEBUG_OBJECT (pool, "got address %s:%u ttl %u", addr->address,
        addr->port, addr->ttl);
  }

  return addr;
}

/**
 * gst_rtsp_address_pool_release_address:
 * @pool: a #GstRTSPAddressPool
 * @id: an address id
 *
 * Release a previously acquired address (with
 * gst_rtsp_address_pool_acquire_address()) back into @pool.
 */
static void
gst_rtsp_address_pool_release_address (GstRTSPAddressPool * pool,
    GstRTSPAddress * addr)
{
  GstRTSPAddressPoolPrivate *priv;
  GList *find;
  AddrRange *range;

  g_return_if_fail (GST_IS_RTSP_ADDRESS_POOL (pool));
  g_return_if_fail (addr != NULL);
  g_return_if_fail (addr->pool == pool);

  priv = pool->priv;
  range = addr->priv;

  /* we don't want to free twice */
  addr->priv = NULL;
  addr->pool = NULL;

  g_mutex_lock (&priv->lock);
  find = g_list_find (priv->allocated, range);
  if (find == NULL)
    goto not_found;

  priv->allocated = g_list_delete_link (priv->allocated, find);

  /* FIXME, merge and do something clever */
  priv->addresses = g_list_prepend (priv->addresses, range);
  g_mutex_unlock (&priv->lock);

  g_object_unref (pool);

  return;

  /* ERRORS */
not_found:
  {
    g_warning ("Released unknown address %p", addr);
    g_mutex_unlock (&priv->lock);
    return;
  }
}

static void
dump_range (AddrRange * range, GstRTSPAddressPool * pool)
{
  gchar *addr1, *addr2;

  addr1 = get_address_string (&range->min);
  addr2 = get_address_string (&range->max);
  g_print ("  address %s-%s, port %u-%u, ttl %u\n", addr1, addr2,
      range->min.port, range->max.port, range->ttl);
  g_free (addr1);
  g_free (addr2);
}

/**
 * gst_rtsp_address_pool_dump:
 * @pool: a #GstRTSPAddressPool
 *
 * Dump the free and allocated addresses to stdout.
 */
void
gst_rtsp_address_pool_dump (GstRTSPAddressPool * pool)
{
  GstRTSPAddressPoolPrivate *priv;

  g_return_if_fail (GST_IS_RTSP_ADDRESS_POOL (pool));

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  g_print ("free:\n");
  g_list_foreach (priv->addresses, (GFunc) dump_range, pool);
  g_print ("allocated:\n");
  g_list_foreach (priv->allocated, (GFunc) dump_range, pool);
  g_mutex_unlock (&priv->lock);
}

static GList *
find_address_in_ranges (GList * addresses, Addr * addr, guint port,
    guint n_ports, guint ttl)
{
  GList *walk, *next;

  /* go over available ranges */
  for (walk = addresses; walk; walk = next) {
    AddrRange *range;

    range = walk->data;
    next = walk->next;

    /* Not the right type of address */
    if (range->min.size != addr->size)
      continue;

    /* Check that the address is in the interval */
    if (memcmp (range->min.bytes, addr->bytes, addr->size) > 0 ||
        memcmp (range->max.bytes, addr->bytes, addr->size) < 0)
      continue;

    /* Make sure the requested ports are inside the range */
    if (port < range->min.port || port + n_ports - 1 > range->max.port)
      continue;

    if (ttl != range->ttl)
      continue;

    break;
  }

  return walk;
}

/**
 * gst_rtsp_address_pool_reserve_address:
 * @pool: a #GstRTSPAddressPool
 * @ip_address: The IP address to reserve
 * @port: The first port to reserve
 * @n_ports: The number of ports
 * @ttl: The requested ttl
 * @address: (out): storage for a #GstRTSPAddress
 *
 * Take a specific address and ports from @pool. @n_ports consecutive
 * ports will be allocated of which the first one can be found in
 * @port.
 *
 * If @ttl is 0, @address should be a unicast address. If @ttl > 0, @address
 * should be a valid multicast address.
 *
 * Returns: #GST_RTSP_ADDRESS_POOL_OK if an address was reserved. The address
 * is returned in @address and should be freed with gst_rtsp_address_free
 * after use.
 */
GstRTSPAddressPoolResult
gst_rtsp_address_pool_reserve_address (GstRTSPAddressPool * pool,
    const gchar * ip_address, guint port, guint n_ports, guint ttl,
    GstRTSPAddress ** address)
{
  GstRTSPAddressPoolPrivate *priv;
  Addr input_addr;
  GList *list;
  AddrRange *addr_range;
  GstRTSPAddress *addr;
  gboolean is_multicast;
  GstRTSPAddressPoolResult result;

  g_return_val_if_fail (GST_IS_RTSP_ADDRESS_POOL (pool),
      GST_RTSP_ADDRESS_POOL_EINVAL);
  g_return_val_if_fail (ip_address != NULL, GST_RTSP_ADDRESS_POOL_EINVAL);
  g_return_val_if_fail (port > 0, GST_RTSP_ADDRESS_POOL_EINVAL);
  g_return_val_if_fail (n_ports > 0, GST_RTSP_ADDRESS_POOL_EINVAL);
  g_return_val_if_fail (address != NULL, GST_RTSP_ADDRESS_POOL_EINVAL);

  priv = pool->priv;
  addr_range = NULL;
  addr = NULL;
  is_multicast = ttl != 0;

  if (!fill_address (ip_address, port, &input_addr, is_multicast))
    goto invalid;

  g_mutex_lock (&priv->lock);
  list = find_address_in_ranges (priv->addresses, &input_addr, port, n_ports,
      ttl);
  if (list != NULL) {
    AddrRange *range = list->data;
    guint skip_port, skip_addr;

    skip_addr = diff_address (&input_addr, &range->min);
    skip_port = port - range->min.port;

    GST_DEBUG_OBJECT (pool, "diff 0x%08x/%u", skip_addr, skip_port);

    /* we found a range, remove from the list */
    priv->addresses = g_list_delete_link (priv->addresses, list);
    /* now split and exit our loop */
    addr_range = split_range (pool, range, skip_addr, skip_port, n_ports);
    priv->allocated = g_list_prepend (priv->allocated, addr_range);
  }

  if (addr_range) {
    addr = g_new0 (GstRTSPAddress, 1);
    addr->pool = g_object_ref (pool);
    addr->address = get_address_string (&addr_range->min);
    addr->n_ports = n_ports;
    addr->port = addr_range->min.port;
    addr->ttl = addr_range->ttl;
    addr->priv = addr_range;

    result = GST_RTSP_ADDRESS_POOL_OK;
    GST_DEBUG_OBJECT (pool, "reserved address %s:%u ttl %u", addr->address,
        addr->port, addr->ttl);
  } else {
    /* We failed to reserve the address. Check if it was because the address
     * was already in use or if it wasn't in the pool to begin with */
    list = find_address_in_ranges (priv->allocated, &input_addr, port, n_ports,
        ttl);
    if (list != NULL) {
      result = GST_RTSP_ADDRESS_POOL_ERESERVED;
    } else {
      result = GST_RTSP_ADDRESS_POOL_ERANGE;
    }
  }
  g_mutex_unlock (&priv->lock);

  *address = addr;
  return result;

  /* ERRORS */
invalid:
  {
    GST_ERROR_OBJECT (pool, "invalid address %s:%u/%u/%u", ip_address,
        port, n_ports, ttl);
    *address = NULL;
    return GST_RTSP_ADDRESS_POOL_EINVAL;
  }
}

/**
 * gst_rtsp_address_pool_has_unicast_addresses:
 * @pool: a #GstRTSPAddressPool
 *
 * Used to know if the pool includes any unicast addresses.
 *
 * Returns: %TRUE if the pool includes any unicast addresses, %FALSE otherwise
 */

gboolean
gst_rtsp_address_pool_has_unicast_addresses (GstRTSPAddressPool * pool)
{
  GstRTSPAddressPoolPrivate *priv;
  gboolean has_unicast_addresses;

  g_return_val_if_fail (GST_IS_RTSP_ADDRESS_POOL (pool), FALSE);

  priv = pool->priv;

  g_mutex_lock (&priv->lock);
  has_unicast_addresses = priv->has_unicast_addresses;
  g_mutex_unlock (&priv->lock);

  return has_unicast_addresses;
}
