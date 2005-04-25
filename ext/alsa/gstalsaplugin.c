/*
 * Copyright (C) 2001 CodeFactory AB
 * Copyright (C) 2001 Thomas Nyberg <thomas@codefactory.se>
 * Copyright (C) 2001-2002 Andy Wingo <apwingo@eos.ncsu.edu>
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstalsasink.h"
#include "gstalsasrc.h"
#include "gstalsamixer.h"

GST_DEBUG_CATEGORY (alsa_debug);

/* ALSA debugging wrapper */
static void
gst_alsa_error_wrapper (const char *file, int line, const char *function,
    int err, const char *fmt, ...)
{
#ifndef GST_DISABLE_GST_DEBUG
  va_list args;
  gchar *str;

  va_start (args, fmt);
  str = g_strdup_vprintf (fmt, args);
  va_end (args);
  /* FIXME: use GST_LEVEL_ERROR here? Currently warning is used because we're 
   * able to catch enough of the errors that would be printed otherwise
   */
  gst_debug_log (alsa_debug, GST_LEVEL_WARNING, file, function, line, NULL,
      "alsalib error: %s%s%s", str, err ? ": " : "",
      err ? snd_strerror (err) : "");
  g_free (str);
#endif
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  int err;

  if (!gst_element_register (plugin, "alsamixer", GST_RANK_NONE,
          GST_TYPE_ALSA_MIXER))
    return FALSE;
  if (!gst_element_register (plugin, "alsasrc", GST_RANK_NONE,
          GST_TYPE_ALSA_SRC))
    return FALSE;
  if (!gst_element_register (plugin, "alsasink", GST_RANK_NONE,
          GST_TYPE_ALSA_SINK))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (alsa_debug, "alsa", 0, "alsa plugins");
  err = snd_lib_error_set_handler (gst_alsa_error_wrapper);
  if (err != 0)
    GST_WARNING ("failed to set alsa error handler");

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "alsa",
    "ALSA plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
