/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstutils.c: Utility functions: gtk_get_property stuff, etc.
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

/**
 * SECTION:gstutils
 * @short_description: Various utility functions
 *
 * When defining own plugins, use the GST_BOILERPLATE ease gobject creation.
 */

#include <stdio.h>
#include <string.h>

#include "gst_private.h"
#include "gstghostpad.h"
#include "gstutils.h"
#include "gstinfo.h"
#include "gst-i18n-lib.h"


/**
 * gst_util_dump_mem:
 * @mem: a pointer to the memory to dump
 * @size: the size of the memory block to dump
 *
 * Dumps the memory block into a hex representation. Useful for debugging.
 */
void
gst_util_dump_mem (const guchar * mem, guint size)
{
  guint i, j;
  GString *string = g_string_sized_new (50);
  GString *chars = g_string_sized_new (18);

  i = j = 0;
  while (i < size) {
    if (g_ascii_isprint (mem[i]))
      g_string_append_printf (chars, "%c", mem[i]);
    else
      g_string_append_printf (chars, ".");

    g_string_append_printf (string, "%02x ", mem[i]);

    j++;
    i++;

    if (j == 16 || i == size) {
      g_print ("%08x (%p): %-48.48s %-16.16s\n", i - j, mem + i - j,
          string->str, chars->str);
      g_string_set_size (string, 0);
      g_string_set_size (chars, 0);
      j = 0;
    }
  }
  g_string_free (string, TRUE);
  g_string_free (chars, TRUE);
}


/**
 * gst_util_set_value_from_string:
 * @value: the value to set
 * @value_str: the string to get the value from
 *
 * Converts the string to the type of the value and
 * sets the value with it.
 */
