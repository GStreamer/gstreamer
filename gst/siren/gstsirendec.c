/*
 * Siren Decoder Gst Element
 *
 *   @author: Youness Alaoui <kakaroto@kakaroto.homelinux.net>
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
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsirendec.h"

#include <string.h>

GST_DEBUG_CATEGORY (sirendec_debug);
#define GST_CAT_DEFAULT (sirendec_debug)

/* elementfactory information */
static const GstElementDetails gst_siren_dec_details =
GST_ELEMENT_DETAILS (
  "Siren Decoder element",
  "Codec/Decoder/Audio ",
  "Decode streams encoded with the Siren7 codec into 16bit PCM",
  "Youness Alaoui <kakaroto@kakaroto.homelinux.net>");


static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-siren, "
        "dct-length = (int) 320"));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "endianness = (int) 1234, "
        "signed = (boolean) true, "
        "rate = (int) 16000, "
        "channels = (int) 1"));

/* signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};


static GstFlowReturn
gst_siren_dec_chain (GstPad *pad, GstBuffer *buf);

static void gst_siren_dec_dispose (GObject *object);

static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT
    (sirendec_debug, "sirendec", 0, "sirendec");
}

GST_BOILERPLATE_FULL (GstSirenDec, gst_siren_dec, GstElement,
    GST_TYPE_ELEMENT, _do_init);

static void
gst_siren_dec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details (element_class, &gst_siren_dec_details);
}

static void
gst_siren_dec_class_init (GstSirenDecClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG ("Initializing Class");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_siren_dec_dispose);

  GST_DEBUG ("Class Init done");
}

static void
gst_siren_dec_init (GstSirenDec *dec, GstSirenDecClass *klass)
{

  GST_DEBUG_OBJECT (dec, "Initializing");
  dec->decoder = Siren7_NewDecoder (16000);;

  dec->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  dec->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");

  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_dec_chain));

  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->srccaps = gst_static_pad_template_get_caps (&srctemplate);

  GST_DEBUG_OBJECT (dec, "Init done");
}

static void
gst_siren_dec_dispose (GObject *object)
{
  GstSirenDec *dec = GST_SIREN_DEC (object);

  GST_DEBUG_OBJECT (dec, "Disposing");

  if (dec->decoder) {
    Siren7_CloseDecoder (dec->decoder);
    dec->decoder = NULL;
  }

  if (dec->srccaps)
  {
    gst_caps_unref (dec->srccaps);
    dec->srccaps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstFlowReturn
gst_siren_dec_chain (GstPad *pad, GstBuffer *buf)
{
  GstSirenDec *dec = GST_SIREN_DEC (gst_pad_get_parent_element (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *decoded = NULL;
  guint inoffset = 0;
  guint outoffset = 0;
  gint decode_ret = 0;
  guint size = 0;

  GST_LOG_OBJECT (dec, "Decoding buffer of size %d", GST_BUFFER_SIZE (buf));

  size = GST_BUFFER_SIZE (buf) * 16;
  size -= size % 640;

  if (size == 0)
  {
    GST_LOG_OBJECT (dec, "Got buffer smaller than framesize: %u < 40",
        GST_BUFFER_SIZE (buf));
    return GST_FLOW_OK;
  }

  if (GST_BUFFER_SIZE (buf) % 40 != 0)
    GST_LOG_OBJECT (dec, "Got buffer with size not a multiple for frame size,"
        " ignoring last %u bytes", GST_BUFFER_SIZE (buf) % 40);

  ret = gst_pad_alloc_buffer_and_set_caps (dec->srcpad,
      GST_BUFFER_OFFSET (buf) * 16, size, dec->srccaps, &decoded);
  if (ret != GST_FLOW_OK)
    return ret;

  GST_BUFFER_TIMESTAMP (decoded) = GST_BUFFER_TIMESTAMP (buf);

  while((inoffset + 40 <= GST_BUFFER_SIZE (buf)) &&
      ret == GST_FLOW_OK) {

    GST_LOG_OBJECT (dec, "Decoding frame");

    decode_ret = Siren7_DecodeFrame (dec->decoder,
        GST_BUFFER_DATA (buf) + inoffset,
        GST_BUFFER_DATA (decoded) + outoffset);
    if (decode_ret != 0) {
      GST_ERROR_OBJECT (dec, "Siren7_DecodeFrame returned %d", decode_ret);
      ret = GST_FLOW_ERROR;
    }

    inoffset += 40;
    outoffset += 640;
  }

  GST_LOG_OBJECT (dec, "Finished decoding : %d", outoffset);
  if (outoffset != GST_BUFFER_SIZE (decoded)) {
    GST_ERROR_OBJECT (dec,
        "didn't decode enough : offfset (%d) != BUFFER_SIZE (%d)",
        outoffset, GST_BUFFER_SIZE (decoded));
    return GST_FLOW_ERROR;
  }

  ret = gst_pad_push (dec->srcpad, decoded);

  gst_object_unref (dec);

  return ret;
}


gboolean
gst_siren_dec_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "sirendec",
      GST_RANK_MARGINAL, GST_TYPE_SIREN_DEC);
}
