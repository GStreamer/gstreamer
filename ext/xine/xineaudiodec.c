/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include <gst/gst.h>
#include "gstxine.h"
#include <xine/xine_internal.h>
#include <xine/plugin_catalog.h>

#define GST_TYPE_XINE_AUDIO_DEC \
  (gst_xine_audio_dec_get_type())
#define GST_XINE_AUDIO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_XINE_AUDIO_DEC,GstXineAudioDec))
#define GST_XINE_AUDIO_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_XINE_AUDIO_DEC, GstXineAudioDecClass))
#define GST_XINE_AUDIO_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_XINE_AUDIO_DEC,GstXineAudioDecClass))
#define GST_IS_XINE_AUDIO_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_XINE_AUDIO_DEC))
#define GST_IS_XINE_AUDIO_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_XINE_AUDIO_DEC))

GType gst_xine_audio_dec_get_type (void);

typedef struct _GstXineAudioDec GstXineAudioDec;
typedef struct _GstXineAudioDecClass GstXineAudioDecClass;

struct _GstXineAudioDec
{
  GstXine parent;

  GstPad *sinkpad;
  GstPad *srcpad;

  audio_decoder_t *decoder;
  guint32 format;
  xine_waveformatex wave;
  gboolean setup;
};

struct _GstXineAudioDecClass
{
  GstXineClass parent_class;

  plugin_node_t *plugin_node;
};

/*** xine audio driver wrapper ************************************************/

typedef struct
{
  xine_ao_driver_t driver;
  GstXineAudioDec *xine;
  gboolean open;
}
GstXineAudioDriver;

static guint32
_driver_get_capabilities (xine_ao_driver_t * driver)
{
  /* FIXME: add more when gst handles more than 2 channels */
  return AO_CAP_MODE_MONO | AO_CAP_MODE_STEREO | AO_CAP_8BITS;
}

static gint
_driver_get_property (xine_ao_driver_t * driver, int property)
{
  return 0;
}

static gint
_driver_set_property (xine_ao_driver_t * driver, int property, int value)
{
  return ~value;
}

static gint
_driver_open (xine_ao_driver_t * driver, xine_stream_t * stream, guint32 bits,
    guint32 rate, int mode)
{
  GstCaps *caps;
  GstXineAudioDriver *xine = ((GstXineAudioDriver *) driver);

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, (gint) bits,
      "depth", G_TYPE_INT, (gint) bits,
      "signed", G_TYPE_BOOLEAN, (bits == 8) ? FALSE : TRUE,
      "channels", G_TYPE_INT, (mode | AO_CAP_MODE_STEREO) ? 2 : 1,
      "rate", G_TYPE_INT, rate, NULL);

  if (!gst_pad_set_explicit_caps (xine->xine->srcpad, caps)) {
    gst_caps_free (caps);
    driver->open = FALSE;
    return -1;
  }

  xine->open = TRUE;
  gst_caps_free (caps);
  return rate;
}

static void
_driver_close (xine_ao_driver_t * driver, xine_stream_t * stream)
{
  /* FIXME: unset explicit caps here? And how? */
  driver->open = FALSE;
}

static void
_driver_exit (xine_ao_driver_t * driver)
{
  g_free (driver);
}

static int
_driver_control (xine_ao_driver_t * driver, int cmd, ...)
{
  return 0;
}

static void
_driver_flush (xine_ao_driver_t * driver)
{
}

static int
_driver_status (xine_ao_driver_t * driver, xine_stream_t * stream,
    uint32_t * bits, uint32_t * rate, int *mode)
{
  const GstCaps *caps;
  GstStructure *structure;
  gint temp;
  GstXineAudioDriver *xine = (GstXineAudioDriver *) driver;

  if (xine->open == FALSE
      || !(caps = gst_pad_get_negotiated_caps (xine->xine->srcpad)))
    return 0;

  structure = gst_caps_get_structure (caps, 0);
  *bits = 0;                    /* FIXME */
  if (!gst_structure_get_int (structure, "rate", &temp)) {
    g_assert_not_reached ();    /* may never happen with negotiated caps */
    return 0;
  }
  *rate = temp;
  if (!gst_structure_get_int (structure, "channels", &temp)) {
    g_assert_not_reached ();    /* may never happen with negotiated caps */
    return 0;
  }
  *mode = (temp == 2) ? AO_CAP_MODE_STEREO : AO_CAP_MODE_MONO;
  if (!gst_structure_get_int (structure, "width", &temp)) {
    g_assert_not_reached ();    /* may never happen with negotiated caps */
    return 0;
  }
  if (temp == 8)
    *mode |= AO_CAP_8BITS;

  return 1;
}

