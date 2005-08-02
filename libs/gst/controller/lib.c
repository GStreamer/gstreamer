/*
 * lib.c
 * 
 * New dynamic properties
 *
 */

#include "config.h"
#include <gst/gst.h>

/* library initialisation */

#define GST_CAT_DEFAULT gst_controller_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

/**
 * gst_controller_init:
 * @argc: pointer to the commandline argument count
 * @argv: pointer to the commandline argument values
 *
 * Initializes the use of the controller library. Suggested to be called right
 * after gst_init().
 *
 * Returns: the %TRUE for success.
 */
gboolean
gst_controller_init (int *argc, char ***argv)
{
  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "gstcontroller", 0,
      "dynamic parameter control for gstreamer elements");

  return TRUE;
}