void
gst_util_set_value_from_string (GValue * value, const gchar * value_str)
{
  gint sscanf_ret;

  g_return_if_fail (value != NULL);
  g_return_if_fail (value_str != NULL);

  GST_CAT_DEBUG (GST_CAT_PARAMS, "parsing '%s' to type %s", value_str,
      g_type_name (G_VALUE_TYPE (value)));

  switch (G_VALUE_TYPE (value)) {
    case G_TYPE_STRING:
      g_value_set_string (value, g_strdup (value_str));
      break;
    case G_TYPE_ENUM:
    case G_TYPE_INT:{
      gint i;

      sscanf_ret = sscanf (value_str, "%d", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_int (value, i);
      break;
    }
    case G_TYPE_UINT:{
      guint i;

      sscanf_ret = sscanf (value_str, "%u", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_uint (value, i);
      break;
    }
    case G_TYPE_LONG:{
      glong i;

      sscanf_ret = sscanf (value_str, "%ld", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_long (value, i);
      break;
    }
    case G_TYPE_ULONG:{
      gulong i;

      sscanf_ret = sscanf (value_str, "%lu", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_ulong (value, i);
      break;
    }
    case G_TYPE_BOOLEAN:{
      gboolean i = FALSE;

      if (!g_ascii_strncasecmp ("true", value_str, 4))
        i = TRUE;
      g_value_set_boolean (value, i);
      break;
    }
    case G_TYPE_CHAR:{
      gchar i;

      sscanf_ret = sscanf (value_str, "%c", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_char (value, i);
      break;
    }
    case G_TYPE_UCHAR:{
      guchar i;

      sscanf_ret = sscanf (value_str, "%c", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_uchar (value, i);
      break;
    }
    case G_TYPE_FLOAT:{
      gfloat i;

      sscanf_ret = sscanf (value_str, "%f", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_float (value, i);
      break;
    }
    case G_TYPE_DOUBLE:{
      gfloat i;

      sscanf_ret = sscanf (value_str, "%g", &i);
      g_return_if_fail (sscanf_ret == 1);
      g_value_set_double (value, (gdouble) i);
      break;
    }
    default:
      break;
  }
}

/**
 * gst_util_set_object_arg:
 * @object: the object to set the argument of
 * @name: the name of the argument to set
 * @value: the string value to set
 *
 * Convertes the string value to the type of the objects argument and
 * sets the argument with it.
 */
void
gst_util_set_object_arg (GObject * object, const gchar * name,
    const gchar * value)
{
  gboolean sscanf_ret;

  if (name && value) {
    GParamSpec *paramspec;

    paramspec =
        g_object_class_find_property (G_OBJECT_GET_CLASS (object), name);

    if (!paramspec) {
      return;
    }

    GST_DEBUG ("paramspec->flags is %d, paramspec->value_type is %d",
        paramspec->flags, (gint) paramspec->value_type);

    if (paramspec->flags & G_PARAM_WRITABLE) {
      switch (paramspec->value_type) {
        case G_TYPE_STRING:
          g_object_set (G_OBJECT (object), name, value, NULL);
          break;
        case G_TYPE_ENUM:
        case G_TYPE_INT:{
          gint i;

          sscanf_ret = sscanf (value, "%d", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_UINT:{
          guint i;

          sscanf_ret = sscanf (value, "%u", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_LONG:{
          glong i;

          sscanf_ret = sscanf (value, "%ld", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_ULONG:{
          gulong i;

          sscanf_ret = sscanf (value, "%lu", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_BOOLEAN:{
          gboolean i = FALSE;

          if (!g_ascii_strncasecmp ("true", value, 4))
            i = TRUE;
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_CHAR:{
          gchar i;

          sscanf_ret = sscanf (value, "%c", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_UCHAR:{
          guchar i;

          sscanf_ret = sscanf (value, "%c", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_FLOAT:{
          gfloat i;

          sscanf_ret = sscanf (value, "%f", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, i, NULL);
          break;
        }
        case G_TYPE_DOUBLE:{
          gfloat i;

          sscanf_ret = sscanf (value, "%g", &i);
          g_return_if_fail (sscanf_ret == 1);
          g_object_set (G_OBJECT (object), name, (gdouble) i, NULL);
          break;
        }
        default:
          if (G_IS_PARAM_SPEC_ENUM (paramspec)) {
            gint i;

            sscanf_ret = sscanf (value, "%d", &i);
            g_return_if_fail (sscanf_ret == 1);
            g_object_set (G_OBJECT (object), name, i, NULL);
          }
          break;
      }
    }
  }
}

/* work around error C2520: conversion from unsigned __int64 to double
 * not implemented, use signed __int64
 *
 * These are implemented as functions because on some platforms a 64bit int to
 * double conversion is not defined/implemented.
 */

gdouble
gst_util_guint64_to_gdouble (guint64 value)
{
  if (value & G_GINT64_CONSTANT (0x8000000000000000))
    return (gdouble) ((gint64) value) + (gdouble) 18446744073709551616.;
  else
    return (gdouble) ((gint64) value);
}

guint64
gst_util_gdouble_to_guint64 (gdouble value)
{
  if (value < (gdouble) 9223372036854775808.)   /* 1 << 63 */
    return ((guint64) ((gint64) value));

  value -= (gdouble) 18446744073709551616.;
  return ((guint64) ((gint64) value));
}

/* convenience struct for getting high and low uint32 parts of
 * a guint64 */
typedef union
{
  guint64 ll;
  struct
  {
#if G_BYTE_ORDER == G_BIG_ENDIAN
    guint32 high, low;
#else
    guint32 low, high;
#endif
  } l;
} GstUInt64;

/* based on Hacker's Delight p152 */
static guint64
gst_util_div128_64 (GstUInt64 c1, GstUInt64 c0, guint64 denom)
{
  GstUInt64 q1, q0, rhat;
  GstUInt64 v, cmp1, cmp2;
  guint s;

  v.ll = denom;

  /* count number of leading zeroes, we know they must be in the high
   * part of denom since denom > G_MAXUINT32. */
  s = v.l.high | (v.l.high >> 1);
  s |= (s >> 2);
  s |= (s >> 4);
  s |= (s >> 8);
  s = ~(s | (s >> 16));
  s = s - ((s >> 1) & 0x55555555);
  s = (s & 0x33333333) + ((s >> 2) & 0x33333333);
  s = (s + (s >> 4)) & 0x0f0f0f0f;
  s += (s >> 8);
  s = (s + (s >> 16)) & 0x3f;

  if (s > 0) {
    /* normalize divisor and dividend */
    v.ll <<= s;
    c1.ll = (c1.ll << s) | (c0.l.high >> (32 - s));
    c0.ll <<= s;
  }

  q1.ll = c1.ll / v.l.high;
  rhat.ll = c1.ll - q1.ll * v.l.high;

  cmp1.l.high = rhat.l.low;
  cmp1.l.low = c0.l.high;
  cmp2.ll = q1.ll * v.l.low;

  while (q1.l.high || cmp2.ll > cmp1.ll) {
    q1.ll--;
    rhat.ll += v.l.high;
    if (rhat.l.high)
      break;
    cmp1.l.high = rhat.l.low;
    cmp2.ll -= v.l.low;
  }
  c1.l.high = c1.l.low;
  c1.l.low = c0.l.high;
  c1.ll -= q1.ll * v.ll;
  q0.ll = c1.ll / v.l.high;
  rhat.ll = c1.ll - q0.ll * v.l.high;

  cmp1.l.high = rhat.l.low;
  cmp1.l.low = c0.l.low;
  cmp2.ll = q0.ll * v.l.low;

  while (q0.l.high || cmp2.ll > cmp1.ll) {
    q0.ll--;
    rhat.ll += v.l.high;
    if (rhat.l.high)
      break;
    cmp1.l.high = rhat.l.low;
    cmp2.ll -= v.l.low;
  }
  q0.l.high += q1.l.low;

  return q0.ll;
}

static guint64
gst_util_uint64_scale_int64 (guint64 val, guint64 num, guint64 denom)
{
  GstUInt64 a0, a1, b0, b1, c0, ct, c1, result;
  GstUInt64 v, n;

  /* prepare input */
  v.ll = val;
  n.ll = num;

  /* do 128 bits multiply
   *                   nh   nl
   *                *  vh   vl
   *                ----------
   * a0 =              vl * nl
   * a1 =         vl * nh
   * b0 =         vh * nl
   * b1 =  + vh * nh
   *       -------------------
   * c1,c0
   */
  a0.ll = (guint64) v.l.low * n.l.low;
  a1.ll = (guint64) v.l.low * n.l.high;
  b0.ll = (guint64) v.l.high * n.l.low;
  b1.ll = (guint64) v.l.high * n.l.high;

  /* and sum together with carry into 128 bits c1, c0 */
  c0.l.low = a0.l.low;
  ct.ll = (guint64) a0.l.high + a1.l.low + b0.l.low;
  c0.l.high = ct.l.low;
  c1.ll = (guint64) a1.l.high + b0.l.high + ct.l.high + b1.ll;

  /* if high bits bigger than denom, we overflow */
  if (c1.ll >= denom)
    goto overflow;

  /* shortcut for division by 1, c1.ll should be 0 because of the
   * overflow check above. */
  if (denom == 1)
    return c0.ll;

  /* and 128/64 bits division, result fits 64 bits */
  if (denom <= G_MAXUINT32) {
    guint32 den = (guint32) denom;

    /* easy case, (c1,c0)128/(den)32 division */
    c1.l.high %= den;
    c1.l.high = c1.ll % den;
    c1.l.low = c0.l.high;
    c0.l.high = c1.ll % den;
    result.l.high = c1.ll / den;
    result.l.low = c0.ll / den;
  } else {
    result.ll = gst_util_div128_64 (c1, c0, denom);
  }
  return result.ll;

overflow:
  {
    return G_MAXUINT64;
  }
}

/**
 * gst_util_uint64_scale:
 * @val: the number to scale
 * @num: the numerator of the scale ratio
 * @denom: the denominator of the scale ratio
 *
 * Scale @val by @num / @denom, trying to avoid overflows.
 *
 * This function can potentially be very slow if denom > G_MAXUINT32.
 *
 * Returns: @val * @num / @denom, trying to avoid overflows.
 */
guint64
gst_util_uint64_scale (guint64 val, guint64 num, guint64 denom)
{
  g_return_val_if_fail (denom != 0, G_MAXUINT64);

  if (num == 0)
    return 0;

  if (num == 1 && denom == 1)
    return val;

  /* if the denom is high, we need to do a 64 muldiv */
  if (denom > G_MAXINT32)
    goto do_int64;

  /* if num and denom are low we can do a 32 bit muldiv */
  if (num <= G_MAXINT32)
    goto do_int32;

  /* val and num are high, we need 64 muldiv */
  if (val > G_MAXINT32)
    goto do_int64;

  /* val is low and num is high, we can swap them and do 32 muldiv */
  return gst_util_uint64_scale_int (num, (gint) val, (gint) denom);

do_int32:
  return gst_util_uint64_scale_int (val, (gint) num, (gint) denom);

do_int64:
  /* to the more heavy implementations... */
  return gst_util_uint64_scale_int64 (val, num, denom);
}

/**
 * gst_util_uint64_scale_int:
 * @val: guint64 (such as a #GstClockTime) to scale.
 * @num: numerator of the scale factor.
 * @denom: denominator of the scale factor.
 *
 * Scale a guint64 by a factor expressed as a fraction (num/denom), avoiding
 * overflows and loss of precision.
 *
 * @num and @denom must be positive integers. @denom cannot be 0.
 *
 * Returns: @val * @num / @denom, avoiding overflow and loss of precision
 */
guint64
gst_util_uint64_scale_int (guint64 val, gint num, gint denom)
{
  GstUInt64 result;
  GstUInt64 low, high;

  g_return_val_if_fail (denom > 0, G_MAXUINT64);
  g_return_val_if_fail (num >= 0, G_MAXUINT64);

  if (num == 0)
    return 0;

  if (num == 1 && denom == 1)
    return val;

  if (val <= G_MAXUINT32)
    /* simple case */
    return val * num / denom;

  /* do 96 bits mult/div */
  low.ll = val;
  result.ll = ((guint64) low.l.low) * num;
  high.ll = ((guint64) low.l.high) * num + (result.l.high);

  low.ll = high.ll / denom;
  result.l.high = high.ll % denom;
  result.ll /= denom;

  /* avoid overflow */
  if (low.ll + result.l.high > G_MAXUINT32)
    goto overflow;

  result.l.high += low.l.low;

  return result.ll;

overflow:
  {
    return G_MAXUINT64;
  }
}

/* -----------------------------------------------------
 *
 *  The following code will be moved out of the main
 * gstreamer library someday.
 */

#include "gstpad.h"

static void
string_append_indent (GString * str, gint count)
{
  gint xx;

  for (xx = 0; xx < count; xx++)
    g_string_append_c (str, ' ');
}

/**
 * gst_print_pad_caps:
 * @buf: the buffer to print the caps in
 * @indent: initial indentation
 * @pad: the pad to print the caps from
 *
 * Write the pad capabilities in a human readable format into
 * the given GString.
 */
void
gst_print_pad_caps (GString * buf, gint indent, GstPad * pad)
{
  GstCaps *caps;

  caps = pad->caps;

  if (!caps) {
    string_append_indent (buf, indent);
    g_string_printf (buf, "%s:%s has no capabilities",
        GST_DEBUG_PAD_NAME (pad));
  } else {
    char *s;

    s = gst_caps_to_string (caps);
    g_string_append (buf, s);
    g_free (s);
  }
}

/**
 * gst_print_element_args:
 * @buf: the buffer to print the args in
 * @indent: initial indentation
 * @element: the element to print the args of
 *
 * Print the element argument in a human readable format in the given
 * GString.
 */
void
gst_print_element_args (GString * buf, gint indent, GstElement * element)
{
  guint width;
  GValue value = { 0, };        /* the important thing is that value.type = 0 */
  gchar *str = NULL;
  GParamSpec *spec, **specs, **walk;

  specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (element), NULL);

  width = 0;
  for (walk = specs; *walk; walk++) {
    spec = *walk;
    if (width < strlen (spec->name))
      width = strlen (spec->name);
  }

  for (walk = specs; *walk; walk++) {
    spec = *walk;

    if (spec->flags & G_PARAM_READABLE) {
      g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (spec));
      g_object_get_property (G_OBJECT (element), spec->name, &value);
      str = g_strdup_value_contents (&value);
      g_value_unset (&value);
    } else {
      str = g_strdup ("Parameter not readable.");
    }

    string_append_indent (buf, indent);
    g_string_append (buf, spec->name);
    string_append_indent (buf, 2 + width - strlen (spec->name));
    g_string_append (buf, str);
    g_string_append_c (buf, '\n');

    g_free (str);
  }

  g_free (specs);
}

/**
 * gst_element_create_all_pads:
 * @element: a #GstElement to create pads for
 *
 * Creates a pad for each pad template that is always available.
 * This function is only useful during object intialization of
 * subclasses of #GstElement.
 */
void
gst_element_create_all_pads (GstElement * element)
{
  GList *padlist;

  /* FIXME: lock element */

  padlist =
      gst_element_class_get_pad_template_list (GST_ELEMENT_CLASS
      (G_OBJECT_GET_CLASS (element)));

  while (padlist) {
    GstPadTemplate *padtempl = (GstPadTemplate *) padlist->data;

    if (padtempl->presence == GST_PAD_ALWAYS) {
      GstPad *pad;

      pad = gst_pad_new_from_template (padtempl, padtempl->name_template);

      gst_element_add_pad (element, pad);
    }
    padlist = padlist->next;
  }
}

/**
 * gst_element_get_compatible_pad_template:
 * @element: a #GstElement to get a compatible pad template for.
 * @compattempl: the #GstPadTemplate to find a compatible template for.
 *
 * Retrieves a pad template from @element that is compatible with @compattempl.
 * Pads from compatible templates can be linked together.
 *
 * Returns: a compatible #GstPadTemplate, or NULL if none was found. No
 * unreferencing is necessary.
 */
GstPadTemplate *
gst_element_get_compatible_pad_template (GstElement * element,
    GstPadTemplate * compattempl)
{
  GstPadTemplate *newtempl = NULL;
  GList *padlist;
  GstElementClass *class;

  g_return_val_if_fail (element != NULL, NULL);
  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (compattempl != NULL, NULL);

  class = GST_ELEMENT_GET_CLASS (element);

  padlist = gst_element_class_get_pad_template_list (class);

  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
      "Looking for a suitable pad template in %s out of %d templates...",
      GST_ELEMENT_NAME (element), g_list_length (padlist));

  while (padlist) {
    GstPadTemplate *padtempl = (GstPadTemplate *) padlist->data;
    GstCaps *intersection;

    /* Ignore name
     * Ignore presence
     * Check direction (must be opposite)
     * Check caps
     */
    GST_CAT_LOG (GST_CAT_CAPS,
        "checking pad template %s", padtempl->name_template);
    if (padtempl->direction != compattempl->direction) {
      GST_CAT_DEBUG (GST_CAT_CAPS,
          "compatible direction: found %s pad template \"%s\"",
          padtempl->direction == GST_PAD_SRC ? "src" : "sink",
          padtempl->name_template);

      GST_CAT_DEBUG (GST_CAT_CAPS,
          "intersecting %" GST_PTR_FORMAT, GST_PAD_TEMPLATE_CAPS (compattempl));
      GST_CAT_DEBUG (GST_CAT_CAPS,
          "..and %" GST_PTR_FORMAT, GST_PAD_TEMPLATE_CAPS (padtempl));

      intersection = gst_caps_intersect (GST_PAD_TEMPLATE_CAPS (compattempl),
          GST_PAD_TEMPLATE_CAPS (padtempl));

      GST_CAT_DEBUG (GST_CAT_CAPS, "caps are %scompatible %" GST_PTR_FORMAT,
          (intersection ? "" : "not "), intersection);

      if (!gst_caps_is_empty (intersection))
        newtempl = padtempl;
      gst_caps_unref (intersection);
      if (newtempl)
        break;
    }

    padlist = g_list_next (padlist);
  }
  if (newtempl)
    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
        "Returning new pad template %p", newtempl);
  else
    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "No compatible pad template found");

  return newtempl;
}

static GstPad *
gst_element_request_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name)
{
  GstPad *newpad = NULL;
  GstElementClass *oclass;

  oclass = GST_ELEMENT_GET_CLASS (element);

  if (oclass->request_new_pad)
    newpad = (oclass->request_new_pad) (element, templ, name);

  if (newpad)
    gst_object_ref (newpad);

  return newpad;
}



/**
 * gst_element_get_pad_from_template:
 * @element: a #GstElement.
 * @templ: a #GstPadTemplate belonging to @element.
 *
 * Gets a pad from @element described by @templ. If the presence of @templ is
 * #GST_PAD_REQUEST, requests a new pad. Can return %NULL for #GST_PAD_SOMETIMES
 * templates.
 *
 * Returns: the #GstPad, or NULL if one could not be found or created.
 */
static GstPad *
gst_element_get_pad_from_template (GstElement * element, GstPadTemplate * templ)
{
  GstPad *ret = NULL;
  GstPadPresence presence;

  /* If this function is ever exported, we need check the validity of `element'
   * and `templ', and to make sure the template actually belongs to the
   * element. */

  presence = GST_PAD_TEMPLATE_PRESENCE (templ);

  switch (presence) {
    case GST_PAD_ALWAYS:
    case GST_PAD_SOMETIMES:
      ret = gst_element_get_static_pad (element, templ->name_template);
      if (!ret && presence == GST_PAD_ALWAYS)
        g_warning
            ("Element %s has an ALWAYS template %s, but no pad of the same name",
            GST_OBJECT_NAME (element), templ->name_template);
      break;

    case GST_PAD_REQUEST:
      ret = gst_element_request_pad (element, templ, NULL);
      break;
  }

  return ret;
}

/**
 * gst_element_request_compatible_pad:
 * @element: a #GstElement.
 * @templ: the #GstPadTemplate to which the new pad should be able to link.
 *
 * Requests a pad from @element. The returned pad should be unlinked and
 * compatible with @templ. Might return an existing pad, or request a new one.
 *
 * Returns: a #GstPad, or %NULL if one could not be found or created.
 */
GstPad *
gst_element_request_compatible_pad (GstElement * element,
    GstPadTemplate * templ)
{
  GstPadTemplate *templ_new;
  GstPad *pad = NULL;

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (GST_IS_PAD_TEMPLATE (templ), NULL);

  /* FIXME: should really loop through the templates, testing each for
   *      compatibility and pad availability. */
  templ_new = gst_element_get_compatible_pad_template (element, templ);
  if (templ_new)
    pad = gst_element_get_pad_from_template (element, templ_new);

  /* This can happen for non-request pads. No need to unref. */
  if (pad && GST_PAD_PEER (pad))
    pad = NULL;

  return pad;
}

/**
 * gst_element_get_compatible_pad:
 * @element: a #GstElement in which the pad should be found.
 * @pad: the #GstPad to find a compatible one for.
 * @caps: the #GstCaps to use as a filter.
 *
 * Looks for an unlinked pad to which the given pad can link. It is not
 * guaranteed that linking the pads will work, though it should work in most
 * cases.
 *
 * Returns: the #GstPad to which a link can be made, or %NULL if one cannot be
 * found.
 */
GstPad *
gst_element_get_compatible_pad (GstElement * element, GstPad * pad,
    const GstCaps * caps)
{
  GstIterator *pads;
  GstPadTemplate *templ;
  GstCaps *templcaps;
  GstPad *foundpad = NULL;
  gboolean done;

  /* FIXME check for caps compatibility */

  g_return_val_if_fail (GST_IS_ELEMENT (element), NULL);
  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
      "finding pad in %s compatible with %s:%s",
      GST_ELEMENT_NAME (element), GST_DEBUG_PAD_NAME (pad));

  g_return_val_if_fail (GST_PAD_PEER (pad) == NULL, NULL);

  done = FALSE;
  /* try to get an existing unlinked pad */
  pads = gst_element_iterate_pads (element);
  while (!done) {
    gpointer padptr;

    switch (gst_iterator_next (pads, &padptr)) {
      case GST_ITERATOR_OK:
      {
        GstPad *peer;
        GstPad *current;

        current = GST_PAD (padptr);

        GST_CAT_LOG (GST_CAT_ELEMENT_PADS, "examining pad %s:%s",
            GST_DEBUG_PAD_NAME (current));

        peer = gst_pad_get_peer (current);

        if (peer == NULL && gst_pad_can_link (pad, current)) {

          GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
              "found existing unlinked pad %s:%s",
              GST_DEBUG_PAD_NAME (current));

          gst_iterator_free (pads);

          return current;
        } else {
          GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "unreffing pads");

          gst_object_unref (current);
          if (peer)
            gst_object_unref (peer);
        }
        break;
      }
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pads);
        break;
      case GST_ITERATOR_ERROR:
        g_assert_not_reached ();
        break;
    }
  }
  gst_iterator_free (pads);

  /* try to create a new one */
  /* requesting is a little crazy, we need a template. Let's create one */
  templcaps = gst_pad_get_caps (pad);

  templ = gst_pad_template_new ((gchar *) GST_PAD_NAME (pad),
      GST_PAD_DIRECTION (pad), GST_PAD_ALWAYS, templcaps);
  foundpad = gst_element_request_compatible_pad (element, templ);
  gst_object_unref (templ);

  if (foundpad) {
    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
        "found existing request pad %s:%s", GST_DEBUG_PAD_NAME (foundpad));
    return foundpad;
  }

  GST_CAT_INFO_OBJECT (GST_CAT_ELEMENT_PADS, element,
      "Could not find a compatible pad to link to %s:%s",
      GST_DEBUG_PAD_NAME (pad));
  return NULL;
}

/**
 * gst_element_state_get_name:
 * @state: a #GstState to get the name of.
 *
 * Gets a string representing the given state.
 *
 * Returns: a string with the name of the state.
 */
const gchar *
gst_element_state_get_name (GstState state)
{
  switch (state) {
#ifdef GST_DEBUG_COLOR
    case GST_STATE_VOID_PENDING:
      return "VOID_PENDING";
      break;
    case GST_STATE_NULL:
      return "\033[01;34mNULL\033[00m";
      break;
    case GST_STATE_READY:
      return "\033[01;31mREADY\033[00m";
      break;
    case GST_STATE_PLAYING:
      return "\033[01;32mPLAYING\033[00m";
      break;
    case GST_STATE_PAUSED:
      return "\033[01;33mPAUSED\033[00m";
      break;
    default:
      /* This is a memory leak */
      return g_strdup_printf ("\033[01;35;41mUNKNOWN!\033[00m(%d)", state);
#else
    case GST_STATE_VOID_PENDING:
      return "VOID_PENDING";
      break;
    case GST_STATE_NULL:
      return "NULL";
      break;
    case GST_STATE_READY:
      return "READY";
      break;
    case GST_STATE_PLAYING:
      return "PLAYING";
      break;
    case GST_STATE_PAUSED:
      return "PAUSED";
      break;
    default:
      /* This is a memory leak */
      return g_strdup_printf ("UNKNOWN!(%d)", state);
#endif
  }
  return "";
}

/**
 * gst_element_factory_can_src_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can source the given capability.
 *
 * Returns: true if it can src the capabilities
 */
gboolean
gst_element_factory_can_src_caps (GstElementFactory * factory,
    const GstCaps * caps)
{
  GList *templates;

  g_return_val_if_fail (factory != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  templates = factory->staticpadtemplates;

  while (templates) {
    GstStaticPadTemplate *template = (GstStaticPadTemplate *) templates->data;

    if (template->direction == GST_PAD_SRC) {
      if (gst_caps_is_always_compatible (gst_static_caps_get (&template->
                  static_caps), caps))
        return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}

/**
 * gst_element_factory_can_sink_caps :
 * @factory: factory to query
 * @caps: the caps to check
 *
 * Checks if the factory can sink the given capability.
 *
 * Returns: true if it can sink the capabilities
 */
gboolean
gst_element_factory_can_sink_caps (GstElementFactory * factory,
    const GstCaps * caps)
{
  GList *templates;

  g_return_val_if_fail (factory != NULL, FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  templates = factory->staticpadtemplates;

  while (templates) {
    GstStaticPadTemplate *template = (GstStaticPadTemplate *) templates->data;

    if (template->direction == GST_PAD_SINK) {
      if (gst_caps_is_always_compatible (caps,
              gst_static_caps_get (&template->static_caps)))
        return TRUE;
    }
    templates = g_list_next (templates);
  }

  return FALSE;
}


/* if return val is true, *direct_child is a caller-owned ref on the direct
 * child of ancestor that is part of object's ancestry */
static gboolean
object_has_ancestor (GstObject * object, GstObject * ancestor,
    GstObject ** direct_child)
{
  GstObject *child, *parent;

  if (direct_child)
    *direct_child = NULL;

  child = gst_object_ref (object);
  parent = gst_object_get_parent (object);

  while (parent) {
    if (ancestor == parent) {
      if (direct_child)
        *direct_child = child;
      else
        gst_object_unref (child);
      gst_object_unref (parent);
      return TRUE;
    }

    gst_object_unref (child);
    child = parent;
    parent = gst_object_get_parent (parent);
  }

  gst_object_unref (child);

  return FALSE;
}

/* caller owns return */
static GstObject *
find_common_root (GstObject * o1, GstObject * o2)
{
  GstObject *top = o1;
  GstObject *kid1, *kid2;
  GstObject *root = NULL;

  while (GST_OBJECT_PARENT (top))
    top = GST_OBJECT_PARENT (top);

  /* the itsy-bitsy spider... */

  if (!object_has_ancestor (o2, top, &kid2))
    return NULL;

  root = gst_object_ref (top);
  while (TRUE) {
    if (!object_has_ancestor (o1, kid2, &kid1)) {
      gst_object_unref (kid2);
      return root;
    }
    root = kid2;
    if (!object_has_ancestor (o2, kid1, &kid2)) {
      gst_object_unref (kid1);
      return root;
    }
    root = kid1;
  }
}

/* caller does not own return */
static GstPad *
ghost_up (GstElement * e, GstPad * pad)
{
  static gint ghost_pad_index = 0;
  GstPad *gpad;
  gchar *name;
  GstObject *parent = GST_OBJECT_PARENT (e);

  name = g_strdup_printf ("ghost%d", ghost_pad_index++);
  gpad = gst_ghost_pad_new (name, pad);
  g_free (name);

  if (!gst_element_add_pad ((GstElement *) parent, gpad)) {
    g_warning ("Pad named %s already exists in element %s\n",
        GST_OBJECT_NAME (gpad), GST_OBJECT_NAME (parent));
    gst_object_unref ((GstObject *) gpad);
    return NULL;
  }

  return gpad;
}

static void
remove_pad (gpointer ppad, gpointer unused)
{
  GstPad *pad = ppad;

  if (!gst_element_remove_pad ((GstElement *) GST_OBJECT_PARENT (pad), pad))
    g_warning ("Couldn't remove pad %s from element %s",
        GST_OBJECT_NAME (pad), GST_OBJECT_NAME (GST_OBJECT_PARENT (pad)));
}

static gboolean
prepare_link_maybe_ghosting (GstPad ** src, GstPad ** sink,
    GSList ** pads_created)
{
  GstObject *root;
  GstObject *e1, *e2;
  GSList *pads_created_local = NULL;

  g_assert (pads_created);

  e1 = GST_OBJECT_PARENT (*src);
  e2 = GST_OBJECT_PARENT (*sink);

  if (GST_OBJECT_PARENT (e1) == GST_OBJECT_PARENT (e2)) {
    GST_CAT_INFO (GST_CAT_PADS, "%s and %s in same bin, no need for ghost pads",
        GST_OBJECT_NAME (e1), GST_OBJECT_NAME (e2));
    return TRUE;
  }

  GST_CAT_INFO (GST_CAT_PADS, "%s and %s not in same bin, making ghost pads",
      GST_OBJECT_NAME (e1), GST_OBJECT_NAME (e2));

  /* we need to setup some ghost pads */
  root = find_common_root (e1, e2);
  if (!root) {
    g_warning
        ("Trying to connect elements that don't share a common ancestor: %s and %s\n",
        GST_ELEMENT_NAME (e1), GST_ELEMENT_NAME (e2));
    return FALSE;
  }

  while (GST_OBJECT_PARENT (e1) != root) {
    *src = ghost_up ((GstElement *) e1, *src);
    if (!*src)
      goto cleanup_fail;
    e1 = GST_OBJECT_PARENT (*src);
    pads_created_local = g_slist_prepend (pads_created_local, *src);
  }
  while (GST_OBJECT_PARENT (e2) != root) {
    *sink = ghost_up ((GstElement *) e2, *sink);
    if (!*sink)
      goto cleanup_fail;
    e2 = GST_OBJECT_PARENT (*sink);
    pads_created_local = g_slist_prepend (pads_created_local, *sink);
  }

  gst_object_unref (root);
  *pads_created = g_slist_concat (*pads_created, pads_created_local);
  return TRUE;

cleanup_fail:
  gst_object_unref (root);
  g_slist_foreach (pads_created_local, remove_pad, NULL);
  g_slist_free (pads_created_local);
  return FALSE;
}

static gboolean
pad_link_maybe_ghosting (GstPad * src, GstPad * sink)
{
  GSList *pads_created = NULL;
  gboolean ret;

  if (!prepare_link_maybe_ghosting (&src, &sink, &pads_created)) {
    ret = FALSE;
  } else {
    ret = (gst_pad_link (src, sink) == GST_PAD_LINK_OK);
  }

  if (!ret) {
    g_slist_foreach (pads_created, remove_pad, NULL);
  }
  g_slist_free (pads_created);

  return ret;
}

/**
 * gst_element_link_pads:
 * @src: a #GstElement containing the source pad.
 * @srcpadname: the name of the #GstPad in source element or NULL for any pad.
 * @dest: the #GstElement containing the destination pad.
 * @destpadname: the name of the #GstPad in destination element,
 * or NULL for any pad.
 *
 * Links the two named pads of the source and destination elements.
 * Side effect is that if one of the pads has no parent, it becomes a
 * child of the parent of the other element.  If they have different
 * parents, the link fails.
 *
 * Returns: TRUE if the pads could be linked, FALSE otherwise.
 */
gboolean
gst_element_link_pads (GstElement * src, const gchar * srcpadname,
    GstElement * dest, const gchar * destpadname)
{
  const GList *srcpads, *destpads, *srctempls, *desttempls, *l;
  GstPad *srcpad, *destpad;
  GstPadTemplate *srctempl, *desttempl;
  GstElementClass *srcclass, *destclass;

  /* checks */
  g_return_val_if_fail (GST_IS_ELEMENT (src), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (dest), FALSE);

  srcclass = GST_ELEMENT_GET_CLASS (src);
  destclass = GST_ELEMENT_GET_CLASS (dest);

  GST_CAT_INFO (GST_CAT_ELEMENT_PADS,
      "trying to link element %s:%s to element %s:%s", GST_ELEMENT_NAME (src),
      srcpadname ? srcpadname : "(any)", GST_ELEMENT_NAME (dest),
      destpadname ? destpadname : "(any)");

  /* get a src pad */
  if (srcpadname) {
    /* name specified, look it up */
    srcpad = gst_element_get_pad (src, srcpadname);
    if (!srcpad) {
      GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "no pad %s:%s",
          GST_ELEMENT_NAME (src), srcpadname);
      return FALSE;
    } else {
      if (!(GST_PAD_DIRECTION (srcpad) == GST_PAD_SRC)) {
        GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "pad %s:%s is no src pad",
            GST_DEBUG_PAD_NAME (srcpad));
        gst_object_unref (srcpad);
        return FALSE;
      }
      if (GST_PAD_PEER (srcpad) != NULL) {
        GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "pad %s:%s is already linked",
            GST_DEBUG_PAD_NAME (srcpad));
        gst_object_unref (srcpad);
        return FALSE;
      }
    }
    srcpads = NULL;
  } else {
    /* no name given, get the first available pad */
    GST_OBJECT_LOCK (src);
    srcpads = GST_ELEMENT_PADS (src);
    srcpad = srcpads ? GST_PAD_CAST (srcpads->data) : NULL;
    if (srcpad)
      gst_object_ref (srcpad);
    GST_OBJECT_UNLOCK (src);
  }

  /* get a destination pad */
  if (destpadname) {
    /* name specified, look it up */
    destpad = gst_element_get_pad (dest, destpadname);
    if (!destpad) {
      GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "no pad %s:%s",
          GST_ELEMENT_NAME (dest), destpadname);
      return FALSE;
    } else {
      if (!(GST_PAD_DIRECTION (destpad) == GST_PAD_SINK)) {
        GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "pad %s:%s is no sink pad",
            GST_DEBUG_PAD_NAME (destpad));
        gst_object_unref (destpad);
        return FALSE;
      }
      if (GST_PAD_PEER (destpad) != NULL) {
        GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "pad %s:%s is already linked",
            GST_DEBUG_PAD_NAME (destpad));
        gst_object_unref (destpad);
        return FALSE;
      }
    }
    destpads = NULL;
  } else {
    /* no name given, get the first available pad */
    GST_OBJECT_LOCK (dest);
    destpads = GST_ELEMENT_PADS (dest);
    destpad = destpads ? GST_PAD_CAST (destpads->data) : NULL;
    if (destpad)
      gst_object_ref (destpad);
    GST_OBJECT_UNLOCK (dest);
  }

  if (srcpadname && destpadname) {
    gboolean result;

    /* two explicitly specified pads */
    result = pad_link_maybe_ghosting (srcpad, destpad);

    gst_object_unref (srcpad);
    gst_object_unref (destpad);

    return result;
  }

  if (srcpad) {
    /* loop through the allowed pads in the source, trying to find a
     * compatible destination pad */
    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
        "looping through allowed src and dest pads");
    do {
      GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "trying src pad %s:%s",
          GST_DEBUG_PAD_NAME (srcpad));
      if ((GST_PAD_DIRECTION (srcpad) == GST_PAD_SRC) &&
          (GST_PAD_PEER (srcpad) == NULL)) {
        GstPad *temp;

        if (destpadname) {
          temp = destpad;
          gst_object_ref (temp);
        } else {
          temp = gst_element_get_compatible_pad (dest, srcpad, NULL);
        }

        if (temp && pad_link_maybe_ghosting (srcpad, temp)) {
          GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "linked pad %s:%s to pad %s:%s",
              GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (temp));
          if (destpad)
            gst_object_unref (destpad);
          gst_object_unref (srcpad);
          gst_object_unref (temp);
          return TRUE;
        }

        if (temp) {
          gst_object_unref (temp);
        }
      }
      /* find a better way for this mess */
      if (srcpads) {
        srcpads = g_list_next (srcpads);
        if (srcpads) {
          gst_object_unref (srcpad);
          srcpad = GST_PAD_CAST (srcpads->data);
          gst_object_ref (srcpad);
        }
      }
    } while (srcpads);
  }
  if (srcpadname) {
    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "no link possible from %s:%s to %s",
        GST_DEBUG_PAD_NAME (srcpad), GST_ELEMENT_NAME (dest));
    if (srcpad)
      gst_object_unref (srcpad);
    srcpad = NULL;
    if (destpad)
      gst_object_unref (destpad);
    destpad = NULL;
  } else {
    if (srcpad)
      gst_object_unref (srcpad);
    srcpad = NULL;
  }

  if (destpad) {
    /* loop through the existing pads in the destination */
    do {
      GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "trying dest pad %s:%s",
          GST_DEBUG_PAD_NAME (destpad));
      if ((GST_PAD_DIRECTION (destpad) == GST_PAD_SINK) &&
          (GST_PAD_PEER (destpad) == NULL)) {
        GstPad *temp = gst_element_get_compatible_pad (src, destpad, NULL);

        if (temp && pad_link_maybe_ghosting (temp, destpad)) {
          GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "linked pad %s:%s to pad %s:%s",
              GST_DEBUG_PAD_NAME (temp), GST_DEBUG_PAD_NAME (destpad));
          gst_object_unref (temp);
          gst_object_unref (destpad);
          if (srcpad)
            gst_object_unref (srcpad);
          return TRUE;
        }
        if (temp) {
          gst_object_unref (temp);
        }
      }
      if (destpads) {
        destpads = g_list_next (destpads);
        if (destpads) {
          gst_object_unref (destpad);
          destpad = GST_PAD_CAST (destpads->data);
          gst_object_ref (destpad);
        }
      }
    } while (destpads);
  }

  if (destpadname) {
    GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "no link possible from %s to %s:%s",
        GST_ELEMENT_NAME (src), GST_DEBUG_PAD_NAME (destpad));
    gst_object_unref (destpad);
    if (srcpad)
      gst_object_unref (srcpad);
    return FALSE;
  } else {
    if (srcpad)
      gst_object_unref (srcpad);
    srcpad = NULL;
    if (destpad)
      gst_object_unref (destpad);
    destpad = NULL;
  }

  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
      "we might have request pads on both sides, checking...");
  srctempls = gst_element_class_get_pad_template_list (srcclass);
  desttempls = gst_element_class_get_pad_template_list (destclass);

  if (srctempls && desttempls) {
    while (srctempls) {
      srctempl = (GstPadTemplate *) srctempls->data;
      if (srctempl->presence == GST_PAD_REQUEST) {
        for (l = desttempls; l; l = l->next) {
          desttempl = (GstPadTemplate *) l->data;
          if (desttempl->presence == GST_PAD_REQUEST &&
              desttempl->direction != srctempl->direction) {
            if (gst_caps_is_always_compatible (gst_pad_template_get_caps
                    (srctempl), gst_pad_template_get_caps (desttempl))) {
              srcpad =
                  gst_element_get_request_pad (src, srctempl->name_template);
              destpad =
                  gst_element_get_request_pad (dest, desttempl->name_template);
              if (pad_link_maybe_ghosting (srcpad, destpad)) {
                GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS,
                    "linked pad %s:%s to pad %s:%s",
                    GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (destpad));
                gst_object_unref (srcpad);
                gst_object_unref (destpad);
                return TRUE;
              }
              /* it failed, so we release the request pads */
              gst_element_release_request_pad (src, srcpad);
              gst_element_release_request_pad (dest, destpad);
            }
          }
        }
      }
      srctempls = srctempls->next;
    }
  }

  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "no link possible from %s to %s",
      GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (dest));
  return FALSE;
}

