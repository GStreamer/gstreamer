/*
 * Copyright 2026 Google Inc.
 * author: Arthur SC Chan <arthur.chan@adalogics.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <locale.h>

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <gst/gst.h>
#include <gst/tag/tag.h>

/* Tag parsing fuzzing target
 *
 * This application can be compiled with libFuzzer to fuzz
 * various tag parsing functions in gst-plugins-base/gst-libs/gst/tag/
 *
 * The goal is to cover tag parsing code for:
 * - EXIF tags (gstexiftag.c) - TIFF/JPEG metadata
 * - ID3v2 tags (id3v2.c, id3v2frames.c) - MP3/AAC metadata
 * - ID3v1 tags - Legacy MP3 metadata
 * - XMP tags (gstxmptag.c) - XML-based metadata
 * - Vorbis comments (gstvorbistag.c) - Ogg/FLAC/WebM metadata
 *
 * All parsers handle complex binary/text formats with potential for:
 * - Memory corruption
 * - Integer overflows
 * - Buffer overruns
 * - Logic errors
 **/

static void
custom_logger (const gchar * log_domain,
    GLogLevelFlags log_level, const gchar * message, gpointer unused_data)
{
  if (log_level & G_LOG_LEVEL_CRITICAL) {
    g_printerr ("CRITICAL ERROR : %s\n", message);
    abort ();
  } else if (log_level & G_LOG_LEVEL_WARNING) {
    g_printerr ("WARNING : %s\n", message);
  }
}

int
LLVMFuzzerTestOneInput (const guint8 * data, size_t size)
{
  GstBuffer *buffer;
  GstTagList *taglist = NULL;
  static gboolean initialized = FALSE;

  if (!initialized) {
    g_log_set_always_fatal (G_LOG_LEVEL_CRITICAL);
    g_log_set_default_handler (custom_logger, NULL);
    gst_init (NULL, NULL);

    initialized = TRUE;
  }

  if (size == 0)
    return 0;

  /* Test 1: EXIF parsing without TIFF header */
  buffer =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
      size, 0, size, NULL, NULL);
  if (buffer) {
    taglist = gst_tag_list_from_exif_buffer (buffer, G_LITTLE_ENDIAN, 0);
    if (taglist != NULL) {
      gst_tag_list_unref (taglist);
      taglist = NULL;
    }
    gst_buffer_unref (buffer);
  }

  /* Test 2: EXIF parsing with TIFF header */
  buffer =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
      size, 0, size, NULL, NULL);
  if (buffer) {
    taglist = gst_tag_list_from_exif_buffer_with_tiff_header (buffer);
    if (taglist != NULL) {
      gst_tag_list_unref (taglist);
      taglist = NULL;
    }
    gst_buffer_unref (buffer);
  }

  /* Test 3: ID3v2 tag parsing */
  buffer =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
      size, 0, size, NULL, NULL);
  if (buffer) {
    taglist = gst_tag_list_from_id3v2_tag (buffer);
    if (taglist != NULL) {
      gst_tag_list_unref (taglist);
      taglist = NULL;
    }
    gst_buffer_unref (buffer);
  }

  /* Test 4: ID3v1 tag parsing (if size matches) */
  if (size >= 128) {
    taglist = gst_tag_list_new_from_id3v1 (data);
    if (taglist != NULL) {
      gst_tag_list_unref (taglist);
      taglist = NULL;
    }
  }

  /* Test 5: XMP tag parsing */
  buffer =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
      size, 0, size, NULL, NULL);
  if (buffer) {
    taglist = gst_tag_list_from_xmp_buffer (buffer);
    if (taglist != NULL) {
      gst_tag_list_unref (taglist);
      taglist = NULL;
    }
    gst_buffer_unref (buffer);
  }

  /* Test 6: Vorbis comment parsing */
  buffer =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, (gpointer) data,
      size, 0, size, NULL, NULL);
  if (buffer) {
    taglist = gst_tag_list_from_vorbiscomment_buffer (buffer, NULL, 0, NULL);
    if (taglist != NULL) {
      gst_tag_list_unref (taglist);
      taglist = NULL;
    }
    gst_buffer_unref (buffer);
  }

  return 0;
}
