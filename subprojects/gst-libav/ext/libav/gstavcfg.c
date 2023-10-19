/* GStreamer
 *
 * FFMpeg Configuration
 *
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
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


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstav.h"
#include "gstavvidenc.h"
#include "gstavcfg.h"

#include <string.h>
#include <libavutil/opt.h>

static GQuark avoption_quark;
static GHashTable *generic_overrides = NULL;

static void
make_generic_overrides (void)
{
  g_assert (!generic_overrides);
  generic_overrides = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, (GDestroyNotify) gst_structure_free);

  g_hash_table_insert (generic_overrides, g_strdup ("b"),
      gst_structure_new_empty ("bitrate"));
  g_hash_table_insert (generic_overrides, g_strdup ("ab"),
      gst_structure_new_empty ("bitrate"));
  g_hash_table_insert (generic_overrides, g_strdup ("g"),
      gst_structure_new_empty ("gop-size"));
  g_hash_table_insert (generic_overrides, g_strdup ("bt"),
      gst_structure_new_empty ("bitrate-tolerance"));
  g_hash_table_insert (generic_overrides, g_strdup ("bf"),
      gst_structure_new_empty ("max-bframes"));

  /* Those are exposed through caps */
  g_hash_table_insert (generic_overrides, g_strdup ("profile"),
      gst_structure_new ("profile", "skip", G_TYPE_BOOLEAN, TRUE, NULL));
  g_hash_table_insert (generic_overrides, g_strdup ("level"),
      gst_structure_new ("level", "skip", G_TYPE_BOOLEAN, TRUE, NULL));
  g_hash_table_insert (generic_overrides, g_strdup ("color_primaries"),
      gst_structure_new ("color_primaries", "skip", G_TYPE_BOOLEAN, TRUE,
          NULL));
  g_hash_table_insert (generic_overrides, g_strdup ("color_trc"),
      gst_structure_new ("color_trc", "skip", G_TYPE_BOOLEAN, TRUE, NULL));
  g_hash_table_insert (generic_overrides, g_strdup ("colorspace"),
      gst_structure_new ("colorspace", "skip", G_TYPE_BOOLEAN, TRUE, NULL));
  g_hash_table_insert (generic_overrides, g_strdup ("color_range"),
      gst_structure_new ("color_range", "skip", G_TYPE_BOOLEAN, TRUE, NULL));
}

void
gst_ffmpeg_cfg_init (void)
{
  avoption_quark = g_quark_from_static_string ("ffmpeg-cfg-param-spec-data");
  make_generic_overrides ();
}

static gint
cmp_enum_value (GEnumValue * val1, GEnumValue * val2)
{
  if (val1->value == val2->value)
    return 0;
  return (val1->value > val2->value) ? 1 : -1;
}