/**
 * gst_element_link_pads_filtered:
 * @src: a #GstElement containing the source pad.
 * @srcpadname: the name of the #GstPad in source element or NULL for any pad.
 * @dest: the #GstElement containing the destination pad.
 * @destpadname: the name of the #GstPad in destination element or NULL for any pad.
 * @filter: the #GstCaps to filter the link, or #NULL for no filter.
 *
 * Links the two named pads of the source and destination elements. Side effect
 * is that if one of the pads has no parent, it becomes a child of the parent of
 * the other element. If they have different parents, the link fails. If @caps
 * is not #NULL, makes sure that the caps of the link is a subset of @caps.
 *
 * Returns: TRUE if the pads could be linked, FALSE otherwise.
 */
gboolean
gst_element_link_pads_filtered (GstElement * src, const gchar * srcpadname,
    GstElement * dest, const gchar * destpadname, GstCaps * filter)
{
  /* checks */
  g_return_val_if_fail (GST_IS_ELEMENT (src), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (dest), FALSE);
  g_return_val_if_fail (filter == NULL || GST_IS_CAPS (filter), FALSE);

  if (filter) {
    GstElement *capsfilter;
    GstObject *parent;
    GstState state, pending;

    capsfilter = gst_element_factory_make ("capsfilter", NULL);
    if (!capsfilter) {
      GST_ERROR ("Could not make a capsfilter");
      return FALSE;
    }

    parent = gst_object_get_parent (GST_OBJECT (src));
    g_return_val_if_fail (GST_IS_BIN (parent), FALSE);

    gst_element_get_state (GST_ELEMENT_CAST (parent), &state, &pending, 0);

    if (!gst_bin_add (GST_BIN (parent), capsfilter)) {
      GST_ERROR ("Could not add capsfilter");
      gst_object_unref (capsfilter);
      gst_object_unref (parent);
      return FALSE;
    }

    if (pending != GST_STATE_VOID_PENDING)
      state = pending;

    gst_element_set_state (capsfilter, state);

    gst_object_unref (parent);

    g_object_set (capsfilter, "caps", filter, NULL);

    if (gst_element_link_pads (src, srcpadname, capsfilter, "sink")
        && gst_element_link_pads (capsfilter, "src", dest, destpadname)) {
      return TRUE;
    } else {
      GST_INFO ("Could not link elements");
      gst_bin_remove (GST_BIN (GST_OBJECT_PARENT (capsfilter)), capsfilter);
      /* will unref and unlink as appropriate */
      return FALSE;
    }
  } else {
    return gst_element_link_pads (src, srcpadname, dest, destpadname);
  }
}

