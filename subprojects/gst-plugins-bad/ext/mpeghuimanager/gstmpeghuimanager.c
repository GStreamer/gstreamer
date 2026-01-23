/*
 * Copyright (C) 2026 Fraunhofer Institute for Integrated Circuits IIS
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>

#include "gstmpeghuimanager.h"

#define MAX_MPEGH_FRAME_SIZE 65536
#define MAX_CONFIG_LENGTH 104226

// Size of persistence memory in bytes
#define PERSISTENCE_BUFFER_SIZE 2048

GST_DEBUG_CATEGORY_STATIC (gst_mpeghuimanager_debug);
#define GST_CAT_DEFAULT gst_mpeghuimanager_debug

enum
{ PROP_0, PROP_UI_EVENT, PROP_CONFIG, PROP_PERSISTENCE };

G_DEFINE_TYPE (GstMpeghUiManager, gst_mpeghuimanager, GST_TYPE_ELEMENT);
GST_ELEMENT_REGISTER_DEFINE (mpeghuimanager, "mpeghuimanager",
    GST_RANK_NONE, GST_TYPE_MPEGHUIMANAGER);

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "profile = (string) baseline, "
        "stream-format = (string) mhas, "
        "framed = (boolean) true, "
        "level = (int) { 1, 2, 3, 4 }, "
        "rate = (int) 48000, " "stream-type = (string) single"));

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-mpeg-h, "
        "profile = (string) baseline, "
        "stream-format = (string) mhas, "
        "framed = (boolean) true, "
        "level = (int) { 1, 2, 3, 4 }, "
        "rate = (int) 48000, " "stream-type = (string) single"));

static void gst_mpeghuimanager_finalize (GObject * object);
static void gst_mpeghuimanager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_mpeghuimanager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_mpeghuimanager_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_mpeghuimanager_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_mpeghuimanager_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static gboolean gst_mpeghuimanager_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static GstFlowReturn gst_mpeghuimanager_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static gboolean gst_mpeghuimanager_start (GstMpeghUiManager * filter);
static gboolean gst_mpeghuimanager_stop (GstMpeghUiManager * filter);

static void
gst_mpeghuimanager_class_init (GstMpeghUiManagerClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_get_property);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_mpeghuimanager_finalize);

  gst_element_class_set_static_metadata (gstelement_class,
      "MPEG-H UI manager plugin", "Filter/Converter/Audio",
      "handles MPEG-H user interactivity",
      "<mpeg-h-techsupport@iis.fraunhofer.de>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  g_object_class_install_property (gobject_class, PROP_CONFIG,
      g_param_spec_string ("config", "Config",
          "MPEG-H Audio Scene Information XML", NULL, G_PARAM_READABLE));

  g_object_class_install_property (gobject_class, PROP_PERSISTENCE,
      g_param_spec_string ("persistencefile", "persistencefile",
          "Persistence file to read from/write to", NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_UI_EVENT,
      g_param_spec_string ("ui-event", "UI Event",
          "MPEG-H Action Event XML message", NULL, G_PARAM_WRITABLE));
}

static void
gst_mpeghuimanager_init (GstMpeghUiManager * filter)
{
  filter->sink_pad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_query_function (filter->sink_pad,
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_sink_query));
  gst_pad_set_event_function (filter->sink_pad,
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_sink_event));
  gst_pad_set_chain_function (filter->sink_pad,
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_chain));
  gst_pad_use_fixed_caps (filter->sink_pad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sink_pad);

  filter->src_pad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_query_function (filter->src_pad,
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_src_query));
  gst_pad_set_event_function (filter->src_pad,
      GST_DEBUG_FUNCPTR (gst_mpeghuimanager_src_event));
  gst_pad_use_fixed_caps (filter->src_pad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->src_pad);

  filter->mpegh_ui_manager = NULL;
  filter->persistence_filename = g_string_new (NULL);
  filter->persistence_memory =
      (gchar *) calloc (PERSISTENCE_BUFFER_SIZE, sizeof (gchar));
  filter->ui_event_queue = g_queue_new ();
  filter->latest_config = g_string_sized_new (MAX_CONFIG_LENGTH);
}

static void
gst_mpeghuimanager_finalize (GObject * object)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (object);
  g_queue_free (filter->ui_event_queue);
  g_string_free (filter->latest_config, FALSE);
  gst_mpeghuimanager_stop (filter);
}

static void
gst_mpeghuimanager_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (object);
  GString *command;

  switch (prop_id) {
    case PROP_UI_EVENT:
      command = g_string_new ((gchar *) g_value_get_string (value));
      g_queue_push_tail (filter->ui_event_queue, command);
      GST_INFO_OBJECT (filter, "adding command %s", command->str);
      break;

    case PROP_PERSISTENCE:
      g_string_free (filter->persistence_filename, FALSE);
      filter->persistence_filename =
          g_string_new ((gchar *) g_value_get_string (value));
      GST_INFO_OBJECT (filter, "setting persistence file to %s",
          filter->persistence_filename->str);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mpeghuimanager_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (object);
  switch (prop_id) {
    case PROP_UI_EVENT:
      break;

    case PROP_PERSISTENCE:
      g_value_set_string (value, filter->persistence_filename->str);
      break;

    case PROP_CONFIG:
      g_value_set_string (value, filter->latest_config->str);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_mpeghuimanager_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_mpeghuimanager_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GST_DEBUG_OBJECT (pad, "enter: sink event = %" GST_PTR_FORMAT,
      (void *) event);
  gboolean handled = FALSE;
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
      handled = gst_pad_push_event (filter->src_pad, event);
      break;
    default:
      handled = gst_pad_event_default (pad, parent, event);
      break;
  }

  return handled;
}

static gboolean
gst_mpeghuimanager_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  return gst_pad_query_default (pad, parent, query);
}

static gboolean
gst_mpeghuimanager_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event)
{
  GST_DEBUG_OBJECT (pad, "enter: src event = %" GST_PTR_FORMAT, event);
  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  return gst_pad_event_default (pad, parent, event);
}

static GstFlowReturn
gst_mpeghuimanager_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstMpeghUiManager *filter = GST_MPEGHUIMANAGER (parent);

  /* lazy initialization of MPEG-H UI manager */
  if (!gst_mpeghuimanager_start (filter)) {
    gst_buffer_unref (buf);
    GST_ELEMENT_ERROR (filter, STREAM, DECODE,
        ("Unable to initialize MPEG-H UI manager."), (NULL));
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (filter, "input %" GST_PTR_FORMAT, buf);

  GstMapInfo srcmap;
  gst_buffer_map (buf, &srcmap, GST_MAP_READ);

  guint inblocksize = gst_buffer_get_size (buf);
  guchar uimanager_mhas_packet[MAX_MPEGH_FRAME_SIZE] = { 0 };
  memcpy (uimanager_mhas_packet, srcmap.data, inblocksize);
  gst_buffer_unmap (buf, &srcmap);

  /* Feed data into the MPEG-H UI manager */
  MPEGH_UI_ERROR ret =
      mpegh_UI_FeedMHAS (filter->mpegh_ui_manager, uimanager_mhas_packet,
      inblocksize);
  if (ret != MPEGH_UI_OK) {
    gst_buffer_unref (buf);
    GST_ERROR_OBJECT (filter, "Error: mpegh_UI_FeedMHAS failed with: %d", ret);
    GST_ELEMENT_ERROR (filter, STREAM, DECODE,
        ("Call to mpegh_UI_FeedMHAS failed with: %d", ret), (NULL));
    return GST_FLOW_ERROR;
  }

  /* Apply all pending UI events */
  while (!g_queue_is_empty (filter->ui_event_queue)) {
    GString *event = g_queue_peek_head (filter->ui_event_queue);
    GST_DEBUG_OBJECT (filter, "process UI event: %s len:%ld\n",
        event->str, event->len);
    guint flagsOut = 0;
    ret =
        mpegh_UI_ApplyXmlAction (filter->mpegh_ui_manager, (gchar *) event->str,
        event->len, &flagsOut);
    if (ret != MPEGH_UI_OK) {
      GST_WARNING_OBJECT (filter,
          "Warning: mpegh_UI_ApplyXmlAction failed with: %d", ret);
    }
    event = g_queue_pop_head (filter->ui_event_queue);
    g_string_free (event, FALSE);
  }

  /* Obtain the UI config from the MPEG-H UI manager */
  gchar xmlSceneStateBuf[MAX_CONFIG_LENGTH];
  guint flagsIn = 0;
  guint flagsOut = 0;
  while (!(flagsOut & MPEGH_UI_NO_CHANGE)) {
    ret = mpegh_UI_GetXmlSceneState (filter->mpegh_ui_manager, xmlSceneStateBuf,
        MAX_CONFIG_LENGTH, flagsIn, &flagsOut);
    if (ret != MPEGH_UI_OK) {
      GST_DEBUG_OBJECT (filter,
          "Warning: mpegh_UI_GetXmlSceneState failed with: %d", ret);
      break;
    }

    GString *currentConfig = g_string_new_take (xmlSceneStateBuf);
    if (currentConfig->len > 0) {
      filter->latest_config =
          g_string_assign (filter->latest_config, currentConfig->str);

      GST_INFO_OBJECT (filter, "ASC with length %lu : %s",
          filter->latest_config->len, filter->latest_config->str);
      GstStructure *structure =
          gst_structure_new ("UIM_OSD_CONFIG", "config", G_TYPE_STRING,
          filter->latest_config->str, "id",
          G_TYPE_UINT64, filter->latest_config->len, NULL);
      GstBus *bus = gst_element_get_bus (GST_ELEMENT (filter));
      GstMessage *msg = gst_message_new_application (NULL, structure);
      gst_bus_post (bus, msg);
      gst_object_unref (bus);
    }
    g_string_free (currentConfig, FALSE);
  }

  /* Update the bitstream data */
  guint outblocksize = inblocksize;
  ret =
      mpegh_UI_UpdateMHAS (filter->mpegh_ui_manager, uimanager_mhas_packet,
      MAX_MPEGH_FRAME_SIZE, &outblocksize);
  if (ret != MPEGH_UI_OK) {
    gst_buffer_unref (buf);
    GST_DEBUG_OBJECT (filter, "Warning: mpegh_UI_UpdateMHAS failed with: %d",
        ret);
    GST_ELEMENT_ERROR (filter, STREAM, DECODE,
        ("Call to mpegh_UI_UpdateMHAS failed with: %d", ret), (NULL));
    return GST_FLOW_ERROR;
  }
  GST_DEBUG_OBJECT (filter, "inblocksize: %d   outblocksize: %d", inblocksize,
      outblocksize);

  GstBuffer *dstbuf = gst_buffer_new_and_alloc (outblocksize);
  GstMapInfo dstmap;
  gst_buffer_map (dstbuf, &dstmap, GST_MAP_WRITE);

  memcpy (dstmap.data, uimanager_mhas_packet, outblocksize);

  GST_BUFFER_PTS (dstbuf) = GST_BUFFER_PTS (buf);
  GST_BUFFER_DTS (dstbuf) = GST_BUFFER_DTS (buf);
  GST_BUFFER_DURATION (dstbuf) = GST_BUFFER_DURATION (buf);
  GST_BUFFER_FLAGS (dstbuf) = GST_BUFFER_FLAGS (buf);
  gst_buffer_unref (buf);

  GST_DEBUG_OBJECT (filter, "output %" GST_PTR_FORMAT, dstbuf);

  gst_buffer_unmap (dstbuf, &dstmap);

  GstFlowReturn res = gst_pad_push (filter->src_pad, dstbuf);
  if (res != GST_FLOW_OK) {
    GST_WARNING_OBJECT (pad, "gst_pad_push failed: %s",
        gst_flow_get_name (res));
    return res;
  }

  return GST_FLOW_OK;
}

