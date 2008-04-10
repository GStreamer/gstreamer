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



static void gst_siren_dec_dispose (GObject *object);

static GstFlowReturn gst_siren_dec_transform (GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean gst_siren_dec_transform_size (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, guint size,
    GstCaps *othercaps, guint *othersize);
static GstCaps * gst_siren_dec_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_siren_dec_start (GstBaseTransform *trans);
static gboolean gst_siren_dec_stop (GstBaseTransform *trans);


static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT
    (sirendec_debug, "sirendec", 0, "sirendec");
}

GST_BOILERPLATE_FULL (GstSirenDec, gst_siren_dec, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);

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
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG ("Initializing Class");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_siren_dec_dispose);

  gstbasetransform_class->transform =
      GST_DEBUG_FUNCPTR (gst_siren_dec_transform);
  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_siren_dec_transform_caps);
  gstbasetransform_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_siren_dec_transform_size);
  gstbasetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_siren_dec_start);
  gstbasetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_siren_dec_stop);

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG ("Class Init done");
}

static void
gst_siren_dec_init (GstSirenDec *dec, GstSirenDecClass *klass)
{

  GST_DEBUG ("Initializing");
  dec->decoder = NULL;
  dec->adapter = gst_adapter_new ();

  GST_DEBUG ("Init done");
}

static void
gst_siren_dec_dispose (GObject *object)
{
  GstSirenDec *dec = GST_SIREN_DEC (object);

  GST_DEBUG ("Disposing");

  if (dec->decoder) {
    Siren7_CloseDecoder (dec->decoder);
    dec->decoder = NULL;
  }
  if (dec->adapter) {
    g_object_unref (dec->adapter);
    dec->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);

  GST_DEBUG ("Dispose done");

}

static gboolean
gst_siren_dec_start (GstBaseTransform *trans)
{
  GstSirenDec *dec = GST_SIREN_DEC (trans);

  GST_DEBUG ("Start");

  if (dec->decoder) {
    Siren7_CloseDecoder (dec->decoder);
    dec->decoder = NULL;
  }
  dec->decoder = Siren7_NewDecoder (16000);
  gst_adapter_clear (dec->adapter);

  return dec->decoder != NULL;
}



static gboolean
gst_siren_dec_stop (GstBaseTransform *trans)
{
  GstSirenDec *dec = GST_SIREN_DEC (trans);

  GST_DEBUG ("Stop");

  if (dec->decoder) {
    Siren7_CloseDecoder (dec->decoder);
    dec->decoder = NULL;
  }
  gst_adapter_clear (dec->adapter);
  return TRUE;
}


static gboolean gst_siren_dec_transform_size (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, guint size,
    GstCaps *othercaps, guint *othersize)
{
  GstSirenDec *dec = GST_SIREN_DEC (trans);
  GstStructure *structure = NULL;
  const gchar *in_name = NULL;
  const gchar *out_name = NULL;
  gboolean decoding;

  if (caps != NULL && othercaps != NULL) {
    structure = gst_caps_get_structure (caps, 0);
    in_name = gst_structure_get_name (structure);
    structure = gst_caps_get_structure (othercaps, 0);
    out_name = gst_structure_get_name (structure);
  }

  GST_DEBUG ("Transform size from caps '%s' to '%s'", in_name, out_name);

  if (in_name == NULL || out_name == NULL) {
    if (direction == GST_PAD_SINK)
      decoding = TRUE;
    else
      decoding = FALSE;
  } else if (strcmp (in_name, "audio/x-raw-int") == 0 &&
      strcmp (out_name, "audio/x-siren") == 0 ) {
    decoding = FALSE;
  } else if (strcmp (in_name, "audio/x-siren") == 0 &&
      strcmp (out_name, "audio/x-raw-int") == 0 ) {
    decoding = TRUE;
  } else {
    GST_DEBUG ("Unknown in/out caps");
    return FALSE;
  }

  if (decoding) {
    size += gst_adapter_available (dec->adapter);
    *othersize = size * 16;
    *othersize -= *othersize % 640;
    if ((size * 16) % 640 > 0)
      *othersize+= 640;
  } else {
    *othersize = size / 16;
    *othersize -= *othersize % 40;
    if ((size / 16) % 40 > 0)
      *othersize+= 40;
  }
  GST_DEBUG ("Transform size %d to %d", size, *othersize);

  return TRUE;
}

static GstCaps * gst_siren_dec_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GstCaps *othercaps = NULL;
  GST_DEBUG ("Transforming caps");

  if (direction == GST_PAD_SINK) {
    return gst_static_pad_template_get_caps (&srctemplate);
  } else if (direction == GST_PAD_SRC) {
    return gst_static_pad_template_get_caps (&sinktemplate);
  }

  GST_DEBUG ("Transform caps");

  return othercaps;
}

static GstFlowReturn
gst_siren_dec_transform (GstBaseTransform *trans, GstBuffer *inbuf,
                     GstBuffer *outbuf)
{

  GstSirenDec *dec = GST_SIREN_DEC (trans);
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *data = NULL;
  gint offset = 0;
  gint decode_ret = 0;

  GST_DEBUG ("Transform");

  if (dec->decoder == NULL) {
    GST_DEBUG ("Siren decoder not set");
    return GST_FLOW_WRONG_STATE;
  }
  if (dec->adapter == NULL) {
    GST_DEBUG ("Adapter not set");
    return GST_FLOW_UNEXPECTED;
  }

  gst_buffer_ref (inbuf);
  gst_adapter_push (dec->adapter, inbuf);

  GST_DEBUG ("Received buffer of size %d with adapter of size : %d",
      GST_BUFFER_SIZE (inbuf), gst_adapter_available (dec->adapter));

  data = GST_BUFFER_DATA (outbuf);
  offset = 0;
  while((gst_adapter_available (dec->adapter) >= 40)  &&
      (offset + 640 <= GST_BUFFER_SIZE (outbuf)) &&
      ret == GST_FLOW_OK) {

    GST_DEBUG ("Decoding frame");

    decode_ret = Siren7_DecodeFrame (dec->decoder,
        (guint8 *) gst_adapter_peek (dec->adapter, 40),
        data + offset);
    if (decode_ret != 0) {
      GST_DEBUG ("Siren7_DecodeFrame returned %d", decode_ret);
      ret = GST_FLOW_ERROR;
    }

    gst_adapter_flush (dec->adapter, 40);
    offset += 640;
  }

  GST_DEBUG ("Finished decoding : %d", offset);
  if (offset != GST_BUFFER_SIZE (outbuf)) {
    GST_DEBUG ("didn't decode enough : offfset (%d) != BUFFER_SIZE (%d)",
        offset, GST_BUFFER_SIZE (outbuf));
    return GST_FLOW_ERROR;
  }

  return ret;
}



gboolean
gst_siren_dec_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "sirendec",
      GST_RANK_MARGINAL, GST_TYPE_SIREN_DEC);
}