/**
 * gst_element_link:
 * @src: a #GstElement containing the source pad.
 * @dest: the #GstElement containing the destination pad.
 *
 * Links @src to @dest. The link must be from source to
 * destination; the other direction will not be tried. The function looks for
 * existing pads that aren't linked yet. It will request new pads if necessary.
 * If multiple links are possible, only one is established.
 *
 * Returns: TRUE if the elements could be linked, FALSE otherwise.
 */
gboolean
gst_element_link (GstElement * src, GstElement * dest)
{
  return gst_element_link_pads_filtered (src, NULL, dest, NULL, NULL);
}

/**
 * gst_element_link_many:
 * @element_1: the first #GstElement in the link chain.
 * @element_2: the second #GstElement in the link chain.
 * @...: the NULL-terminated list of elements to link in order.
 *
 * Chain together a series of elements. Uses gst_element_link().
 *
 * Returns: TRUE on success, FALSE otherwise.
 */
gboolean
gst_element_link_many (GstElement * element_1, GstElement * element_2, ...)
{
  va_list args;

  g_return_val_if_fail (GST_IS_ELEMENT (element_1), FALSE);
  g_return_val_if_fail (GST_IS_ELEMENT (element_2), FALSE);

  va_start (args, element_2);

  while (element_2) {
    if (!gst_element_link (element_1, element_2))
      return FALSE;

    element_1 = element_2;
    element_2 = va_arg (args, GstElement *);
  }

  va_end (args);

  return TRUE;
}