static gboolean
gst_mpeghuimanager_start (GstMpeghUiManager * filter)
{
  if (filter->mpegh_ui_manager != NULL) {
    return TRUE;
  }
  // Load persistence data from file (optional)
  if (filter->persistence_filename->len > 0) {
    if (g_file_test (filter->persistence_filename->str, G_FILE_TEST_EXISTS)) {
      GError *error = NULL;
      GIOChannel *persistenceChannel =
          g_io_channel_new_file (filter->persistence_filename->str, "r",
          &error);
      if (persistenceChannel) {
        g_io_channel_set_encoding (persistenceChannel, NULL, &error);
        gsize bytes_read = 0;
        GIOStatus status = g_io_channel_read_chars (persistenceChannel,
            filter->persistence_memory, PERSISTENCE_BUFFER_SIZE, &bytes_read,
            &error);
        if (status != G_IO_STATUS_NORMAL
            && bytes_read != PERSISTENCE_BUFFER_SIZE) {
          GST_WARNING_OBJECT (filter,
              "Warning: Unable to read enough data from persistence file  %s!",
              filter->persistence_filename->str);
          memset (filter->persistence_memory, 0, PERSISTENCE_BUFFER_SIZE);
        }
        g_io_channel_shutdown (persistenceChannel, TRUE, &error);
      }
    } else {
      GST_WARNING_OBJECT (filter,
          "Warning: Persistence input file not found! Creating a new one after processing.");
    }
  }

  filter->mpegh_ui_manager = mpegh_UI_Manager_Open ();
  if (filter->mpegh_ui_manager == NULL) {
    GST_ERROR_OBJECT (filter, "Error: Unable to create UI manager instance!");
    return FALSE;
  }
  // Set persistence data memory
  mpegh_UI_SetPersistenceMemory (filter->mpegh_ui_manager,
      filter->persistence_memory, PERSISTENCE_BUFFER_SIZE);

  return TRUE;
}

