/* GStreamer
 * (c) 2005 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * gstid3demuxbin.c: hack around autoplugging.
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

#ifdef HAVE_CONFIG_G
#include "config.h"
#endif

#include <gst/gst.h>

#define GST_TYPE_ID3DEMUX_BIN \
  (gst_id3demux_bin_get_type())
#define GST_ID3DEMUX_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_ID3DEMUX_BIN, \
			      GstId3DemuxBin))
#define GST_ID3DEMUX_BIN_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_ID3DEMUX_BIN, \
			   GstId3DemuxBinClass))
#define GST_IS_ID3DEMUX_BIN(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_ID3DEMUX_BIN))
#define GST_IS_ID3DEMUX_BIN_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_ID3DEMUX_BIN))

typedef struct _GstId3DemuxBin
{
  GstBin parent;

  /* ghost pads */
  GstPad *srcpad;

  /* kids */
  GstElement *demux, *typefind;
} GstId3DemuxBin;

typedef struct _GstId3DemuxBinClass
{
  GstBinClass parent;
} GstId3DemuxBinClass;

static GstStaticPadTemplate id3demux_bin_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate id3demux_bin_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-id3")
    );

static void gst_id3demux_bin_class_init (GstId3DemuxBinClass * klass);
static void gst_id3demux_bin_base_init (GstId3DemuxBinClass * klass);
static void gst_id3demux_bin_init (GstId3DemuxBin * manager);

static void found_type (GstElement * element, guint probability,
    const GstCaps * caps, gpointer data);
static GstElementStateReturn gst_id3demux_bin_change_state (GstElement *
    element);

static GstBinClass *parent_class;

GType
gst_id3demux_bin_get_type (void)
{
  static GType gst_id3demux_bin_type = 0;

  if (!gst_id3demux_bin_type) {
    static const GTypeInfo gst_id3demux_bin_info = {
      sizeof (GstId3DemuxBinClass),
      (GBaseInitFunc) gst_id3demux_bin_base_init,
      NULL,
      (GClassInitFunc) gst_id3demux_bin_class_init,
      NULL,
      NULL,
      sizeof (GstId3DemuxBin),
      0,
      (GInstanceInitFunc) gst_id3demux_bin_init,
      NULL
    };

    gst_id3demux_bin_type =
        g_type_register_static (GST_TYPE_BIN,
        "GstId3DemuxBin", &gst_id3demux_bin_info, 0);
  }

  return gst_id3demux_bin_type;
}

static void
gst_id3demux_bin_base_init (GstId3DemuxBinClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_id3demux_bin_details =
      GST_ELEMENT_DETAILS ("ID3-demux bin",
      "Generic/Bin",
      "Manages typefinding for an ID3 demuxer",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_set_details (element_class, &gst_id3demux_bin_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&id3demux_bin_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&id3demux_bin_sink_template));
}

static void
gst_id3demux_bin_class_init (GstId3DemuxBinClass * klass)
{
  parent_class = g_type_class_ref (GST_TYPE_BIN);

  GST_ELEMENT_CLASS (klass)->change_state = gst_id3demux_bin_change_state;
}

static void
gst_id3demux_bin_init (GstId3DemuxBin * id3)
{
  id3->demux = gst_element_factory_make ("id3demux", NULL);
  id3->typefind = gst_element_factory_make ("typefind", NULL);

  g_signal_connect (id3->typefind, "have-type", G_CALLBACK (found_type), id3);
  gst_pad_use_explicit_caps (gst_element_get_pad (id3->typefind, "src"));
  gst_element_add_ghost_pad (GST_ELEMENT (id3),
      gst_element_get_pad (id3->demux, "sink"), "sink");
  gst_bin_add_many (GST_BIN (id3), id3->demux, id3->typefind, NULL);
  gst_element_link (id3->demux, id3->typefind);
}

static void
gst_id3demux_bin_remove_pad (GstId3DemuxBin * id3)
{
  if (id3->srcpad) {
    gst_element_remove_pad (GST_ELEMENT (id3), id3->srcpad);
    id3->srcpad = NULL;
  }
}

static void
found_type (GstElement * element, guint probability,
    const GstCaps * caps, gpointer data)
{
  GstId3DemuxBin *id3 = GST_ID3DEMUX_BIN (data);

  /* get rid of old */
  gst_id3demux_bin_remove_pad (id3);

  GST_LOG ("Found type");

  /* add new */
  if (!gst_pad_set_explicit_caps (gst_element_get_pad (id3->typefind,
              "src"), caps)) {
    GST_ELEMENT_ERROR (id3, CORE, NEGOTIATION, (NULL), (NULL));
    return;
  }

  id3->srcpad = gst_ghost_pad_new ("src",
      gst_element_get_pad (id3->typefind, "src"));
  gst_element_add_pad (GST_ELEMENT (id3), id3->srcpad);
}

static GstElementStateReturn
gst_id3demux_bin_change_state (GstElement * element)
{
  GstId3DemuxBin *id3 = GST_ID3DEMUX_BIN (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      gst_id3demux_bin_remove_pad (id3);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
