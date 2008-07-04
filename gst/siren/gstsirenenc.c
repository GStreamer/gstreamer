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

static GstFlowReturn
gst_siren_enc_chain (GstPad *pad, GstBuffer *buf);
static GstStateChangeReturn
gst_siren_change_state (GstElement *element, GstStateChange transition);


static void
_do_init (GType type)
{
  GST_DEBUG_CATEGORY_INIT
    (sirenenc_debug, "sirenenc", 0, "sirenenc");
}

GST_BOILERPLATE_FULL (GstSirenEnc, gst_siren_enc, GstElement,
    GST_TYPE_ELEMENT, _do_init);

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
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  GST_DEBUG ("Initializing Class");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_siren_enc_dispose);

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_siren_change_state);

  GST_DEBUG ("Class Init done");
}

static void
gst_siren_enc_init (GstSirenEnc *enc, GstSirenEncClass *klass)
{

  GST_DEBUG_OBJECT (enc, "Initializing");
  enc->encoder = Siren7_NewEncoder (16000);
  enc->adapter = gst_adapter_new ();

  enc->sinkpad = gst_pad_new_from_static_template (&sinktemplate, "sink");
  enc->srcpad = gst_pad_new_from_static_template (&srctemplate, "src");

  gst_pad_set_chain_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (gst_siren_enc_chain));

  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);

  enc->srccaps = gst_static_pad_template_get_caps (&srctemplate);

  GST_DEBUG_OBJECT (enc, "Init done");
}

static void
gst_siren_enc_dispose (GObject *object)
{
  GstSirenEnc *enc = GST_SIREN_ENC (object);

  GST_DEBUG_OBJECT (object, "Disposing");

  if (enc->encoder) {
    Siren7_CloseEncoder (enc->encoder);
    enc->encoder = NULL;
  }

  if (enc->adapter) {
    g_object_unref (enc->adapter);
    enc->adapter = NULL;
  }

  if (enc->srccaps)
  {
    gst_caps_unref (enc->srccaps);
    enc->srccaps = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstFlowReturn
gst_siren_enc_chain (GstPad *pad, GstBuffer *buf)
{
  GstSirenEnc *enc = GST_SIREN_ENC (gst_pad_get_parent_element (pad));
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *encoded = NULL;
  guint8 *data = NULL;
  gint offset = 0;
  gint encode_ret = 0;
  gint size = 0;
  guint in_offset = 0;

  GST_OBJECT_LOCK (enc);

  gst_adapter_push (enc->adapter, buf);

  GST_LOG_OBJECT (enc, "Received buffer of size %d with adapter of size : %d",
      GST_BUFFER_SIZE (buf), gst_adapter_available (enc->adapter));

  size = gst_adapter_available (enc->adapter);
  size /= 16;
  size -= size % 40;

  if (size == 0) {
    GST_OBJECT_UNLOCK (enc);
    goto out;
  }

  data = gst_adapter_take (enc->adapter, size * 16);

  GST_OBJECT_UNLOCK (enc);

  ret = gst_pad_alloc_buffer_and_set_caps (enc->srcpad,
      GST_BUFFER_OFFSET (buf) / 16, size, enc->srccaps, &encoded);

  if (ret != GST_FLOW_OK)
    goto out;

  while (offset < size && ret == GST_FLOW_OK) {
    GST_LOG_OBJECT (enc, "Encoding frame");

    encode_ret = Siren7_EncodeFrame (enc->encoder,
        data + in_offset,
        GST_BUFFER_DATA (encoded) + offset);
    if (encode_ret != 0) {
      GST_ERROR_OBJECT (enc, "Siren7_EncodeFrame returned %d", encode_ret);
      ret = GST_FLOW_ERROR;
      gst_buffer_unref (encoded);
      goto out;
    }

    offset += 40;
    in_offset += 640;
  }

  GST_LOG_OBJECT (enc, "Finished encoding : %d", offset);

  ret = gst_pad_push (enc->srcpad, encoded);

 out:
  if (data)
    g_free (data);

  gst_object_unref (enc);
  return ret;
}

static GstStateChangeReturn
gst_siren_change_state (GstElement *element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstSirenEnc *enc = GST_SIREN_ENC (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition)
  {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (element);
      gst_adapter_clear (enc->adapter);
      GST_OBJECT_UNLOCK (element);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_siren_enc_plugin_init (GstPlugin *plugin)
{
  return gst_element_register (plugin, "sirenenc",
      GST_RANK_MARGINAL, GST_TYPE_SIREN_ENC);
}