static GType
register_enum (const AVClass ** obj, const AVOption * top_opt)
{
  const AVOption *opt = NULL;
  GType res = 0;
  GArray *values;
  gchar *lower_obj_name = g_ascii_strdown ((*obj)->class_name, -1);
  gchar *enum_name = g_strdup_printf ("%s-%s", lower_obj_name, top_opt->unit);
  gboolean none_default = TRUE;
  const gchar *enum_name_strip;

  g_strcanon (enum_name, G_CSET_a_2_z G_CSET_DIGITS, '-');

  /* strip leading '-'s */
  enum_name_strip = enum_name;
  while (enum_name_strip[0] == '-')
    enum_name_strip++;

  if (enum_name_strip[0] == '\0')
    goto done;

  if ((res = g_type_from_name (enum_name_strip)))
    goto done;

  values = g_array_new (TRUE, TRUE, sizeof (GEnumValue));

  while ((opt = av_opt_next (obj, opt))) {
    if (opt->type == AV_OPT_TYPE_CONST && !g_strcmp0 (top_opt->unit, opt->unit)) {
      GEnumValue val;

      val.value = opt->default_val.i64;
      val.value_name = g_strdup (opt->help ? opt->help : opt->name);
      val.value_nick = g_strdup (opt->name);

      if (opt->default_val.i64 == top_opt->default_val.i64)
        none_default = FALSE;

      g_array_append_val (values, val);
    }
  }

  if (values->len) {
    guint i = 0;
    gint cur_val;
    gboolean cur_val_set = FALSE;

    /* Sometimes ffmpeg sets a default value but no named constants with
     * this value, we assume this means "unspecified" and add our own
     */
    if (none_default) {
      GEnumValue val;

      val.value = top_opt->default_val.i64;
      val.value_name = g_strdup ("Unspecified");
      val.value_nick = g_strdup ("unknown");
      g_array_append_val (values, val);
    }

    g_array_sort (values, (GCompareFunc) cmp_enum_value);

    /* Dedup, easy once sorted
     * We do this because ffmpeg can expose multiple names for the
     * same constant, the way we expose enums makes this too confusing.
     */
    while (i < values->len) {
      if (cur_val_set) {
        if (g_array_index (values, GEnumValue, i).value == cur_val) {
          GEnumValue val = g_array_index (values, GEnumValue, i);
          /* Don't leak the strings */
          g_free ((gchar *) val.value_name);
          g_free ((gchar *) val.value_nick);
          g_array_remove_index (values, i);
        } else {
          cur_val = g_array_index (values, GEnumValue, i).value;
          i++;
        }
      } else {
        cur_val = g_array_index (values, GEnumValue, i).value;
        cur_val_set = TRUE;
        i++;
      }
    }

    res = g_enum_register_static (enum_name_strip,
        &g_array_index (values, GEnumValue, 0));

    gst_type_mark_as_plugin_api (res, 0);

    g_array_free (values, FALSE);
  } else {
    g_array_free (values, TRUE);
  }

done:
  g_free (lower_obj_name);
  g_free (enum_name);
  return res;
}

static gint
cmp_flags_value (GFlagsValue * val1, GFlagsValue * val2)
{
  if (val1->value == val2->value)
    return 0;
  return (val1->value > val2->value) ? 1 : -1;
}

static GType
register_flags (const AVClass ** obj, const AVOption * top_opt)
{
  const AVOption *opt = NULL;
  GType res = 0;
  GArray *values;
  gchar *lower_obj_name = g_ascii_strdown ((*obj)->class_name, -1);
  gchar *flags_name = g_strdup_printf ("%s-%s", lower_obj_name, top_opt->unit);
  const gchar *flags_name_strip;

  g_strcanon (flags_name, G_CSET_a_2_z G_CSET_DIGITS, '-');

  /* strip leading '-'s */
  flags_name_strip = flags_name;
  while (flags_name_strip[0] == '-')
    flags_name_strip++;

  if (flags_name_strip[0] == '\0')
    goto done;

  if ((res = g_type_from_name (flags_name_strip)))
    goto done;

  values = g_array_new (TRUE, TRUE, sizeof (GFlagsValue));

  while ((opt = av_opt_next (obj, opt))) {
    if (opt->type == AV_OPT_TYPE_CONST && !g_strcmp0 (top_opt->unit, opt->unit)) {
      GFlagsValue val;

      /* We expose pass manually, hardcoding this isn't very nice, but
       * I don't expect we want to do that sort of things often enough
       * to warrant a general mechanism
       */
      if (!g_strcmp0 (top_opt->name, "flags")) {
        if (opt->default_val.i64 == AV_CODEC_FLAG_QSCALE ||
            opt->default_val.i64 == AV_CODEC_FLAG_PASS1 ||
            opt->default_val.i64 == AV_CODEC_FLAG_PASS2) {
          continue;
        }
      }

      val.value = opt->default_val.i64;
      val.value_name = g_strdup (opt->help ? opt->help : opt->name);
      val.value_nick = g_strdup (opt->name);

      g_array_append_val (values, val);
    }
  }

  if (values->len) {
    g_array_sort (values, (GCompareFunc) cmp_flags_value);

    res =
        g_flags_register_static (flags_name_strip, &g_array_index (values,
            GFlagsValue, 0));

    gst_type_mark_as_plugin_api (res, 0);
    g_array_free (values, FALSE);
  } else
    g_array_free (values, TRUE);

done:
  g_free (lower_obj_name);
  g_free (flags_name);
  return res;
}