static gboolean
gst_mpeghuimanager_stop (GstMpeghUiManager * filter)
{
  gboolean res = TRUE;

  // Save persistence data to file (optional)
  if (filter->persistence_filename->len > 0) {
    gpointer pMemory = NULL;
    guint16 size = 0;

    MPEGH_UI_ERROR err =
        mpegh_UI_GetPersistenceMemory (filter->mpegh_ui_manager, &pMemory,
        &size);
    if (err != MPEGH_UI_OK) {
      GST_WARNING_OBJECT (filter, "Warning: Unable to get persistence memory!");
    }

    if (err == MPEGH_UI_OK && pMemory && size) {
      if (!g_file_test (filter->persistence_filename->str, G_FILE_TEST_EXISTS)) {
        GST_WARNING_OBJECT (filter,
            "Warning: Persistence file not existing -> Trying to create a new file!");
        FILE *file = fopen (filter->persistence_filename->str, "a");
        if (file) {
          fclose (file);
        } else {
          GST_WARNING_OBJECT (filter, "Warning: Failed to create a new file!");
        }
      }
      GError *error = NULL;
      GIOChannel *persistenceChannel =
          g_io_channel_new_file (filter->persistence_filename->str, "w",
          &error);
      if (persistenceChannel) {
        g_io_channel_set_encoding (persistenceChannel, NULL, &error);
        gsize bytes_written = 0;
        GIOStatus status = g_io_channel_write_chars (persistenceChannel,
            filter->persistence_memory, PERSISTENCE_BUFFER_SIZE, &bytes_written,
            &error);
        if (status != G_IO_STATUS_NORMAL
            && bytes_written != PERSISTENCE_BUFFER_SIZE) {
          GST_WARNING_OBJECT (filter,
              "Warning: Unable to write enough data to persistence file!");
        }
        g_io_channel_shutdown (persistenceChannel, TRUE, &error);
      } else {
        GST_WARNING_OBJECT (filter,
            "Warning: Failed to open/create persistence data file at %s with error %s!",
            filter->persistence_filename->str, error->message);
      }
    } else {
      GST_WARNING_OBJECT (filter,
          "Warning: Obtaining persistence memory failed!");
    }
    g_string_free (filter->persistence_filename, FALSE);
  }

  free (filter->persistence_memory);

  mpegh_UI_Manager_Close (filter->mpegh_ui_manager);
  filter->mpegh_ui_manager = NULL;

  return res;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_mpeghuimanager_debug, "mpeghuimanager", 0,
      "MPEG-H UI Manager");
  return gst_element_register (plugin, "mpeghuimanager", GST_RANK_PRIMARY,
      GST_TYPE_MPEGHUIMANAGER);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, mpeghuimanager,
    "MPEG-H UI Manager", plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