#define _DRIVER_BUFFER_SIZE 4096
static audio_buffer_t *
_driver_get_buffer (xine_ao_driver_t * driver)
{
  GstXineAudioDriver *xine = (GstXineAudioDriver *) driver;
  audio_buffer_t *audio = g_new0 (audio_buffer_t, 1);

  audio->mem = g_malloc (_DRIVER_BUFFER_SIZE);
  audio->mem_size = _DRIVER_BUFFER_SIZE;
  audio->stream = gst_xine_get_stream (GST_XINE (xine->xine));
  /* FIXME: fill more fields */

  return audio;
}

static void
_driver_put_buffer (xine_ao_driver_t * driver, audio_buffer_t * audio,
    xine_stream_t * stream)
{
  GstXineAudioDriver *xine = (GstXineAudioDriver *) driver;
  GstBuffer *buffer;

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = (guint8 *) audio->mem;
  GST_BUFFER_SIZE (buffer) = audio->mem_size;
  GST_BUFFER_MAXSIZE (buffer) = audio->mem_size;
  /* FIXME: fill more fields */
  g_free (audio);
  gst_pad_push (xine->xine->srcpad, GST_DATA (buffer));
}

static xine_ao_driver_t *
_gst_xine_audio_dec_create_audio_driver (GstXine * xine)
{
  GstXineAudioDriver *driver = g_new (GstXineAudioDriver, 1);

  driver->xine = GST_XINE_AUDIO_DEC (xine);
  driver->open = FALSE;

  driver->driver.get_buffer = _driver_get_buffer;
  driver->driver.put_buffer = _driver_put_buffer;
  driver->driver.get_capabilities = _driver_get_capabilities;
  driver->driver.get_property = _driver_get_property;
  driver->driver.set_property = _driver_set_property;
  driver->driver.open = _driver_open;
  driver->driver.close = _driver_close;
  driver->driver.exit = _driver_exit;
  driver->driver.control = _driver_control;
  driver->driver.flush = _driver_flush;
  driver->driver.status = _driver_status;

  return (xine_ao_driver_t *) driver;
}

/** GstXineAudioDec ***********************************************************/

GST_BOILERPLATE (GstXineAudioDec, gst_xine_audio_dec, GstXine, GST_TYPE_XINE)

     static void gst_xine_audio_dec_chain (GstPad * pad, GstData * in);
     static GstElementStateReturn
         gst_xine_audio_dec_change_state (GstElement * element);

/* this function handles the link with other plug-ins */
     static GstPadLinkReturn
         gst_xine_audio_dec_sink_link (GstPad * pad, const GstCaps * caps)
{
  guint temp;
  GstStructure *structure;
  GstXineAudioDec *xine =
      GST_XINE_AUDIO_DEC (gst_object_get_parent (GST_OBJECT (pad)));

  xine->format = gst_xine_get_format_for_caps (caps);
  if (xine->format == 0)
    return GST_PAD_LINK_REFUSED;

  /* get setup data */
  xine->setup = FALSE;
  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_get_int (structure, "channels", &temp))
    xine->wave.nChannels = temp;
  if (gst_structure_get_int (structure, "rate", &temp))
    xine->wave.nSamplesPerSec = temp;
  xine->wave.wBitsPerSample = 16;       /* FIXME: how do we figure this thing out? */
  /* FIXME: fill wave header better */

  return GST_PAD_LINK_OK;
}

static void
gst_xine_audio_dec_base_init (gpointer g_class)
{
}

static void
gst_xine_audio_dec_class_init (GstXineAudioDecClass * klass)
{
  GstXineClass *xine = GST_XINE_CLASS (klass);
  GstElementClass *element = GST_ELEMENT_CLASS (klass);

  element->change_state = gst_xine_audio_dec_change_state;

  xine->create_audio_driver = _gst_xine_audio_dec_create_audio_driver;
}

static void
gst_xine_audio_dec_init (GstXineAudioDec * xine)
{
  xine->setup = FALSE;
}

static void
gst_xine_audio_dec_event (GstXineAudioDec * xine, GstEvent * event)
{
  gst_pad_event_default (xine->sinkpad, event);
}

