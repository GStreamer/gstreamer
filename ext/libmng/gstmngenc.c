/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * Filter:
 * Copyright (C) 2000 Donald A. Graft
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include "gstmngenc.h"
#include <gst/video/video.h>

#define MAX_HEIGHT              4096

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_SNAPSHOT        TRUE

enum
{
  ARG_0
};

static void gst_mng_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_mng_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstFlowReturn gst_mng_enc_chain (GstPad * pad, GstBuffer * buf);

GstPadTemplate *mngenc_src_template, *mngenc_sink_template;

GST_BOILERPLATE (GstMngEnc, gst_mng_enc, GstElement, GST_TYPE_ELEMENT);

static GstCaps *
mng_caps_factory (void)
{
  return gst_caps_new_simple ("video/x-mng",
      "width", GST_TYPE_INT_RANGE, 16, 4096,
      "height", GST_TYPE_INT_RANGE, 16, 4096,
      "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
}


static GstCaps *
raw_caps_factory (void)
{
  return gst_caps_from_string (GST_VIDEO_CAPS_RGB);
}

static void
gst_mng_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstCaps *raw_caps, *mng_caps;

  raw_caps = raw_caps_factory ();
  mng_caps = mng_caps_factory ();

  mngenc_sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, raw_caps);

  mngenc_src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, mng_caps);

  gst_element_class_add_pad_template (element_class, mngenc_sink_template);
  gst_element_class_add_pad_template (element_class, mngenc_src_template);
  gst_element_class_set_details_simple (element_class, "MNG video encoder",
      "Codec/Encoder/Video",
      "Encode a video frame to an .mng video", "Wim Taymans <wim@fluendo.com>");
}

static void
gst_mng_enc_class_init (GstMngEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->get_property = gst_mng_enc_get_property;
  gobject_class->set_property = gst_mng_enc_set_property;
}

static gboolean
gst_mng_enc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstMngEnc *mngenc;
  gdouble fps;
  GstStructure *structure;

  mngenc = GST_MNG_ENC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &mngenc->width);
  gst_structure_get_int (structure, "height", &mngenc->height);
  gst_structure_get_double (structure, "framerate", &fps);
  gst_structure_get_int (structure, "bpp", &mngenc->bpp);

  caps = gst_caps_new_simple ("video/x-mng",
      "framerate", G_TYPE_DOUBLE, fps,
      "width", G_TYPE_INT, mngenc->width,
      "height", G_TYPE_INT, mngenc->height, NULL);

  gst_pad_set_caps (mngenc->srcpad, caps);
  gst_caps_unref (caps);

  gst_object_unref (mngenc);

  return TRUE;
}

static void
gst_mng_enc_init (GstMngEnc * mngenc, GstMngEncClass * gclass)
{
  mngenc->sinkpad = gst_pad_new_from_template (mngenc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (mngenc), mngenc->sinkpad);

  mngenc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (mngenc), mngenc->srcpad);

  gst_pad_set_chain_function (mngenc->sinkpad, gst_mng_enc_chain);
  gst_pad_set_setcaps_function (mngenc->sinkpad, gst_mng_enc_sink_setcaps);
}

static GstFlowReturn
gst_mng_enc_chain (GstPad * pad, GstBuffer * buf)
{
  GstMngEnc *mngenc;

  mngenc = GST_MNG_ENC (gst_pad_get_parent (pad));

  /* FIXME, do something here */

  gst_buffer_unref (buf);
  gst_object_unref (mngenc);

  return GST_FLOW_NOT_SUPPORTED;
}


static void
gst_mng_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstMngEnc *mngenc;

  mngenc = GST_MNG_ENC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_mng_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstMngEnc *mngenc;

  mngenc = GST_MNG_ENC (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
