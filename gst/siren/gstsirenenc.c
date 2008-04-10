/*
 * Siren Encoder Gst Element
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

#include "gstsirenenc.h"

#include <string.h>

GST_DEBUG_CATEGORY (sirenenc_debug);
#define GST_CAT_DEFAULT (sirenenc_debug)

/* elementfactory information */
static const GstElementDetails gst_siren_enc_details =
GST_ELEMENT_DETAILS (
  "Siren Encoder element",
  "Codec/Encoder/Audio ",
  "Encode 16bit PCM streams into the Siren7 codec",
  "Youness Alaoui <kakaroto@kakaroto.homelinux.net>");


static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-siren, "
        "dct-length = (int) 320"));

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
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



static void gst_siren_enc_dispose (GObject *object);

static GstFlowReturn gst_siren_enc_transform (GstBaseTransform *trans,
    GstBuffer *inbuf, GstBuffer *outbuf);
static gboolean gst_siren_enc_transform_size (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, guint size,
    GstCaps *othercaps, guint *othersize);
static GstCaps * gst_siren_enc_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_siren_enc_start (GstBaseTransform *trans);
static gboolean gst_siren_enc_stop (GstBaseTransform *trans);


static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT
    (sirenenc_debug, "sirenenc", 0, "sirenenc");
}

GST_BOILERPLATE_FULL (GstSirenEnc, gst_siren_enc, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, _do_init);

static void
gst_siren_enc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&srctemplate));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_details (element_class, &gst_siren_enc_details);
}

static void
gst_siren_enc_class_init (GstSirenEncClass *klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *gstbasetransform_class;

  gobject_class = (GObjectClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;

  GST_DEBUG ("Initializing Class");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_siren_enc_dispose);

  gstbasetransform_class->transform =
      GST_DEBUG_FUNCPTR (gst_siren_enc_transform);
  gstbasetransform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_siren_enc_transform_caps);
  gstbasetransform_class->transform_size =
      GST_DEBUG_FUNCPTR (gst_siren_enc_transform_size);
  gstbasetransform_class->start =
      GST_DEBUG_FUNCPTR (gst_siren_enc_start);
  gstbasetransform_class->stop =
      GST_DEBUG_FUNCPTR (gst_siren_enc_stop);

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG ("Class Init done");
}

static void
gst_siren_enc_init (GstSirenEnc *enc, GstSirenEncClass *klass)
{

  GST_DEBUG ("Initializing");
  enc->encoder = NULL;
  enc->adapter = gst_adapter_new ();

  GST_DEBUG ("Init done");
}

static void
gst_siren_enc_dispose (GObject *object)
{
  GstSirenEnc *enc = GST_SIREN_ENC (object);

  GST_DEBUG ("Disposing");

  if (enc->encoder) {
    Siren7_CloseEncoder (enc->encoder);
    enc->encoder = NULL;
  }
  if (enc->adapter) {
    g_object_unref (enc->adapter);
    enc->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);

  GST_DEBUG ("Dispose done");

}

static gboolean
gst_siren_enc_start (GstBaseTransform *trans)
{
  GstSirenEnc *enc = GST_SIREN_ENC (trans);

  GST_DEBUG ("Start");

  if (enc->encoder) {
    Siren7_CloseEncoder (enc->encoder);
    enc->encoder = NULL;
  }
  enc->encoder = Siren7_NewEncoder (16000);
  gst_adapter_clear (enc->adapter);

  return enc->encoder != NULL;
}



static gboolean
gst_siren_enc_stop (GstBaseTransform *trans)
{
  GstSirenEnc *enc = GST_SIREN_ENC (trans);

  GST_DEBUG ("Stop");

  if (enc->encoder) {
    Siren7_CloseEncoder (enc->encoder);
    enc->encoder = NULL;
  }
  gst_adapter_clear (enc->adapter);
  return TRUE;
}


