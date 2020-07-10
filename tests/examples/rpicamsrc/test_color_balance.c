/*
 * Copyright (c) 2015, Igalia S.L
 *     Author: Philippe Normand <philn@igalia.com>
 * Licence: LGPL. (See COPYING.LGPL)
 */

#include <glib.h>
#include <gst/gst.h>
#include <gst/video/colorbalance.h>

#define CONTROL_SATURATION 1
#define CONTROL_BRIGHTNESS 1
#define CONTROL_CONTRAST 1

#define PIPELINE "rpicamsrc name=src preview=0 fullscreen=0 ! h264parse ! omxh264dec ! glimagesink sync=0"

#define declare_value(name, value)      \
    static gint current_##name = value; \
    static gboolean incrementing_##name = TRUE;

#if CONTROL_SATURATION
declare_value (SATURATION, 50);
#endif
#if CONTROL_BRIGHTNESS
declare_value (BRIGHTNESS, 50);
#endif
#if CONTROL_CONTRAST
declare_value (CONTRAST, 0);
#endif

#define update(name, channel, current_value)              \
    if (!g_strcmp0(channel->label, #name)) {              \
        if (current_value >= channel->max_value)          \
            incrementing_##name = FALSE;                  \
        else if (current_value <= channel->min_value)     \
            incrementing_##name = TRUE;                   \
        current_##name += incrementing_##name ? 10 : -10; \
        g_print("new " #name ": %d\n", current_##name);   \
        return current_##name;                            \
    }

static gint
compute_value (GstColorBalanceChannel * channel, gint current_value)
{
#if CONTROL_SATURATION
  update (SATURATION, channel, current_value);
#endif
#if CONTROL_BRIGHTNESS
  update (BRIGHTNESS, channel, current_value);
#endif
#if CONTROL_CONTRAST
  update (CONTRAST, channel, current_value);
#endif
  return current_value;
}

static gboolean
process (gpointer data)
{
  GstColorBalance *balance = (GstColorBalance *) data;
  const GList *controls;
  GstColorBalanceChannel *channel;
  const GList *item;
  gint index, new_value, current_value;

  controls = gst_color_balance_list_channels (balance);

  if (controls == NULL) {
    g_printerr ("There is no list of colorbalance controls\n");
    return G_SOURCE_REMOVE;
  }

  for (item = controls, index = 0; item != NULL; item = item->next, ++index) {
    channel = item->data;
    current_value = gst_color_balance_get_value (balance, channel);
    new_value = compute_value (channel, current_value);
    gst_color_balance_set_value (balance, channel, new_value);
  }

  return G_SOURCE_CONTINUE;
}

int
main (int argc, char **argv)
{
  GMainLoop *loop;
  GstElement *pipeline;
  GError *error = NULL;
  GstElement *src;
  GstColorBalance *balance;

  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_parse_launch (PIPELINE, &error);
  if (error != NULL) {
    g_printerr ("Error parsing '%s': %s", PIPELINE, error->message);
    g_error_free (error);
    return -1;
  }

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  if (!src) {
    g_printerr ("Source element not found\n");
    return -2;
  }

  balance = GST_COLOR_BALANCE (src);
  g_timeout_add_seconds (1, process, balance);
  g_main_loop_run (loop);

  gst_object_unref (src);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  return 0;
}
