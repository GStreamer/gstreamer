/*
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * sinesrc.c: An elemnt emitting a sine src in lots of different formats
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "sinesrc.h"
#include <math.h>
#include <string.h> /* memcpy */

#define SAMPLES_PER_WAVE 200

GST_PAD_TEMPLATE_FACTORY (sinesrc_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "sinesrc_int_src",
    "audio/raw",
      "law",            GST_PROPS_INT (0),
      "endianness",	    GST_PROPS_LIST (GST_PROPS_INT (G_LITTLE_ENDIAN), GST_PROPS_INT (G_BIG_ENDIAN)),
      "signed",         GST_PROPS_LIST (GST_PROPS_BOOLEAN (FALSE), GST_PROPS_BOOLEAN (TRUE)),
      "width",          GST_PROPS_INT_RANGE (8, 32),
      "depth",    	    GST_PROPS_INT_RANGE (8, 32),
      "rate",     	    GST_PROPS_INT_RANGE (8000, 192000),
      "channels", 	    GST_PROPS_INT_RANGE (1, 16)
  ),
  GST_CAPS_NEW (
    "sinesrc_float_src",
    "audio/raw",
      "channels", 	    GST_PROPS_INT_RANGE (1, 16)
  )
);

static GstElementClass *parent_class = NULL;

static void             sinesrc_init (SineSrc *src);
static void             sinesrc_class_init (SineSrcClass *klass);

static GstData *      sinesrc_get (GstPad *pad);
static GstElementStateReturn sinesrc_change_state (GstElement *element);


GType
sinesrc_get_type (void)
{
  static GType sinesrc_type = 0;

  if (!sinesrc_type) {
    static const GTypeInfo sinesrc_info = {
      sizeof (SineSrcClass), NULL, NULL,
      (GClassInitFunc) sinesrc_class_init, NULL, NULL,
      sizeof (SineSrc), 0,
      (GInstanceInitFunc) sinesrc_init,
    };
    sinesrc_type = g_type_register_static (GST_TYPE_ELEMENT, "SineSrc", 
		                           &sinesrc_info, 0);
  }
  return sinesrc_type;
}
static void
sinesrc_class_init (SineSrcClass *klass) 
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  element_class->change_state = sinesrc_change_state;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void 
sinesrc_init (SineSrc *src) 
{
  src->src = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sinesrc_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT(src), src->src);
  gst_pad_set_get_function (src->src, sinesrc_get);

  src->width = 16;
  src->depth = 16;
  src->sign = TRUE;
  src->endianness = G_BYTE_ORDER;
  src->rate = 44100;
  src->channels = 1;
  src->type = SINE_SRC_INT;
  src->newcaps = TRUE;
  
  src->pre_get_func = NULL;
  
  GST_OBJECT (src)->name = "sinesrc";
}

static void 
sinesrc_force_caps (SineSrc *src) {
  GstCaps *caps;

  if (!src->newcaps)
    return;
  
  src->newcaps = FALSE;

  switch (src->type) {
    case SINE_SRC_INT:
      caps = GST_CAPS_NEW (
	      "sinesrc_src_caps",
	      "audio/raw",
        "law",              GST_PROPS_INT (0),
        "signed",           GST_PROPS_BOOLEAN (src->sign),
    	  "depth",            GST_PROPS_INT (src->depth)
	    );
      if (src->width > 8)
        gst_props_add_entry (gst_caps_get_props (caps), 
                gst_props_entry_new ("endianness", 
                        GST_PROPS_INT (src->endianness)));
      break;
    case SINE_SRC_FLOAT:
      g_assert (src->width == 32 || src->width == 64);
      caps = GST_CAPS_NEW (
	      "sinesrc_src_caps",
	      "audio/raw",
              "endianness", GST_PROPS_INT(src->endianness)
      );
      break;
    default:
      g_assert_not_reached();
  }
  gst_props_add_entry (gst_caps_get_props (caps), 
          gst_props_entry_new ("width", GST_PROPS_INT (src->width)));
  gst_props_add_entry (gst_caps_get_props (caps), 
          gst_props_entry_new ("rate", GST_PROPS_INT (src->rate)));
  gst_props_add_entry (gst_caps_get_props (caps), 
          gst_props_entry_new ("channels", GST_PROPS_INT (src->channels)));
  
  g_assert (gst_pad_try_set_caps (src->src, caps) == GST_PAD_LINK_OK);
}
/* always return 1 wave 
 * there are 200 waves in 1 second, so the frequency is samplerate/200
 */