/**
 * gst_element_link_filtered:
 * @src: a #GstElement containing the source pad.
 * @dest: the #GstElement containing the destination pad.
 * @filter: the #GstCaps to filter the link, or #NULL for no filter.
 *
 * Links @src to @dest using the given caps as filtercaps.
 * The link must be from source to
 * destination; the other direction will not be tried. The function looks for
 * existing pads that aren't linked yet. It will request new pads if necessary.
 * If multiple links are possible, only one is established.
 *
 * Returns: TRUE if the pads could be linked, FALSE otherwise.
 */
gboolean
gst_element_link_filtered (GstElement * src, GstElement * dest,
    GstCaps * filter)
{
  return gst_element_link_pads_filtered (src, NULL, dest, NULL, filter);
}

/**
 * gst_element_unlink_pads:
 * @src: a #GstElement containing the source pad.
 * @srcpadname: the name of the #GstPad in source element.
 * @dest: a #GstElement containing the destination pad.
 * @destpadname: the name of the #GstPad in destination element.
 *
 * Unlinks the two named pads of the source and destination elements.
 */
void
gst_element_unlink_pads (GstElement * src, const gchar * srcpadname,
    GstElement * dest, const gchar * destpadname)
{
  GstPad *srcpad, *destpad;

  g_return_if_fail (src != NULL);
  g_return_if_fail (GST_IS_ELEMENT (src));
  g_return_if_fail (srcpadname != NULL);
  g_return_if_fail (dest != NULL);
  g_return_if_fail (GST_IS_ELEMENT (dest));
  g_return_if_fail (destpadname != NULL);

  /* obtain the pads requested */
  srcpad = gst_element_get_pad (src, srcpadname);
  if (srcpad == NULL) {
    GST_WARNING_OBJECT (src, "source element has no pad \"%s\"", srcpadname);
    return;
  }
  destpad = gst_element_get_pad (dest, destpadname);
  if (srcpad == NULL) {
    GST_WARNING_OBJECT (dest, "destination element has no pad \"%s\"",
        destpadname);
    return;
  }

  /* we're satisified they can be unlinked, let's do it */
  gst_pad_unlink (srcpad, destpad);
}

/**
 * gst_element_unlink_many:
 * @element_1: the first #GstElement in the link chain.
 * @element_2: the second #GstElement in the link chain.
 * @...: the NULL-terminated list of elements to unlink in order.
 *
 * Unlinks a series of elements. Uses gst_element_unlink().
 */
void
gst_element_unlink_many (GstElement * element_1, GstElement * element_2, ...)
{
  va_list args;

  g_return_if_fail (element_1 != NULL && element_2 != NULL);
  g_return_if_fail (GST_IS_ELEMENT (element_1) && GST_IS_ELEMENT (element_2));

  va_start (args, element_2);

  while (element_2) {
    gst_element_unlink (element_1, element_2);

    element_1 = element_2;
    element_2 = va_arg (args, GstElement *);
  }

  va_end (args);
}

/**
 * gst_element_unlink:
 * @src: the source #GstElement to unlink.
 * @dest: the sink #GstElement to unlink.
 *
 * Unlinks all source pads of the source element with all sink pads
 * of the sink element to which they are linked.
 */