static void
gst_xine_audio_dec_chain (GstPad * pad, GstData * in)
{
  buf_element_t buffer = { 0, };
  GstXineAudioDec *xine =
      GST_XINE_AUDIO_DEC (gst_object_get_parent (GST_OBJECT (pad)));

  if (GST_IS_EVENT (in)) {
    gst_xine_audio_dec_event (xine, GST_EVENT (in));
    return;
  }

  if (xine->format == 0) {
    /* no caps yet */
    GST_ELEMENT_ERROR (xine, CORE, NEGOTIATION, (NULL),
        ("buffer sent before doing caps nego"));
    gst_data_unref (in);
    return;
  }

  if (!xine->setup) {
    buf_element_t element = { 0, };
    guint8 stsd[150] = { 0, };
    guint temp;
    GstStructure *structure;

    /* sent setup header */
    element.type = xine->format;
    element.decoder_flags = BUF_FLAG_HEADER;
    element.decoder_info[0] = 0;
    element.decoder_info[1] = xine->wave.nSamplesPerSec;
    element.decoder_info[2] = xine->wave.wBitsPerSample;
    element.decoder_info[3] = xine->wave.nChannels;
    element.content = (guchar *) & xine->wave;
    element.size = sizeof (xine_waveformatex);
    xine->decoder->decode_data (xine->decoder, &element);
    /* send stsd emulation to the decoder */
    /* FIXME: qdm2 only right now */
    g_assert (gst_pad_get_negotiated_caps (xine->sinkpad));
    structure =
        gst_caps_get_structure (gst_pad_get_negotiated_caps (xine->sinkpad), 0);
    *((guint32 *) & stsd[56]) = GUINT32_TO_BE (12);
    memcpy (&stsd[60], "frmaQDM2", 8);
    *((guint32 *) & stsd[68]) = GUINT32_TO_BE (36);
    memcpy (&stsd[72], "QDCA", 4);
    *((guint32 *) & stsd[76]) = GUINT32_TO_BE (1);
    if (!gst_structure_get_int (structure, "channels", &temp))
      temp = 0;
    *((guint32 *) & stsd[80]) = GUINT32_TO_BE (temp);
    if (!gst_structure_get_int (structure, "rate", &temp))
      temp = 0;
    *((guint32 *) & stsd[84]) = GUINT32_TO_BE (temp);
    if (!gst_structure_get_int (structure, "bitrate", &temp))
      temp = 0;
    *((guint32 *) & stsd[88]) = GUINT32_TO_BE (temp);
    if (!gst_structure_get_int (structure, "blocksize", &temp))
      temp = 0;
    *((guint32 *) & stsd[92]) = GUINT32_TO_BE (temp);
    *((guint32 *) & stsd[96]) = GUINT32_TO_BE (256);
    if (!gst_structure_get_int (structure, "framesize", &temp))
      temp = 0;
    *((guint32 *) & stsd[100]) = GUINT32_TO_BE (temp);
    *((guint32 *) & stsd[104]) = GUINT32_TO_BE (28);
    memcpy (&stsd[108], "QDCP", 4);
    *((guint32 *) & stsd[112]) = GUINT32_TO_BE (1065353216);
    *((guint32 *) & stsd[116]) = GUINT32_TO_BE (0);
    *((guint32 *) & stsd[120]) = GUINT32_TO_BE (1065353216);
    *((guint32 *) & stsd[124]) = GUINT32_TO_BE (1065353216);
    *((guint32 *) & stsd[128]) = GUINT32_TO_BE (27);
    *((guint32 *) & stsd[132]) = GUINT32_TO_BE (8);
    *((guint32 *) & stsd[136]) = GUINT32_TO_BE (0);
    *((guint32 *) & stsd[140]) = GUINT32_TO_BE (24);
    gst_util_dump_mem (stsd, 144);
    element.decoder_flags = BUF_FLAG_SPECIAL;
    element.decoder_info[1] = BUF_SPECIAL_STSD_ATOM;
    element.decoder_info[2] = 144;
    element.decoder_info[3] = 0;
    element.decoder_info_ptr[2] = stsd;
    element.size = 0;
    element.content = 0;
    xine->decoder->decode_data (xine->decoder, &element);

    xine->setup = TRUE;
  }

  gst_buffer_to_xine_buffer (&buffer, GST_BUFFER (in));
  buffer.type = xine->format;
  xine->decoder->decode_data (xine->decoder, &buffer);
  gst_data_unref (in);
}

static audio_decoder_t *
_load_decoder (GstXineAudioDec * dec)
{
  xine_stream_t *stream = gst_xine_get_stream (GST_XINE (dec));
  plugin_catalog_t *catalog = stream->xine->plugin_catalog;
  plugin_node_t *node = GST_XINE_AUDIO_DEC_GET_CLASS (dec)->plugin_node;
  audio_decoder_t *ret;

  /* FIXME: this is really hacky, but how to force xine to load a plugin? */
  /* how it works: xine can load a plugin for a particular stream type.
   * We just take one type, which should not have plugins attached to it,
   * attach our plugin and load it */
  g_assert (catalog->audio_decoder_map[DECODER_MAX - 1][0] == NULL);
  catalog->audio_decoder_map[DECODER_MAX - 1][0] = node;
  ret = _x_get_audio_decoder (stream, DECODER_MAX - 1);
  catalog->audio_decoder_map[DECODER_MAX - 1][0] = NULL;

  return ret;
}

