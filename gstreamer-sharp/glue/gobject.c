#include <glib-object.h>
#include <gst/gst.h>

gint
gstsharp_g_closure_sizeof (void)
{
  return sizeof (GClosure);
}

GType
gstsharp_g_type_from_instance (GTypeInstance * instance)
{
  return G_TYPE_FROM_INSTANCE (instance);
}

gboolean
gstsharp_g_param_spec_get_range (const GParamSpec * pspec, GValue * min,
    GValue * max)
{
  switch (pspec->value_type) {
    case G_TYPE_CHAR:{
      GParamSpecChar *pchar = G_PARAM_SPEC_CHAR (pspec);

      g_value_set_char (min, pchar->minimum);
      g_value_set_char (max, pchar->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_UCHAR:{
      GParamSpecUChar *puchar = G_PARAM_SPEC_UCHAR (pspec);

      g_value_set_uchar (min, puchar->minimum);
      g_value_set_uchar (max, puchar->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_INT:{
      GParamSpecInt *pint = G_PARAM_SPEC_INT (pspec);

      g_value_set_int (min, pint->minimum);
      g_value_set_int (max, pint->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_UINT:{
      GParamSpecUInt *puint = G_PARAM_SPEC_UINT (pspec);

      g_value_set_uint (min, puint->minimum);
      g_value_set_uint (max, puint->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_INT64:{
      GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (pspec);

      g_value_set_int64 (min, pint64->minimum);
      g_value_set_int64 (max, pint64->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_UINT64:{
      GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (pspec);

      g_value_set_uint64 (min, puint64->minimum);
      g_value_set_uint64 (max, puint64->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_LONG:{
      GParamSpecLong *plong = G_PARAM_SPEC_LONG (pspec);

      g_value_set_long (min, plong->minimum);
      g_value_set_long (max, plong->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_ULONG:{
      GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (pspec);

      g_value_set_ulong (min, pulong->minimum);
      g_value_set_ulong (max, pulong->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_FLOAT:{
      GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (pspec);

      g_value_set_float (min, pfloat->minimum);
      g_value_set_float (max, pfloat->maximum);

      return TRUE;
    }
      break;
    case G_TYPE_DOUBLE:{
      GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (pspec);

      g_value_set_double (min, pdouble->minimum);
      g_value_set_double (max, pdouble->maximum);

      return TRUE;
    }
      break;
    default:
      if (pspec->value_type == GST_TYPE_FRACTION) {
        GstParamSpecFraction *pfraction = GST_PARAM_SPEC_FRACTION (pspec);

        gst_value_set_fraction (min, pfraction->min_num, pfraction->min_den);
        gst_value_set_fraction (max, pfraction->max_num, pfraction->max_den);

        return TRUE;
      }
      break;
  }

  return FALSE;
}