void
gst_element_unlink (GstElement * src, GstElement * dest)
{
  GstIterator *pads;
  gboolean done = FALSE;

  g_return_if_fail (GST_IS_ELEMENT (src));
  g_return_if_fail (GST_IS_ELEMENT (dest));

  GST_CAT_DEBUG (GST_CAT_ELEMENT_PADS, "unlinking \"%s\" and \"%s\"",
      GST_ELEMENT_NAME (src), GST_ELEMENT_NAME (dest));

  pads = gst_element_iterate_pads (src);
  while (!done) {
    gpointer data;

    switch (gst_iterator_next (pads, &data)) {
      case GST_ITERATOR_OK:
      {
        GstPad *pad = GST_PAD_CAST (data);

        if (GST_PAD_IS_SRC (pad)) {
          GstPad *peerpad = gst_pad_get_peer (pad);

          /* see if the pad is connected and is really a pad
           * of dest */
          if (peerpad) {
            GstElement *peerelem;

            peerelem = gst_pad_get_parent_element (peerpad);

            if (peerelem == dest) {
              gst_pad_unlink (pad, peerpad);
            }
            if (peerelem)
              gst_object_unref (peerelem);

            gst_object_unref (peerpad);
          }
        }
        gst_object_unref (pad);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (pads);
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }
}

/**
 * gst_element_query_position:
 * @element: a #GstElement to invoke the position query on.
 * @format: a pointer to the #GstFormat asked for.
 *          On return contains the #GstFormat used.
 * @cur: A location in which to store the current position, or NULL.
 *
 * Queries an element for the stream position.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_element_query_position (GstElement * element, GstFormat * format,
    gint64 * cur)
{
  GstQuery *query;
  gboolean ret;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  query = gst_query_new_position (*format);
  ret = gst_element_query (element, query);

  if (ret)
    gst_query_parse_position (query, format, cur);

  gst_query_unref (query);

  return ret;
}

/**
 * gst_element_query_duration:
 * @element: a #GstElement to invoke the duration query on.
 * @format: a pointer to the #GstFormat asked for.
 *          On return contains the #GstFormat used.
 * @duration: A location in which to store the total duration, or NULL.
 *
 * Queries an element for the total stream duration.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_element_query_duration (GstElement * element, GstFormat * format,
    gint64 * duration)
{
  GstQuery *query;
  gboolean ret;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  query = gst_query_new_duration (*format);
  ret = gst_element_query (element, query);

  if (ret)
    gst_query_parse_duration (query, format, duration);

  gst_query_unref (query);

  return ret;
}

/**
 * gst_element_query_convert:
 * @element: a #GstElement to invoke the convert query on.
 * @src_format: a #GstFormat to convert from.
 * @src_val: a value to convert.
 * @dest_format: a pointer to the #GstFormat to convert to.
 * @dest_val: a pointer to the result.
 *
 * Queries an element to convert @src_val in @src_format to @dest_format.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_element_query_convert (GstElement * element, GstFormat src_format,
    gint64 src_val, GstFormat * dest_format, gint64 * dest_val)
{
  GstQuery *query;
  gboolean ret;

  g_return_val_if_fail (GST_IS_ELEMENT (element), FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_val != NULL, FALSE);

  if (*dest_format == src_format) {
    *dest_val = src_val;
    return TRUE;
  }

  query = gst_query_new_convert (src_format, src_val, *dest_format);
  ret = gst_element_query (element, query);

  if (ret)
    gst_query_parse_convert (query, NULL, NULL, dest_format, dest_val);

  gst_query_unref (query);

  return ret;
}

/**
 * gst_pad_can_link:
 * @srcpad: the source #GstPad to link.
 * @sinkpad: the sink #GstPad to link.
 *
 * Checks if the source pad and the sink pad can be linked.
 * Both @srcpad and @sinkpad must be unlinked.
 *
 * Returns: TRUE if the pads can be linked, FALSE otherwise.
 */
gboolean
gst_pad_can_link (GstPad * srcpad, GstPad * sinkpad)
{
  /* FIXME This function is gross.  It's almost a direct copy of
   * gst_pad_link_filtered().  Any decent programmer would attempt
   * to merge the two functions, which I will do some day. --ds
   */

  /* generic checks */
  g_return_val_if_fail (GST_IS_PAD (srcpad), FALSE);
  g_return_val_if_fail (GST_IS_PAD (sinkpad), FALSE);

  GST_CAT_INFO (GST_CAT_PADS, "trying to link %s:%s and %s:%s",
      GST_DEBUG_PAD_NAME (srcpad), GST_DEBUG_PAD_NAME (sinkpad));

  /* FIXME: shouldn't we convert this to g_return_val_if_fail? */
  if (GST_PAD_PEER (srcpad) != NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Source pad %s:%s has a peer, failed",
        GST_DEBUG_PAD_NAME (srcpad));
    return FALSE;
  }
  if (GST_PAD_PEER (sinkpad) != NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Sink pad %s:%s has a peer, failed",
        GST_DEBUG_PAD_NAME (sinkpad));
    return FALSE;
  }
  if (!GST_PAD_IS_SRC (srcpad)) {
    GST_CAT_INFO (GST_CAT_PADS, "Src pad %s:%s is not source pad, failed",
        GST_DEBUG_PAD_NAME (srcpad));
    return FALSE;
  }
  if (!GST_PAD_IS_SINK (sinkpad)) {
    GST_CAT_INFO (GST_CAT_PADS, "Sink pad %s:%s is not sink pad, failed",
        GST_DEBUG_PAD_NAME (sinkpad));
    return FALSE;
  }
  if (GST_PAD_PARENT (srcpad) == NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Src pad %s:%s has no parent, failed",
        GST_DEBUG_PAD_NAME (srcpad));
    return FALSE;
  }
  if (GST_PAD_PARENT (sinkpad) == NULL) {
    GST_CAT_INFO (GST_CAT_PADS, "Sink pad %s:%s has no parent, failed",
        GST_DEBUG_PAD_NAME (srcpad));
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_pad_use_fixed_caps:
 * @pad: the pad to use
 *
 * A helper function you can use that sets the
 * @gst_pad_get_fixed_caps_func as the getcaps function for the
 * pad. This way the function will always return the negotiated caps
 * or in case the pad is not negotiated, the padtemplate caps.
 *
 * Use this function on a pad that, once _set_caps() has been called
 * on it, cannot be renegotiated to something else.
 */
void
gst_pad_use_fixed_caps (GstPad * pad)
{
  gst_pad_set_getcaps_function (pad, gst_pad_get_fixed_caps_func);
}

/**
 * gst_pad_get_fixed_caps_func:
 * @pad: the pad to use
 *
 * A helper function you can use as a GetCaps function that
 * will return the currently negotiated caps or the padtemplate
 * when NULL.
 *
 * Returns: The currently negotiated caps or the padtemplate.
 */
GstCaps *
gst_pad_get_fixed_caps_func (GstPad * pad)
{
  GstCaps *result;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  if (GST_PAD_CAPS (pad)) {
    result = GST_PAD_CAPS (pad);

    GST_CAT_DEBUG (GST_CAT_CAPS,
        "using pad caps %p %" GST_PTR_FORMAT, result, result);

    result = gst_caps_ref (result);
    goto done;
  }
  if (GST_PAD_PAD_TEMPLATE (pad)) {
    GstPadTemplate *templ = GST_PAD_PAD_TEMPLATE (pad);

    result = GST_PAD_TEMPLATE_CAPS (templ);
    GST_CAT_DEBUG (GST_CAT_CAPS,
        "using pad template %p with caps %p %" GST_PTR_FORMAT, templ, result,
        result);

    result = gst_caps_ref (result);
    goto done;
  }
  GST_CAT_DEBUG (GST_CAT_CAPS, "pad has no caps");
  result = gst_caps_new_empty ();

done:
  return result;
}

/**
 * gst_pad_get_parent_element:
 * @pad: a pad
 *
 * Gets the parent of @pad, cast to a #GstElement. If a @pad has no parent or
 * its parent is not an element, return NULL.
 *
 * Returns: The parent of the pad. The caller has a reference on the parent, so
 * unref when you're finished with it.
 *
 * MT safe.
 */
GstElement *
gst_pad_get_parent_element (GstPad * pad)
{
  GstObject *p;

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  p = gst_object_get_parent (GST_OBJECT_CAST (pad));

  if (p && !GST_IS_ELEMENT (p)) {
    gst_object_unref (p);
    p = NULL;
  }
  return GST_ELEMENT_CAST (p);
}

/**
 * gst_object_default_error:
 * @source: the #GstObject that initiated the error.
 * @error: the GError.
 * @debug: an additional debug information string, or NULL.
 *
 * A default error function.
 *
 * The default handler will simply print the error string using g_print.
 */
void
gst_object_default_error (GstObject * source, GError * error, gchar * debug)
{
  gchar *name = gst_object_get_path_string (source);

  g_print (_("ERROR: from element %s: %s\n"), name, error->message);
  if (debug)
    g_print (_("Additional debug info:\n%s\n"), debug);

  g_free (name);
}

/**
 * gst_bin_add_many:
 * @bin: a #GstBin
 * @element_1: the #GstElement element to add to the bin
 * @...: additional elements to add to the bin
 *
 * Adds a NULL-terminated list of elements to a bin.  This function is
 * equivalent to calling gst_bin_add() for each member of the list.
 */
void
gst_bin_add_many (GstBin * bin, GstElement * element_1, ...)
{
  va_list args;

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (element_1));

  va_start (args, element_1);

  while (element_1) {
    gst_bin_add (bin, element_1);

    element_1 = va_arg (args, GstElement *);
  }

  va_end (args);
}

/**
 * gst_bin_remove_many:
 * @bin: a #GstBin
 * @element_1: the first #GstElement to remove from the bin
 * @...: NULL-terminated list of elements to remove from the bin
 *
 * Remove a list of elements from a bin. This function is equivalent
 * to calling gst_bin_remove() with each member of the list.
 */
void
gst_bin_remove_many (GstBin * bin, GstElement * element_1, ...)
{
  va_list args;

  g_return_if_fail (GST_IS_BIN (bin));
  g_return_if_fail (GST_IS_ELEMENT (element_1));

  va_start (args, element_1);

  while (element_1) {
    gst_bin_remove (bin, element_1);

    element_1 = va_arg (args, GstElement *);
  }

  va_end (args);
}

static void
gst_element_populate_std_props (GObjectClass * klass, const gchar * prop_name,
    guint arg_id, GParamFlags flags)
{
  GQuark prop_id = g_quark_from_string (prop_name);
  GParamSpec *pspec;

  static GQuark fd_id = 0;
  static GQuark blocksize_id;
  static GQuark bytesperread_id;
  static GQuark dump_id;
  static GQuark filesize_id;
  static GQuark mmapsize_id;
  static GQuark location_id;
  static GQuark offset_id;
  static GQuark silent_id;
  static GQuark touch_id;

  if (!fd_id) {
    fd_id = g_quark_from_static_string ("fd");
    blocksize_id = g_quark_from_static_string ("blocksize");
    bytesperread_id = g_quark_from_static_string ("bytesperread");
    dump_id = g_quark_from_static_string ("dump");
    filesize_id = g_quark_from_static_string ("filesize");
    mmapsize_id = g_quark_from_static_string ("mmapsize");
    location_id = g_quark_from_static_string ("location");
    offset_id = g_quark_from_static_string ("offset");
    silent_id = g_quark_from_static_string ("silent");
    touch_id = g_quark_from_static_string ("touch");
  }

  if (prop_id == fd_id) {
    pspec = g_param_spec_int ("fd", "File-descriptor",
        "File-descriptor for the file being read", 0, G_MAXINT, 0, flags);
  } else if (prop_id == blocksize_id) {
    pspec = g_param_spec_ulong ("blocksize", "Block Size",
        "Block size to read per buffer", 0, G_MAXULONG, 4096, flags);

  } else if (prop_id == bytesperread_id) {
    pspec = g_param_spec_int ("bytesperread", "Bytes per read",
        "Number of bytes to read per buffer", G_MININT, G_MAXINT, 0, flags);

  } else if (prop_id == dump_id) {
    pspec = g_param_spec_boolean ("dump", "Dump",
        "Dump bytes to stdout", FALSE, flags);

  } else if (prop_id == filesize_id) {
    pspec = g_param_spec_int64 ("filesize", "File Size",
        "Size of the file being read", 0, G_MAXINT64, 0, flags);

  } else if (prop_id == mmapsize_id) {
    pspec = g_param_spec_ulong ("mmapsize", "mmap() Block Size",
        "Size in bytes of mmap()d regions", 0, G_MAXULONG, 4 * 1048576, flags);

  } else if (prop_id == location_id) {
    pspec = g_param_spec_string ("location", "File Location",
        "Location of the file to read", NULL, flags);

  } else if (prop_id == offset_id) {
    pspec = g_param_spec_int64 ("offset", "File Offset",
        "Byte offset of current read pointer", 0, G_MAXINT64, 0, flags);

  } else if (prop_id == silent_id) {
    pspec = g_param_spec_boolean ("silent", "Silent", "Don't produce events",
        FALSE, flags);

  } else if (prop_id == touch_id) {
    pspec = g_param_spec_boolean ("touch", "Touch read data",
        "Touch data to force disk read before " "push ()", TRUE, flags);
  } else {
    g_warning ("Unknown - 'standard' property '%s' id %d from klass %s",
        prop_name, arg_id, g_type_name (G_OBJECT_CLASS_TYPE (klass)));
    pspec = NULL;
  }

  if (pspec) {
    g_object_class_install_property (klass, arg_id, pspec);
  }
}

