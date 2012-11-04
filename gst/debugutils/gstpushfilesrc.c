/* GStreamer Push File Source
 * Copyright (C) <2007> Tim-Philipp Müller <tim centricular net>
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
 * SECTION:element-pushfilesrc
 * @see_also: filesrc
 *
 * This element is only useful for debugging purposes. It implements an URI
 * protocol handler for the 'pushfile' protocol and behaves like a file source
 * element that cannot be activated in pull-mode. This makes it very easy to
 * debug demuxers or decoders that can operate both pull and push-based in
 * connection with the playbin element (which creates a source based on the
 * URI passed).
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -m playbin uri=pushfile:///home/you/some/file.ogg
 * ]| This plays back the given file using playbin, with the demuxer operating
 * push-based.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpushfilesrc.h"

#include <gst/gst.h>

GST_DEBUG_CATEGORY_STATIC (pushfilesrc_debug);
#define GST_CAT_DEFAULT pushfilesrc_debug

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static void gst_push_file_src_uri_handler_init (gpointer g_iface,
    gpointer iface_data);

#define gst_push_file_src_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstPushFileSrc, gst_push_file_src, GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
        gst_push_file_src_uri_handler_init));

static void
gst_push_file_src_dispose (GObject * obj)
{
  GstPushFileSrc *src = GST_PUSH_FILE_SRC (obj);

  if (src->srcpad) {
    gst_element_remove_pad (GST_ELEMENT (src), src->srcpad);
    src->srcpad = NULL;
  }
  if (src->filesrc) {
    gst_bin_remove (GST_BIN (src), src->filesrc);
    src->filesrc = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (obj);
}

static void
gst_push_file_src_class_init (GstPushFileSrcClass * g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (g_class);
  element_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (pushfilesrc_debug, "pushfilesrc", 0,
      "pushfilesrc element");

  gobject_class->dispose = gst_push_file_src_dispose;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));

  gst_element_class_set_static_metadata (element_class, "Push File Source",
      "Testing",
      "Implements pushfile:// URI-handler for push-based file access",
      "Tim-Philipp Müller <tim centricular net>");
}

static gboolean
gst_push_file_src_ghostpad_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  gboolean res;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SCHEDULING:
      gst_query_set_scheduling (query, GST_SCHEDULING_FLAG_SEEKABLE, 1, -1, 0);
      gst_query_add_scheduling_mode (query, GST_PAD_MODE_PUSH);
      res = TRUE;
      break;
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }
  return res;
}

static void
gst_push_file_src_init (GstPushFileSrc * src)
{
  src->filesrc = gst_element_factory_make ("filesrc", "real-filesrc");
  if (src->filesrc) {
    GstPad *pad;

    gst_bin_add (GST_BIN (src), src->filesrc);
    pad = gst_element_get_static_pad (src->filesrc, "src");
    g_assert (pad != NULL);
    src->srcpad = gst_ghost_pad_new ("src", pad);
    /* FIXME^H^HCORE: try pushfile:///foo/bar.ext ! typefind ! fakesink without
     * this and watch core bugginess (some pad stays in flushing state) */
    gst_pad_set_query_function (src->srcpad,
        GST_DEBUG_FUNCPTR (gst_push_file_src_ghostpad_query));
    gst_element_add_pad (GST_ELEMENT (src), src->srcpad);
    gst_object_unref (pad);
  }
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
gst_push_file_src_uri_get_type (GType type)
{
  return GST_URI_SRC;
}

static const gchar *const *
gst_push_file_src_uri_get_protocols (GType type)
{
  static const gchar *protocols[] = { "pushfile", NULL };

  return protocols;
}

static gchar *
gst_push_file_src_uri_get_uri (GstURIHandler * handler)
{
  GstPushFileSrc *src = GST_PUSH_FILE_SRC (handler);
  gchar *fileuri, *pushfileuri;

  if (src->filesrc == NULL)
    return NULL;

  fileuri = gst_uri_handler_get_uri (GST_URI_HANDLER (src->filesrc));;
  if (fileuri == NULL)
    return NULL;
  pushfileuri = g_strconcat ("push", fileuri, NULL);
  g_free (fileuri);

  return pushfileuri;
}

static gboolean
gst_push_file_src_uri_set_uri (GstURIHandler * handler, const gchar * uri,
    GError ** error)
{
  GstPushFileSrc *src = GST_PUSH_FILE_SRC (handler);

  if (src->filesrc == NULL) {
    g_set_error_literal (error, GST_URI_ERROR, GST_URI_ERROR_BAD_STATE,
        "Could not create file source element");
    return FALSE;
  }

  /* skip 'push' bit */
  return gst_uri_handler_set_uri (GST_URI_HANDLER (src->filesrc), uri + 4,
      error);
}

static void
gst_push_file_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
  GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

  iface->get_type = gst_push_file_src_uri_get_type;
  iface->get_protocols = gst_push_file_src_uri_get_protocols;
  iface->get_uri = gst_push_file_src_uri_get_uri;
  iface->set_uri = gst_push_file_src_uri_set_uri;
}