/** GstFFMpegTrilian:
 *
 * Since: 1.24
 */

/** GstFFMpegTrilian::auto
 *
 * Since: 1.24
 */

/** GstFFMpegTrilian::on
 *
 * Since: 1.24
 */

/** GstFFMpegTrilian::off
 *
 * Since: 1.24
 */

#define GST_TYPE_FFMPEG_TRILIAN (gst_ffmpeg_trilian_get_type ())
static GType
gst_ffmpeg_trilian_get_type (void)
{
  static const GEnumValue types[] = {
    {-1, "Auto", "auto"},
    {0, "Off", "off"},
    {1, "On", "on"},
    {0, NULL, NULL},
  };
  static gsize id = 0;

  if (g_once_init_enter (&id)) {
    GType gtype = g_enum_register_static ("GstFFMpegTrilian", types);

    gst_type_mark_as_plugin_api (gtype, 0);
    g_once_init_leave (&id, gtype);
  }

  return (GType) id;
}

static guint
install_opts (GObjectClass * gobject_class, const AVClass ** obj, guint prop_id,
    gint flags, const gchar * extra_help, GHashTable * overrides)
{
  const AVOption *opt = NULL;

  while ((opt = av_opt_next (obj, opt))) {
    GParamSpec *pspec = NULL;
    AVOptionRanges *r;
    gdouble min = G_MINDOUBLE;
    gdouble max = G_MAXDOUBLE;
    gchar *help;
    const gchar *name;

    if (overrides && g_hash_table_contains (overrides, opt->name)) {
      gboolean skip;
      const GstStructure *s =
          (GstStructure *) g_hash_table_lookup (overrides, opt->name);

      name = gst_structure_get_name (s);
      if (gst_structure_get_boolean (s, "skip", &skip) && skip) {
        continue;
      }
    } else {
      name = opt->name;
    }

    if ((opt->flags & flags) != flags)
      continue;

    if (g_object_class_find_property (gobject_class, name))
      continue;

    if (av_opt_query_ranges (&r, obj, opt->name, AV_OPT_SEARCH_FAKE_OBJ) >= 0) {
      if (r->nb_ranges == 1) {
        min = r->range[0]->value_min;
        max = r->range[0]->value_max;
      }
      av_opt_freep_ranges (&r);
    }

    help = g_strdup_printf ("%s%s", opt->help, extra_help);

    switch (opt->type) {
      case AV_OPT_TYPE_INT:
        if (opt->unit) {
          GType enum_gtype;
          enum_gtype = register_enum (obj, opt);

          if (enum_gtype) {
            pspec = g_param_spec_enum (name, name, help,
                enum_gtype, opt->default_val.i64, G_PARAM_READWRITE);
            g_object_class_install_property (gobject_class, prop_id++, pspec);
          } else {              /* Some options have a unit but no named constants associated */
            pspec = g_param_spec_int (name, name, help,
                (gint) min, (gint) max, opt->default_val.i64,
                G_PARAM_READWRITE);
            g_object_class_install_property (gobject_class, prop_id++, pspec);
          }
        } else {
          pspec = g_param_spec_int (name, name, help,
              (gint) min, (gint) max, opt->default_val.i64, G_PARAM_READWRITE);
          g_object_class_install_property (gobject_class, prop_id++, pspec);
        }
        break;
      case AV_OPT_TYPE_FLAGS:
        if (opt->unit) {
          GType flags_gtype;
          flags_gtype = register_flags (obj, opt);

          if (flags_gtype) {
            pspec = g_param_spec_flags (name, name, help,
                flags_gtype, opt->default_val.i64, G_PARAM_READWRITE);
            g_object_class_install_property (gobject_class, prop_id++, pspec);
          }
        }
        break;
      case AV_OPT_TYPE_DURATION:       /* Fall through */
      case AV_OPT_TYPE_INT64:
        /* FIXME 2.0: Workaround for worst property related API change. We
         * continue using a 32 bit integer for the bitrate property as
         * otherwise too much existing code will fail at runtime.
         *
         * See https://gitlab.freedesktop.org/gstreamer/gst-libav/issues/41#note_142808 */
        if (g_strcmp0 (name, "bitrate") == 0) {
          pspec = g_param_spec_int (name, name, help,
              (gint) MAX (min, G_MININT), (gint) MIN (max, G_MAXINT),
              (gint) opt->default_val.i64, G_PARAM_READWRITE);
        } else {
          /* ffmpeg expresses all ranges with doubles, this is sad */
          pspec = g_param_spec_int64 (name, name, help,
              (min == (gdouble) INT64_MIN ? INT64_MIN : (gint64) min),
              (max == (gdouble) INT64_MAX ? INT64_MAX : (gint64) max),
              opt->default_val.i64, G_PARAM_READWRITE);
        }
        g_object_class_install_property (gobject_class, prop_id++, pspec);
        break;
      case AV_OPT_TYPE_DOUBLE:
        pspec = g_param_spec_double (name, name, help,
            min, max, opt->default_val.dbl, G_PARAM_READWRITE);
        g_object_class_install_property (gobject_class, prop_id++, pspec);
        break;
      case AV_OPT_TYPE_FLOAT:
        pspec = g_param_spec_float (name, name, help,
            (gfloat) min, (gfloat) max, (gfloat) opt->default_val.dbl,
            G_PARAM_READWRITE);
        g_object_class_install_property (gobject_class, prop_id++, pspec);
        break;
      case AV_OPT_TYPE_STRING:
        pspec = g_param_spec_string (name, name, help,
            opt->default_val.str, G_PARAM_READWRITE);
        g_object_class_install_property (gobject_class, prop_id++, pspec);
        break;
      case AV_OPT_TYPE_UINT64:
        /* ffmpeg expresses all ranges with doubles, this is appalling */
        pspec = g_param_spec_uint64 (name, name, help,
            (guint64) (min <= (gdouble) 0 ? 0 : (guint64) min),
            (guint64) (max >=
                /* Biggest value before UINT64_MAX that can be represented as double */
                (gdouble) 18446744073709550000.0 ?
                /* The Double conversion rounds UINT64_MAX to a bigger */
                /* value, so the following smaller limit must be used. */
                G_GUINT64_CONSTANT (18446744073709550000) : (guint64) max),
            opt->default_val.i64, G_PARAM_READWRITE);
        g_object_class_install_property (gobject_class, prop_id++, pspec);
        break;
      case AV_OPT_TYPE_BOOL:
        /* Some ffmpeg options claims to be booleans but are actually 3-values enums
         * with -1 as default instead of 1 or 0. Handle those using a custom enum
         * so we keep the same defaults as ffmpeg and users can properly configure them.
         */
        if (opt->default_val.i64 == -1) {
          pspec = g_param_spec_enum (name, name, help,
              GST_TYPE_FFMPEG_TRILIAN, opt->default_val.i64, G_PARAM_READWRITE);
        } else {
          pspec = g_param_spec_boolean (name, name, help,
              opt->default_val.i64 ? TRUE : FALSE, G_PARAM_READWRITE);
        }
        g_object_class_install_property (gobject_class, prop_id++, pspec);
        break;
        /* TODO: didn't find options for the video encoders with
         * the following type, add support if needed */
      case AV_OPT_TYPE_CHANNEL_LAYOUT:
      case AV_OPT_TYPE_COLOR:
      case AV_OPT_TYPE_VIDEO_RATE:
      case AV_OPT_TYPE_SAMPLE_FMT:
      case AV_OPT_TYPE_PIXEL_FMT:
      case AV_OPT_TYPE_IMAGE_SIZE:
      case AV_OPT_TYPE_DICT:
      case AV_OPT_TYPE_BINARY:
      case AV_OPT_TYPE_RATIONAL:
      default:
        break;
    }

    g_free (help);

    if (pspec) {
      g_param_spec_set_qdata (pspec, avoption_quark, (gpointer) opt);
    }
  }

  return prop_id;
}