/**
 * gst_element_class_install_std_props:
 * @klass: the #GstElementClass to add the properties to.
 * @first_name: the name of the first property.
 * in a NULL terminated
 * @...: the id and flags of the first property, followed by
 * further 'name', 'id', 'flags' triplets and terminated by NULL.
 *
 * Adds a list of standardized properties with types to the @klass.
 * the id is for the property switch in your get_prop method, and
 * the flags determine readability / writeability.
 **/
void
gst_element_class_install_std_props (GstElementClass * klass,
    const gchar * first_name, ...)
{
  const char *name;

  va_list args;

  g_return_if_fail (GST_IS_ELEMENT_CLASS (klass));

  va_start (args, first_name);

  name = first_name;

  while (name) {
    int arg_id = va_arg (args, int);
    int flags = va_arg (args, int);

    gst_element_populate_std_props ((GObjectClass *) klass, name, arg_id,
        flags);

    name = va_arg (args, char *);
  }

  va_end (args);
}


/**
 * gst_buffer_merge:
 * @buf1: the first source #GstBuffer to merge.
 * @buf2: the second source #GstBuffer to merge.
 *
 * Create a new buffer that is the concatenation of the two source
 * buffers.  The original source buffers will not be modified or
 * unref'd.  Make sure you unref the source buffers if they are not used
 * anymore afterwards.
 *
 * If the buffers point to contiguous areas of memory, the buffer
 * is created without copying the data.
 *
 * Returns: the new #GstBuffer which is the concatenation of the source buffers.
 */
GstBuffer *
gst_buffer_merge (GstBuffer * buf1, GstBuffer * buf2)
{
  GstBuffer *result;

  /* we're just a specific case of the more general gst_buffer_span() */
  result = gst_buffer_span (buf1, 0, buf2, buf1->size + buf2->size);

  return result;
}

/**
 * gst_buffer_join:
 * @buf1: the first source #GstBuffer.
 * @buf2: the second source #GstBuffer.
 *
 * Create a new buffer that is the concatenation of the two source
 * buffers, and unrefs the original source buffers.
 *
 * If the buffers point to contiguous areas of memory, the buffer
 * is created without copying the data.
 *
 * Returns: the new #GstBuffer which is the concatenation of the source buffers.
 */
GstBuffer *
gst_buffer_join (GstBuffer * buf1, GstBuffer * buf2)
{
  GstBuffer *result;

  result = gst_buffer_span (buf1, 0, buf2, buf1->size + buf2->size);
  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);

  return result;
}


/**
 * gst_buffer_stamp:
 * @dest: buffer to stamp
 * @src: buffer to stamp from
 *
 * Copies additional information (the timestamp, duration, and offset start 
 * and end) from one buffer to the other.
 *
 * This function does not copy any buffer flags or caps.
 */
void
gst_buffer_stamp (GstBuffer * dest, const GstBuffer * src)
{
  g_return_if_fail (dest != NULL);
  g_return_if_fail (src != NULL);

  GST_BUFFER_TIMESTAMP (dest) = GST_BUFFER_TIMESTAMP (src);
  GST_BUFFER_DURATION (dest) = GST_BUFFER_DURATION (src);
  GST_BUFFER_OFFSET (dest) = GST_BUFFER_OFFSET (src);
  GST_BUFFER_OFFSET_END (dest) = GST_BUFFER_OFFSET_END (src);
}

static gboolean
intersect_caps_func (GstPad * pad, GValue * ret, GstPad * orig)
{
  if (pad != orig) {
    GstCaps *peercaps, *existing;

    existing = g_value_get_pointer (ret);
    peercaps = gst_pad_peer_get_caps (pad);
    if (peercaps == NULL)
      peercaps = gst_caps_new_any ();
    g_value_set_pointer (ret, gst_caps_intersect (existing, peercaps));
    gst_caps_unref (existing);
    gst_caps_unref (peercaps);
  }
  gst_object_unref (pad);
  return TRUE;
}

/**
 * gst_pad_proxy_getcaps:
 * @pad: a #GstPad to proxy.
 *
 * Calls gst_pad_get_allowed_caps() for every other pad belonging to the
 * same element as @pad, and returns the intersection of the results.
 *
 * This function is useful as a default getcaps function for an element
 * that can handle any stream format, but requires all its pads to have
 * the same caps.  Two such elements are tee and aggregator.
 *
 * Returns: the intersection of the other pads' allowed caps.
 */
GstCaps *
gst_pad_proxy_getcaps (GstPad * pad)
{
  GstElement *element;
  GstCaps *caps, *intersected;
  GstIterator *iter;
  GstIteratorResult res;
  GValue ret = { 0, };

  g_return_val_if_fail (GST_IS_PAD (pad), NULL);

  GST_DEBUG ("proxying getcaps for %s:%s", GST_DEBUG_PAD_NAME (pad));

  element = gst_pad_get_parent_element (pad);
  if (element == NULL)
    return NULL;

  iter = gst_element_iterate_pads (element);

  g_value_init (&ret, G_TYPE_POINTER);
  g_value_set_pointer (&ret, gst_caps_new_any ());

  res = gst_iterator_fold (iter, (GstIteratorFoldFunction) intersect_caps_func,
      &ret, pad);
  gst_iterator_free (iter);

  if (res != GST_ITERATOR_DONE)
    goto pads_changed;

  gst_object_unref (element);

  caps = g_value_get_pointer (&ret);
  g_value_unset (&ret);

  intersected = gst_caps_intersect (caps, gst_pad_get_pad_template_caps (pad));
  gst_caps_unref (caps);

  return intersected;

  /* ERRORS */
pads_changed:
  {
    g_warning ("Pad list changed during capsnego for element %s",
        GST_ELEMENT_NAME (element));
    gst_object_unref (element);
    return NULL;
  }
}

typedef struct
{
  GstPad *orig;
  GstCaps *caps;
} LinkData;

static gboolean
link_fold_func (GstPad * pad, GValue * ret, LinkData * data)
{
  gboolean success = TRUE;

  if (pad != data->orig) {
    success = gst_pad_set_caps (pad, data->caps);
    g_value_set_boolean (ret, success);
  }
  gst_object_unref (pad);

  return success;
}

/**
 * gst_pad_proxy_setcaps
 * @pad: a #GstPad to proxy from
 * @caps: the #GstCaps to link with
 *
 * Calls gst_pad_set_caps() for every other pad belonging to the
 * same element as @pad.  If gst_pad_set_caps() fails on any pad,
 * the proxy setcaps fails. May be used only during negotiation.
 *
 * Returns: TRUE if sucessful
 */
gboolean
gst_pad_proxy_setcaps (GstPad * pad, GstCaps * caps)
{
  GstElement *element;
  GstIterator *iter;
  GstIteratorResult res;
  GValue ret = { 0, };
  LinkData data;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (caps != NULL, FALSE);

  GST_DEBUG ("proxying pad link for %s:%s", GST_DEBUG_PAD_NAME (pad));

  element = gst_pad_get_parent_element (pad);
  if (element == NULL)
    return FALSE;

  iter = gst_element_iterate_pads (element);

  g_value_init (&ret, G_TYPE_BOOLEAN);
  g_value_set_boolean (&ret, TRUE);
  data.orig = pad;
  data.caps = caps;

  res = gst_iterator_fold (iter, (GstIteratorFoldFunction) link_fold_func,
      &ret, &data);
  gst_iterator_free (iter);

  if (res != GST_ITERATOR_DONE)
    goto pads_changed;

  gst_object_unref (element);

  /* ok not to unset the gvalue */
  return g_value_get_boolean (&ret);

  /* ERRORS */
pads_changed:
  {
    g_warning ("Pad list changed during proxy_pad_link for element %s",
        GST_ELEMENT_NAME (element));
    gst_object_unref (element);
    return FALSE;
  }
}

/**
 * gst_pad_query_position:
 * @pad: a #GstPad to invoke the position query on.
 * @format: a pointer to the #GstFormat asked for.
 *          On return contains the #GstFormat used.
 * @cur: A location in which to store the current position, or NULL.
 *
 * Queries a pad for the stream position.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query_position (GstPad * pad, GstFormat * format, gint64 * cur)
{
  GstQuery *query;
  gboolean ret;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  query = gst_query_new_position (*format);
  ret = gst_pad_query (pad, query);

  if (ret)
    gst_query_parse_position (query, format, cur);

  gst_query_unref (query);

  return ret;
}

/**
 * gst_pad_query_duration:
 * @pad: a #GstPad to invoke the duration query on.
 * @format: a pointer to the #GstFormat asked for.
 *          On return contains the #GstFormat used.
 * @duration: A location in which to store the total duration, or NULL.
 *
 * Queries a pad for the total stream duration.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query_duration (GstPad * pad, GstFormat * format, gint64 * duration)
{
  GstQuery *query;
  gboolean ret;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (format != NULL, FALSE);

  query = gst_query_new_duration (*format);
  ret = gst_pad_query (pad, query);

  if (ret)
    gst_query_parse_duration (query, format, duration);

  gst_query_unref (query);

  return ret;
}

/**
 * gst_pad_query_convert:
 * @pad: a #GstPad to invoke the convert query on.
 * @src_format: a #GstFormat to convert from.
 * @src_val: a value to convert.
 * @dest_format: a pointer to the #GstFormat to convert to.
 * @dest_val: a pointer to the result.
 *
 * Queries a pad to convert @src_val in @src_format to @dest_format.
 *
 * Returns: TRUE if the query could be performed.
 */