static GstElementStateReturn
gst_xine_audio_dec_change_state (GstElement * element)
{
  GstXineAudioDec *xine = GST_XINE_AUDIO_DEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      xine->decoder = _load_decoder (xine);
      if (!xine->decoder)
        return GST_STATE_FAILURE;
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      xine->setup = FALSE;
      _x_free_audio_decoder (gst_xine_get_stream (GST_XINE (xine)),
          xine->decoder);
      break;
    default:
      GST_ERROR_OBJECT (element, "invalid state change");
      break;
  }

  return GST_CALL_PARENT_WITH_DEFAULT (GST_ELEMENT_CLASS, change_state,
      (element), GST_STATE_SUCCESS);
}

/** GstXineAudioDec subclasses ************************************************/

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) FALSE, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

static void
gst_xine_audio_dec_subclass_init (gpointer g_class, gpointer class_data)
{
  GstXineAudioDecClass *xine_class = GST_XINE_AUDIO_DEC_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstElementDetails details = GST_ELEMENT_DETAILS (NULL,
      "Filter/Decoder/Audio",
      NULL,
      "Benjamin Otte <otte@gnome.org>");
  GstPadTemplate *template;
  guint i = 0;
  GstCaps *caps = gst_caps_new_empty ();
  decoder_info_t *dec;

  xine_class->plugin_node = class_data;
  dec = xine_class->plugin_node->info->special_info;
  details.longname =
      g_strdup_printf ("%s xine audio decoder",
      xine_class->plugin_node->info->id);
  details.description =
      g_strdup_printf ("decodes audio using the xine '%s' plugin",
      xine_class->plugin_node->info->id);
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.description);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  while (dec->supported_types[i] != 0) {
    const gchar *type_str =
        gst_xine_get_caps_for_format (dec->supported_types[i]);
    if (type_str) {
      gst_caps_append (caps, gst_caps_from_string (type_str));
    }
    i++;
  }
  template = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, template);
}

static void
gst_xine_audio_dec_sub_init (GTypeInstance * instance, gpointer g_class)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (instance);
  GstXineAudioDec *xine = GST_XINE_AUDIO_DEC (instance);

  xine->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_link_function (xine->sinkpad, gst_xine_audio_dec_sink_link);
  gst_pad_set_chain_function (xine->sinkpad, gst_xine_audio_dec_chain);
  gst_element_add_pad (GST_ELEMENT (xine), xine->sinkpad);

  xine->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_use_explicit_caps (xine->srcpad);
  gst_element_add_pad (GST_ELEMENT (xine), xine->srcpad);
}

gboolean
gst_xine_audio_dec_init_plugin (GstPlugin * plugin)
{
  GTypeInfo plugin_info = {
    sizeof (GstXineAudioDecClass),
    NULL,
    NULL,
    gst_xine_audio_dec_subclass_init,
    NULL,
    NULL,
    sizeof (GstXineAudioDec),
    0,
    gst_xine_audio_dec_sub_init,
  };
  xine_node_t *list;
  GstXineClass *klass;

  klass = g_type_class_ref (GST_TYPE_XINE);

  list = klass->xine->plugin_catalog->audio->first;
  while (list) {
    plugin_node_t *node = list->content;
    decoder_info_t *dec;
    guint format = 0;

    list = list->next;
    if (!node)
      continue;

    dec = node->info->special_info;
    while (dec->supported_types[format] != 0) {
      const gchar *caps =
          gst_xine_get_caps_for_format (dec->supported_types[format]);
      if (caps) {
        gchar *plugin_name =
            g_strdup_printf ("xineaudiodec_%s", node->info->id);
        gchar *type_name =
            g_strdup_printf ("GstXineAudioDec%s", node->info->id);
        GType type;

        plugin_info.class_data = node;
        type =
            g_type_register_static (GST_TYPE_XINE_AUDIO_DEC, type_name,
            &plugin_info, 0);
        g_free (type_name);
        if (!gst_element_register (plugin, plugin_name,
                MAX (GST_RANK_MARGINAL,
                    GST_RANK_MARGINAL * dec->priority / 10 + 1), type)) {
          g_free (plugin_name);
          return FALSE;
        }
        g_free (plugin_name);
      }
      format++;
    }
  }

  g_type_class_unref (klass);
  return TRUE;
}