static guint8 UIDENTITY(guint8 x) { return x; };
static gint8 IDENTITY(gint8 x) { return x; };
#define POPULATE(format, be_func, le_func) {\
  format val = (format) int_value;\
  switch (src->endianness) {\
    case G_LITTLE_ENDIAN:\
      val = le_func (val);\
      break;\
    case G_BIG_ENDIAN:\
      val = be_func (val);\
      break;\
    default: \
      g_assert_not_reached ();\
  };\
  format* p = data;\
  for (j = 0; j < src->channels; j++) {\
    *p = val;\
    p ++;\
  }\
  data = p;\
}
static GstData *
sinesrc_get (GstPad *pad)
{
  GstBuffer *buf;
  SineSrc *src;
  
  void *data;
  gint i, j;
  gdouble value;
  
  g_return_val_if_fail (pad != NULL, NULL);
  src = SINESRC(gst_pad_get_parent (pad));

  if (src->pre_get_func)
    src->pre_get_func (src);
  
  g_assert ((buf = gst_buffer_new_and_alloc ((src->width / 8) * src->channels * SAMPLES_PER_WAVE)));
  g_assert ((data = GST_BUFFER_DATA(buf)));
  
  for (i = 0; i < SAMPLES_PER_WAVE; i++) {
    value = sin (i * 2 * M_PI / SAMPLES_PER_WAVE);
    switch (src->type) {
      case SINE_SRC_INT: {
        gint64 int_value = (value + (src->sign ? 0 : 1)) * (((guint64) 1) << (src->depth - 1));
        if (int_value == (1 + (src->sign ? 0 : 1)) * (((guint64) 1) << (src->depth - 1))) int_value--;
        switch (src->width) {
          case 8:
            if (src->sign)
              POPULATE (gint8, IDENTITY, IDENTITY)
            else
              POPULATE (guint8, UIDENTITY, UIDENTITY)
            break;
          case 16:
            if (src->sign)
              POPULATE (gint16, GINT16_TO_BE, GINT16_TO_LE)
            else
              POPULATE (guint16, GUINT16_TO_BE, GUINT16_TO_LE)
            break;
          case 24:
            if (src->sign) {
	      gpointer p;
              gint32 val = (gint32) int_value;
              switch (src->endianness) {
                case G_LITTLE_ENDIAN:
                  val = GINT32_TO_LE (val);
                  break;
                case G_BIG_ENDIAN:
                  val = GINT32_TO_BE (val);
                  break;
                default:
                  g_assert_not_reached ();
              };
	      p = &val;
	      if (src->endianness == G_BIG_ENDIAN)
	        p++;
              for (j = 0; j < src->channels; j++) {
	        memcpy (data, p, 3);
	        data += 3;
	      }	      
	    } else {
	      gpointer p;
              guint32 val = (guint32) int_value;
              switch (src->endianness) {
                case G_LITTLE_ENDIAN:
                  val = GUINT32_TO_LE (val);
                  break;
                case G_BIG_ENDIAN:
                  val = GUINT32_TO_BE (val);
                  break;
                default:
                  g_assert_not_reached ();
              };
	      p = &val;
	      if (src->endianness == G_BIG_ENDIAN)
	        p++;
              for (j = 0; j < src->channels; j++) {
	        memcpy (data, p, 3);
	        data += 3;
	      }
            }	    
            break;
          case 32:
            if (src->sign)
              POPULATE (gint32, GINT32_TO_BE, GINT32_TO_LE)
            else
              POPULATE (guint32, GUINT32_TO_BE, GUINT32_TO_LE)
            break;
          default:
            g_assert_not_reached ();
        }
        break;
      }
      case SINE_SRC_FLOAT: 
        if (src->width == 32) {
          gfloat *p = (gfloat *) data;
          gfloat fval = (gfloat) value;
          for (j = 0; j < src->channels; j++) {
            *p = fval;
            p++;
          }
          data = p;
          break;
        }
        if (src->width == 64) {
          gdouble *p = (gdouble *) data;
          for (j = 0; j < src->channels; j++) {
            *p = value;
            p++;
          }
          data = p;
          break;
        }
        g_assert_not_reached ();
      default:
        g_assert_not_reached ();
    }
  }
  
  if (src->newcaps) {
    sinesrc_force_caps(src);
  }
  return buf;
}

GstElement *
sinesrc_new (void)
{
  return GST_ELEMENT (g_object_new (TYPE_SINESRC, NULL));
}
void
sinesrc_set_pre_get_func (SineSrc *src, PreGetFunc func)
{
  src->pre_get_func = func;
}
static GstElementStateReturn
sinesrc_change_state (GstElement *element)
{
  SineSrc *sinesrc;

  g_return_val_if_fail (element != NULL, FALSE);
  sinesrc = SINESRC (element);

  switch (GST_STATE_TRANSITION (element)) {
  case GST_STATE_NULL_TO_READY:
  case GST_STATE_READY_TO_PAUSED:
  case GST_STATE_PAUSED_TO_PLAYING:
  case GST_STATE_PLAYING_TO_PAUSED:
    break;
  case GST_STATE_PAUSED_TO_READY:
    sinesrc->newcaps = TRUE;
    break;
  case GST_STATE_READY_TO_NULL:
    break;
  default:
    g_assert_not_reached();
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
