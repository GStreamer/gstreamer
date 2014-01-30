/* GStreamer
 * Copyright (C) <2013> Wim Taymans <wim.taymans@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <gst/gst.h>
#include "gst/glib-compat-private.h"

#define BUFFER_SIZE (1400)

gint
main (gint argc, gchar * argv[])
{
  gint i;
  GstBuffer *tmp;
  GstBufferPool *pool;
  GstClockTime start, end;
  GstClockTimeDiff dur1, dur2;
  guint64 nbuffers;
  GstStructure *conf;

  gst_init (&argc, &argv);

  if (argc != 2) {
    g_print ("usage: %s <nbuffers>\n", argv[0]);
    exit (-1);
  }

  nbuffers = atoi (argv[1]);

  if (nbuffers <= 0) {
    g_print ("number of buffers must be greater than 0\n");
    exit (-3);
  }

  /* Let's just make sure the GstBufferClass is loaded ... */
  tmp = gst_buffer_new ();
  gst_buffer_unref (tmp);

  pool = gst_buffer_pool_new ();

  conf = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (conf, NULL, BUFFER_SIZE, 0, 0);
  gst_buffer_pool_set_config (pool, conf);

  gst_buffer_pool_set_active (pool, TRUE);

  /* allocate buffers directly */
  start = gst_util_get_timestamp ();
  for (i = 0; i < nbuffers; i++) {
    tmp = gst_buffer_new_allocate (NULL, BUFFER_SIZE, NULL);
    gst_buffer_unref (tmp);
  }
  end = gst_util_get_timestamp ();
  dur1 = GST_CLOCK_DIFF (start, end);
  g_print ("*** total %" GST_TIME_FORMAT " - average %" GST_TIME_FORMAT
      "  - Done creating %" G_GUINT64_FORMAT " fresh buffers\n",
      GST_TIME_ARGS (dur1), GST_TIME_ARGS (dur1 / nbuffers), nbuffers);

  /* allocate buffers from the pool */
  start = gst_util_get_timestamp ();
  for (i = 0; i < nbuffers; i++) {
    gst_buffer_pool_acquire_buffer (pool, &tmp, NULL);
    gst_buffer_unref (tmp);
  }
  end = gst_util_get_timestamp ();
  dur2 = GST_CLOCK_DIFF (start, end);
  g_print ("*** total %" GST_TIME_FORMAT " - average %" GST_TIME_FORMAT
      "  - Done creating %" G_GUINT64_FORMAT " pooled buffers\n",
      GST_TIME_ARGS (dur2), GST_TIME_ARGS (dur2 / nbuffers), nbuffers);

  g_print ("*** speedup %6.4lf\n", ((gdouble) dur1 / (gdouble) dur2));

  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);

  return 0;
}