static gboolean gst_siren_enc_transform_size (GstBaseTransform *trans,
    GstPadDirection direction, GstCaps *caps, guint size,
    GstCaps *othercaps, guint *othersize)
{
  GstSirenEnc *enc = GST_SIREN_ENC (trans);
  GstStructure *structure;
  const gchar *in_name;
  const gchar *out_name;

  if (caps == NULL || othercaps == NULL) {
    GST_WARNING ("caps NULL");
    return FALSE;
  }

  structure = gst_caps_get_structure (caps, 0);
  in_name = gst_structure_get_name (structure);
  structure = gst_caps_get_structure (othercaps, 0);
  out_name = gst_structure_get_name (structure);

  GST_DEBUG ("Transform size from caps '%s' to '%s'", in_name, out_name);

  if (in_name == NULL || out_name == NULL) {
    return FALSE;
  } else if (strcmp (in_name, "audio/x-raw-int") == 0 &&
      strcmp (out_name, "audio/x-siren") == 0 ) {
    size += gst_adapter_available (enc->adapter);
    *othersize = size / 16;
    *othersize -= *othersize % 40;
    if ((size / 16) % 40 > 0)
      *othersize+=40;
  } else if (strcmp (in_name, "audio/x-siren") == 0 &&
      strcmp (out_name, "audio/x-raw-int") == 0 ) {
    *othersize = size * 16;
    *othersize -= *othersize % 640;
    if ((size * 16) % 640 > 0)
      *othersize+= 640;
  } else {
    GST_DEBUG ("Unknown in/out caps");
    return FALSE;
  }

  GST_DEBUG ("Transform size %d to %d", size, *othersize);

  return TRUE;
}

static GstCaps * gst_siren_enc_transform_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps)
{
  GST_DEBUG ("Transforming caps");

  if (direction == GST_PAD_SRC) {
    return gst_static_pad_template_get_caps (&sinktemplate);
  } else if (direction == GST_PAD_SINK) {
    return gst_static_pad_template_get_caps (&srctemplate);
  }

  /* Make gcc happy */
  return NULL;
}

static GstFlowReturn
gst_siren_enc_transform (GstBaseTransform *trans, GstBuffer *inbuf,
                     GstBuffer *outbuf)
{

  GstSirenEnc *enc = GST_SIREN_ENC (trans);
  GstFlowReturn ret = GST_FLOW_OK;
  guint8 *data = NULL;
  gint offset = 0;
  gint encode_ret = 0;

  GST_DEBUG ("Transform");

  if (enc->encoder == NULL) {
    GST_DEBUG ("Siren encoder not set");
    return GST_FLOW_WRONG_STATE;
  }
  if (enc->adapter == NULL) {
    GST_DEBUG ("Adapter not set");
    return GST_FLOW_UNEXPECTED;
  }

  gst_buffer_ref (inbuf);
  gst_adapter_push (enc->adapter, inbuf);

  GST_DEBUG ("Received buffer of size %d with adapter of size : %d",
      GST_BUFFER_SIZE (inbuf), gst_adapter_available (enc->adapter));

  data = GST_BUFFER_DATA (outbuf);
  offset = 0;
  while(gst_adapter_available (enc->adapter) >= 640  &&
      offset + 40 <= GST_BUFFER_SIZE (outbuf) &&
      ret == GST_FLOW_OK) {

    GST_DEBUG ("Encoding frame");

    encode_ret = Siren7_EncodeFrame (enc->encoder,
        (guint8 *)gst_adapter_peek (enc->adapter, 640),
        data + offset);
    if (encode_ret != 0) {
      GST_DEBUG ("Siren7_EncodeFrame returned %d", encode_ret);
      ret = GST_FLOW_ERROR;
    }

    gst_adapter_flush (enc->adapter, 640);
    offset += 40;
  }

  GST_DEBUG ("Finished encoding : %d", offset);

  GST_BUFFER_SIZE (outbuf) = offset;

  return ret;
}



gboolean
gst_siren_enc_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "sirenenc",
      GST_RANK_MARGINAL, GST_TYPE_SIREN_ENC);
}
