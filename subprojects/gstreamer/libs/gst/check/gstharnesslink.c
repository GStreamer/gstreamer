/* GstHarnessLink - A ref counted class arbitrating access to a
 * pad harness in a thread-safe manner.
 *
 * Copyright (C) 2023 Igalia S.L.
 * Copyright (C) 2023 Metrological
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstharnesslink.h"

/* The HARNESS_LINK association inside a GstPad will store a pointer to
 * GstHarnessLink, which can be used to read and atomically lock the harness
 * while in use. */
#define HARNESS_LINK "harness-link"

struct _GstHarnessLink
{
  /* rw_lock will be locked for writing for tearing down the harness,
   * and locked for reading for any other use. The goal is to allow simultaneous
   * access to the harness from multiple threads while guaranteeing that the
   * resources of harness won't be freed during use. */
  GRWLock rw_lock;
  GstHarness *harness;
};

static void
gst_harness_link_init (GstHarnessLink * link)
{
  g_rw_lock_init (&link->rw_lock);
}

static void
gst_harness_link_dispose (GstHarnessLink * link)
{
  gboolean lock_acquired = g_rw_lock_writer_trylock (&link->rw_lock);
  if (lock_acquired)
    g_rw_lock_writer_unlock (&link->rw_lock);
  else
    g_critical
        ("GstHarnessLink was about to be disposed while having the lock in use.");

  g_rw_lock_clear (&link->rw_lock);
}

static GstHarnessLink *
gst_harness_link_new (void)
{
  GstHarnessLink *link = g_atomic_rc_box_new0 (GstHarnessLink);
  gst_harness_link_init (link);
  return link;
}

static GstHarnessLink *
gst_harness_link_ref (GstHarnessLink * link)
{
  return g_atomic_rc_box_acquire (link);
}

static void
gst_harness_link_unref (GstHarnessLink * link)
{
  g_atomic_rc_box_release_full (link,
      (GDestroyNotify) gst_harness_link_dispose);
}

/**
 * gst_harness_pad_link_set: (skip)
 * @pad: (transfer none): The pad that will be associated with a #GstHarness.
 * @harness: (transfer none): The #GstHarness that will be associated with the
 * pad.
 *
 * Creates a new #GstHarnessLink pointing to the provided @harness and
 * associates it to the provided @pad.
 *
 * Once this association is set, the #GstHarness can be obtained using
 * @gst_harness_pad_link_lock, which will also lock it until
 * @gst_harness_link_unlock is called to prevent the #GstHarness from being
 * destroyed while in use.
 */
void
gst_harness_pad_link_set (GstPad * pad, GstHarness * harness)
{
  GstHarnessLink *link = gst_harness_link_new ();
  link->harness = harness;
  // The pad will own a reference to GstHarnessLink.
  g_object_set_data_full (G_OBJECT (pad), HARNESS_LINK,
      link, (GDestroyNotify) gst_harness_link_unref);
}

static gpointer
_gst_harness_link_dup_func (gpointer harness_link, gpointer user_data)
{
  if (harness_link)
    gst_harness_link_ref (harness_link);
  return harness_link;
}

/**
 * gst_harness_pad_link_lock: (skip)
 * @pad: (transfer none): A #GstPad that has been at one point associated to a
 * #GstHarness.
 * @dst_harness: (transfer none): An address where the pointer to #GstHarness
 * will be placed.
 *
 * Find a #GstHarness associated with this @pad and place a pointer to it on
 * @dst_harness, locking it to prevent it being destroyed while in use.
 *
 * Both @dst_harness and the return value will be set to %NULL if the @pad is no
 * longer linked to a GstHarness. Generally user code will need to handle this
 * gracefully.
 *
 * Call @gst_harness_link_unlock once you're done using #GstHarness.
 *
 * Locking the link in this manner is reentrant: it is valid to lock the pad
 * link more than once from the same thread as long as @gst_harness_link_unlock
 * is called after exactly that many times.
 *
 * Returns: (transfer full) (nullable): a #GstHarnessLink object that you must
 * pass to @gst_harness_link_unlock after being done with the #GstHarness, or
 * %NULL if the link has been torn down at this point.
 */
GstHarnessLink *
gst_harness_pad_link_lock (GstPad * pad, GstHarness ** dst_harness)
{
  // g_object_dup_data() will call _gst_harness_link_dup_func() while holding
  // the mutex of the GObject association table. This guarantees that the
  // GstHarnessLink is not destroyed between the time we get the pointer to it
  // and increase its refcount.
  GstHarnessLink *link = g_object_dup_data (G_OBJECT (pad), HARNESS_LINK,
      _gst_harness_link_dup_func, NULL);
  if (!link) {
    // There is no longer a link between this pad and a GstHarness, as there is
    // no associated GstHarnessLink.
    *dst_harness = NULL;
    return NULL;
  }
  g_rw_lock_reader_lock (&link->rw_lock);
  if ((*dst_harness = link->harness)) {
    // This GstHarnessLink has a valid link to GstHarness and will remain valid
    // for at least as long as the user holds the lock.
    return link;
  } else {
    // This GstHarnessLink has been torn down, it no longer points to a
    // GstHarness. This will happen if we lock the link just after another
    // thread torn it down. The GstHarnessLink will stay alive for a little
    // longer until its refcount runs out.
    g_rw_lock_reader_unlock (&link->rw_lock);
    gst_harness_link_unref (link);
    return NULL;
  }
}

/**
 * gst_harness_link_unlock: (skip)
 * @link: (nullable) (transfer full): A #GstHarnessLink.
 *
 * Release the lock of the harness link for this particular thread.
 *
 * Whenever @gst_harness_pad_link_lock returns non-NULL this function must be
 * called after the caller has finished use of the #GstHarness.
 *
 * The harness data must not be accessed after this function is called, as it is
 * no longer guaranteed not to be destroyed.
 *
 * For convenience, the function will accept %NULL, in which case it will do
 * nothing.
 */
void
gst_harness_link_unlock (GstHarnessLink * link)
{
  if (!link)
    return;

  g_rw_lock_reader_unlock (&link->rw_lock);
  gst_harness_link_unref (link);
}

/**
 * gst_harness_pad_link_tear_down: (skip)
 * @pad: (transfer none): A #GstPad that has been at one point associated to a
 * #GstHarness.
 *
 * Reset the link to the harness. Further calls to @gst_harness_pad_link_lock
 * will return NULL.
 *
 * This function will block until every thread that successfully locked the
 * harness link with @gst_harness_pad_link_lock has unlocked it with
 * @gst_harness_link_unlock.
 */
void
gst_harness_pad_link_tear_down (GstPad * pad)
{
  // Steal the reference from the pad. This is still synchronized with
  // g_object_dup_data().
  GstHarnessLink *link = g_object_steal_data (G_OBJECT (pad), HARNESS_LINK);
  g_return_if_fail (link != NULL);

  // Take the lock for writing, which will wait for all threads that have locked
  // the harness and will block future lock attempts until we unlock.
  g_rw_lock_writer_lock (&link->rw_lock);
  link->harness = NULL;
  g_rw_lock_writer_unlock (&link->rw_lock);

  // Unref the reference. In the likely case that no other thread has just done
  // g_object_dup_data() and has therefore increase the refcount, this will be
  // the last reference and terminate the GstHarnessLink.
  gst_harness_link_unref (link);

  // Even in the case where there is a remaining reference to GstHarnessLink in
  // a different thread, the GstHarness pointer has been cleared at this point,
  // so the caller thread can safely tear down the GstHarness.
}