void
gst_ffmpeg_cfg_install_properties (GObjectClass * klass, AVCodec * in_plugin,
    guint base, gint flags)
{
  gint prop_id;
  AVCodecContext *ctx;

  prop_id = base;
  g_return_if_fail (base > 0);

  ctx = avcodec_alloc_context3 (in_plugin);
  if (!ctx)
    g_warning ("could not get context");

  prop_id =
      install_opts ((GObjectClass *) klass, &in_plugin->priv_class, prop_id, 0,
      " (Private codec option)", NULL);
  prop_id =
      install_opts ((GObjectClass *) klass, &ctx->av_class, prop_id, flags,
      " (Generic codec option, might have no effect)", generic_overrides);

  if (ctx) {
    gst_ffmpeg_avcodec_close (ctx);
    av_free (ctx);
  }
}

static gint
set_option_value (AVCodecContext * ctx, GParamSpec * pspec,
    const GValue * value, const AVOption * opt)
{
  int res = -1;

  switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
    case G_TYPE_INT:
      res = av_opt_set_int (ctx, opt->name,
          g_value_get_int (value), AV_OPT_SEARCH_CHILDREN);
      break;
    case G_TYPE_INT64:
      res = av_opt_set_int (ctx, opt->name,
          g_value_get_int64 (value), AV_OPT_SEARCH_CHILDREN);
      break;
    case G_TYPE_UINT64:
      res = av_opt_set_int (ctx, opt->name,
          g_value_get_uint64 (value), AV_OPT_SEARCH_CHILDREN);
      break;
    case G_TYPE_DOUBLE:
      res = av_opt_set_double (ctx, opt->name,
          g_value_get_double (value), AV_OPT_SEARCH_CHILDREN);
      break;
    case G_TYPE_FLOAT:
      res = av_opt_set_double (ctx, opt->name,
          g_value_get_float (value), AV_OPT_SEARCH_CHILDREN);
      break;
    case G_TYPE_STRING:
      res = av_opt_set (ctx, opt->name,
          g_value_get_string (value), AV_OPT_SEARCH_CHILDREN);
      /* Some code in FFmpeg returns ENOMEM if the string is NULL:
       * *dst = av_strdup(val);
       * return *dst ? 0 : AVERROR(ENOMEM);
       * That makes little sense, let's ignore that
       */
      if (!g_value_get_string (value))
        res = 0;
      break;
    case G_TYPE_BOOLEAN:
      res = av_opt_set_int (ctx, opt->name,
          g_value_get_boolean (value), AV_OPT_SEARCH_CHILDREN);
      break;
    default:
      if (G_IS_PARAM_SPEC_ENUM (pspec)) {
        res = av_opt_set_int (ctx, opt->name,
            g_value_get_enum (value), AV_OPT_SEARCH_CHILDREN);
      } else if (G_IS_PARAM_SPEC_FLAGS (pspec)) {
        res = av_opt_set_int (ctx, opt->name,
            g_value_get_flags (value), AV_OPT_SEARCH_CHILDREN);
      } else {                  /* oops, bit lazy we don't cover this case yet */
        g_critical ("%s does not yet support type %s", GST_FUNCTION,
            g_type_name (G_PARAM_SPEC_VALUE_TYPE (pspec)));
      }
  }

  return res;
}