gboolean
gst_pad_query_convert (GstPad * pad, GstFormat src_format, gint64 src_val,
    GstFormat * dest_format, gint64 * dest_val)
{
  GstQuery *query;
  gboolean ret;

  g_return_val_if_fail (GST_IS_PAD (pad), FALSE);
  g_return_val_if_fail (src_val >= 0, FALSE);
  g_return_val_if_fail (dest_format != NULL, FALSE);
  g_return_val_if_fail (dest_val != NULL, FALSE);

  if (*dest_format == src_format) {
    *dest_val = src_val;
    return TRUE;
  }

  query = gst_query_new_convert (src_format, src_val, *dest_format);
  ret = gst_pad_query (pad, query);

  if (ret)
    gst_query_parse_convert (query, NULL, NULL, dest_format, dest_val);

  gst_query_unref (query);

  return ret;
}

/**
 * gst_atomic_int_set:
 * @atomic_int: pointer to an atomic integer
 * @value: value to set
 *
 * Unconditionally sets the atomic integer to @value.
 */
void
gst_atomic_int_set (gint * atomic_int, gint value)
{
  int ignore;

  *atomic_int = value;
  /* read acts as a memory barrier */
  ignore = g_atomic_int_get (atomic_int);
}

/**
 * gst_pad_add_data_probe:
 * @pad: pad to add the data probe handler to
 * @handler: function to call when data is passed over pad
 * @data: data to pass along with the handler
 *
 * Adds a "data probe" to a pad. This function will be called whenever data
 * passes through a pad. In this case data means both events and buffers. The
 * probe will be called with the data as an argument. Note that the data will
 * have a reference count greater than 1, so it will be immutable -- you must
 * not change it.
 *
 * For source pads, the probe will be called after the blocking function, if any
 * (see gst_pad_set_blocked_async()), but before looking up the peer to chain
 * to. For sink pads, the probe function will be called before configuring the
 * sink with new caps, if any, and before calling the pad's chain function.
 *
 * Your data probe should return TRUE to let the data continue to flow, or FALSE
 * to drop it. Dropping data is rarely useful, but occasionally comes in handy
 * with events.
 *
 * Although probes are implemented internally by connecting @handler to the
 * have-data signal on the pad, if you want to remove a probe it is insufficient
 * to only call g_signal_handler_disconnect on the returned handler id. To
 * remove a probe, use the appropriate function, such as
 * gst_pad_remove_data_probe().
 *
 * Returns: The handler id.
 */
gulong
gst_pad_add_data_probe (GstPad * pad, GCallback handler, gpointer data)
{
  gulong sigid;

  g_return_val_if_fail (GST_IS_PAD (pad), 0);
  g_return_val_if_fail (handler != NULL, 0);

  GST_OBJECT_LOCK (pad);
  sigid = g_signal_connect (pad, "have-data", handler, data);
  GST_PAD_DO_EVENT_SIGNALS (pad)++;
  GST_PAD_DO_BUFFER_SIGNALS (pad)++;
  GST_DEBUG ("adding data probe to pad %s:%s, now %d data, %d event probes",
      GST_DEBUG_PAD_NAME (pad),
      GST_PAD_DO_BUFFER_SIGNALS (pad), GST_PAD_DO_EVENT_SIGNALS (pad));
  GST_OBJECT_UNLOCK (pad);

  return sigid;
}

/**
 * gst_pad_add_event_probe:
 * @pad: pad to add the event probe handler to
 * @handler: function to call when data is passed over pad
 * @data: data to pass along with the handler
 *
 * Adds a probe that will be called for all events passing through a pad. See
 * gst_pad_add_data_probe() for more information.
 *
 * Returns: The handler id
 */
gulong
gst_pad_add_event_probe (GstPad * pad, GCallback handler, gpointer data)
{
  gulong sigid;

  g_return_val_if_fail (GST_IS_PAD (pad), 0);
  g_return_val_if_fail (handler != NULL, 0);

  GST_OBJECT_LOCK (pad);
  sigid = g_signal_connect (pad, "have-data::event", handler, data);
  GST_PAD_DO_EVENT_SIGNALS (pad)++;
  GST_DEBUG ("adding event probe to pad %s:%s, now %d probes",
      GST_DEBUG_PAD_NAME (pad), GST_PAD_DO_EVENT_SIGNALS (pad));
  GST_OBJECT_UNLOCK (pad);

  return sigid;
}

/**
 * gst_pad_add_buffer_probe:
 * @pad: pad to add the buffer probe handler to
 * @handler: function to call when data is passed over pad
 * @data: data to pass along with the handler
 *
 * Adds a probe that will be called for all buffers passing through a pad. See
 * gst_pad_add_data_probe() for more information.
 *
 * Returns: The handler id
 */
gulong
gst_pad_add_buffer_probe (GstPad * pad, GCallback handler, gpointer data)
{
  gulong sigid;

  g_return_val_if_fail (GST_IS_PAD (pad), 0);
  g_return_val_if_fail (handler != NULL, 0);

  GST_OBJECT_LOCK (pad);
  sigid = g_signal_connect (pad, "have-data::buffer", handler, data);
  GST_PAD_DO_BUFFER_SIGNALS (pad)++;
  GST_DEBUG ("adding buffer probe to pad %s:%s, now %d probes",
      GST_DEBUG_PAD_NAME (pad), GST_PAD_DO_BUFFER_SIGNALS (pad));
  GST_OBJECT_UNLOCK (pad);

  return sigid;
}

/**
 * gst_pad_remove_data_probe:
 * @pad: pad to remove the data probe handler from
 * @handler_id: handler id returned from gst_pad_add_data_probe
 *
 * Removes a data probe from @pad.
 */
void
gst_pad_remove_data_probe (GstPad * pad, guint handler_id)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (handler_id > 0);

  GST_OBJECT_LOCK (pad);
  g_signal_handler_disconnect (pad, handler_id);
  GST_PAD_DO_BUFFER_SIGNALS (pad)--;
  GST_PAD_DO_EVENT_SIGNALS (pad)--;
  GST_DEBUG
      ("removed data probe from pad %s:%s, now %d event, %d buffer probes",
      GST_DEBUG_PAD_NAME (pad), GST_PAD_DO_EVENT_SIGNALS (pad),
      GST_PAD_DO_BUFFER_SIGNALS (pad));
  GST_OBJECT_UNLOCK (pad);
}

/**
 * gst_pad_remove_event_probe:
 * @pad: pad to remove the event probe handler from
 * @handler_id: handler id returned from gst_pad_add_event_probe
 *
 * Removes an event probe from @pad.
 */
void
gst_pad_remove_event_probe (GstPad * pad, guint handler_id)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (handler_id > 0);

  GST_OBJECT_LOCK (pad);
  g_signal_handler_disconnect (pad, handler_id);
  GST_PAD_DO_EVENT_SIGNALS (pad)--;
  GST_DEBUG ("removed event probe from pad %s:%s, now %d event probes",
      GST_DEBUG_PAD_NAME (pad), GST_PAD_DO_EVENT_SIGNALS (pad));
  GST_OBJECT_UNLOCK (pad);
}

/**
 * gst_pad_remove_buffer_probe:
 * @pad: pad to remove the buffer probe handler from
 * @handler_id: handler id returned from gst_pad_add_buffer_probe
 *
 * Removes a buffer probe from @pad.
 */
void
gst_pad_remove_buffer_probe (GstPad * pad, guint handler_id)
{
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (handler_id > 0);

  GST_OBJECT_LOCK (pad);
  g_signal_handler_disconnect (pad, handler_id);
  GST_PAD_DO_BUFFER_SIGNALS (pad)--;
  GST_DEBUG ("removed buffer probe from pad %s:%s, now %d buffer probes",
      GST_DEBUG_PAD_NAME (pad), GST_PAD_DO_BUFFER_SIGNALS (pad));
  GST_OBJECT_UNLOCK (pad);
}

/**
 * gst_element_found_tags_for_pad:
 * @element: element for which to post taglist to bus.
 * @pad: pad on which to push tag-event.
 * @list: the taglist to post on the bus and create event from.
 *
 * Posts a message to the bus that new tags were found and pushes the
 * tags as event. Takes ownership of the taglist.
 */
void
gst_element_found_tags_for_pad (GstElement * element,
    GstPad * pad, GstTagList * list)
{
  g_return_if_fail (element != NULL);
  g_return_if_fail (pad != NULL);
  g_return_if_fail (list != NULL);

  gst_pad_push_event (pad, gst_event_new_tag (gst_tag_list_copy (list)));
  gst_element_post_message (element,
      gst_message_new_tag (GST_OBJECT (element), list));
}

static void
push_and_ref (GstPad * pad, GstEvent * event)
{
  gst_pad_push_event (pad, gst_event_ref (event));
}

/**
 * gst_element_found_tags:
 * @element: element for which we found the tags.
 * @list: list of tags.
 *
 * Posts a message to the bus that new tags were found, and pushes an event
 * to all sourcepads. Takes ownership of the taglist.
 */
void
gst_element_found_tags (GstElement * element, GstTagList * list)
{
  GstIterator *iter;
  GstEvent *event;

  g_return_if_fail (element != NULL);
  g_return_if_fail (list != NULL);

  iter = gst_element_iterate_src_pads (element);
  event = gst_event_new_tag (gst_tag_list_copy (list));
  gst_iterator_foreach (iter, (GFunc) push_and_ref, event);
  gst_iterator_free (iter);
  gst_event_unref (event);

  gst_element_post_message (element,
      gst_message_new_tag (GST_OBJECT (element), list));
}
