/* GStreamer
 * Copyright (C) 2015 Sebastian Dröge <sebastian@centricular.com>
 *
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

/* Create a PTP client clock and print times and statistics.
 *
 * When running this from a GStreamer build tree, you will have to set
 * GST_PTP_HELPER to libs/gst/helpers/.libs/gst-ptp-helper and also
 * make sure that it has the right permissions (setuid root or appropriate
 * capabilities
 *
 * You can test this with any PTP compatible clock, e.g. ptpd from here: http://ptpd.sourceforge.net/
 *
 * For testing the accuracy, you can use the PTP reflector available from
 * http://code.centricular.com/ptp-clock-reflector/ or here
 * https://github.com/sdroege/ptp-clock-reflector
 */

#include <gst/gst.h>
#include <gst/net/net.h>

static gint domain = 0;
static gboolean stats = FALSE;

static GOptionEntry opt_entries[] = {
  {"domain", 'd', 0, G_OPTION_ARG_INT, &domain,
      "PTP domain", NULL},
  {"stats", 's', 0, G_OPTION_ARG_NONE, &stats,
      "Print PTP statistics", NULL},
  {NULL}
};

static gboolean
stats_cb (guint8 d, const GstStructure * stats, gpointer user_data)
{
  if (d == domain) {
    gchar *stats_str = gst_structure_to_string (stats);
    g_print ("Got stats: %s\n", stats_str);
    g_free (stats_str);
  }

  return TRUE;
}

gint
main (gint argc, gchar ** argv)
{
  GOptionContext *opt_ctx;
  GstClock *clock;
  GError *err = NULL;

  opt_ctx = g_option_context_new ("- GStreamer PTP clock test app");
  g_option_context_add_main_entries (opt_ctx, opt_entries, NULL);
  g_option_context_add_group (opt_ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (opt_ctx, &argc, &argv, &err))
    g_error ("Error parsing options: %s", err->message);
  g_clear_error (&err);
  g_option_context_free (opt_ctx);

  if (!gst_ptp_init (GST_PTP_CLOCK_ID_NONE, NULL))
    g_error ("failed to init ptp");

  if (stats)
    gst_ptp_statistics_callback_add (stats_cb, NULL, NULL);

  clock = gst_ptp_clock_new ("test-clock", domain);

  gst_clock_wait_for_sync (GST_CLOCK (clock), GST_CLOCK_TIME_NONE);

  while (TRUE) {
    GstClockTime local, remote;
    GstClockTimeDiff diff;

    local = g_get_real_time () * 1000;
    remote = gst_clock_get_time (clock);
    diff = GST_CLOCK_DIFF (local, remote);

    g_print ("local: %" GST_TIME_FORMAT " ptp: %" GST_TIME_FORMAT " diff: %s%"
        GST_TIME_FORMAT "\n", GST_TIME_ARGS (local), GST_TIME_ARGS (remote),
        (diff < 0 ? "-" : " "), GST_TIME_ARGS (ABS (diff)));
    g_usleep (100000);
  }

  return 0;
}