gboolean
gst_ffmpeg_cfg_set_property (AVCodecContext * refcontext, const GValue * value,
    GParamSpec * pspec)
{
  const AVOption *opt;

  opt = g_param_spec_get_qdata (pspec, avoption_quark);

  if (!opt)
    return FALSE;

  return set_option_value (refcontext, pspec, value, opt) >= 0;
}

gboolean
gst_ffmpeg_cfg_get_property (AVCodecContext * refcontext, GValue * value,
    GParamSpec * pspec)
{
  const AVOption *opt;
  int res = -1;

  opt = g_param_spec_get_qdata (pspec, avoption_quark);

  if (!opt)
    return FALSE;

  switch (G_PARAM_SPEC_VALUE_TYPE (pspec)) {
    case G_TYPE_INT:
    {
      int64_t val;
      if ((res = av_opt_get_int (refcontext, opt->name,
                  AV_OPT_SEARCH_CHILDREN, &val) >= 0))
        g_value_set_int (value, val);
      break;
    }
    case G_TYPE_INT64:
    {
      int64_t val;
      if ((res = av_opt_get_int (refcontext, opt->name,
                  AV_OPT_SEARCH_CHILDREN, &val) >= 0))
        g_value_set_int64 (value, val);
      break;
    }
    case G_TYPE_UINT64:
    {
      int64_t val;
      if ((res = av_opt_get_int (refcontext, opt->name,
                  AV_OPT_SEARCH_CHILDREN, &val) >= 0))
        g_value_set_uint64 (value, val);
      break;
    }
    case G_TYPE_DOUBLE:
    {
      gdouble val;
      if ((res = av_opt_get_double (refcontext, opt->name,
                  AV_OPT_SEARCH_CHILDREN, &val) >= 0))
        g_value_set_double (value, val);
      break;
    }
    case G_TYPE_FLOAT:
    {
      gdouble val;
      if ((res = av_opt_get_double (refcontext, opt->name,
                  AV_OPT_SEARCH_CHILDREN, &val) >= 0))
        g_value_set_float (value, (gfloat) val);
      break;
    }
    case G_TYPE_STRING:
    {
      uint8_t *val;
      if ((res = av_opt_get (refcontext, opt->name,
                  AV_OPT_SEARCH_CHILDREN | AV_OPT_ALLOW_NULL, &val) >= 0)) {
        g_value_set_string (value, (gchar *) val);
      }
      break;
    }
    case G_TYPE_BOOLEAN:
    {
      int64_t val;
      if ((res = av_opt_get_int (refcontext, opt->name,
                  AV_OPT_SEARCH_CHILDREN, &val) >= 0))
        g_value_set_boolean (value, val ? TRUE : FALSE);
      break;
    }
    default:
      if (G_IS_PARAM_SPEC_ENUM (pspec)) {
        int64_t val;

        if ((res = av_opt_get_int (refcontext, opt->name,
                    AV_OPT_SEARCH_CHILDREN, &val) >= 0))
          g_value_set_enum (value, val);
      } else if (G_IS_PARAM_SPEC_FLAGS (pspec)) {
        int64_t val;

        if ((res = av_opt_get_int (refcontext, opt->name,
                    AV_OPT_SEARCH_CHILDREN, &val) >= 0))
          g_value_set_flags (value, val);
      } else {                  /* oops, bit lazy we don't cover this case yet */
        g_critical ("%s does not yet support type %s", GST_FUNCTION,
            g_type_name (G_PARAM_SPEC_VALUE_TYPE (pspec)));
      }
  }

  return res >= 0;
}

void
gst_ffmpeg_cfg_fill_context (GObject * object, AVCodecContext * context)
{
  GParamSpec **pspecs;
  guint num_props, i;

  pspecs = g_object_class_list_properties (G_OBJECT_GET_CLASS (object),
      &num_props);

  for (i = 0; i < num_props; ++i) {
    GParamSpec *pspec = pspecs[i];
    const AVOption *opt;
    GValue value = G_VALUE_INIT;

    opt = g_param_spec_get_qdata (pspec, avoption_quark);

    if (!opt)
      continue;

    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    g_object_get_property (object, pspec->name, &value);
    set_option_value (context, pspec, &value, opt);
    g_value_unset (&value);
  }
  g_free (pspecs);
}

void
gst_ffmpeg_cfg_finalize (void)
{
  GST_ERROR ("Finalizing");
  g_assert (generic_overrides);
  g_hash_table_unref (generic_overrides);
}
