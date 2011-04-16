#include "gstrtpbin-marshal.h"

#include	<glib-object.h>


#ifdef G_ENABLE_DEBUG
#define g_marshal_value_peek_boolean(v)  g_value_get_boolean (v)
#define g_marshal_value_peek_char(v)     g_value_get_char (v)
#define g_marshal_value_peek_uchar(v)    g_value_get_uchar (v)
#define g_marshal_value_peek_int(v)      g_value_get_int (v)
#define g_marshal_value_peek_uint(v)     g_value_get_uint (v)
#define g_marshal_value_peek_long(v)     g_value_get_long (v)
#define g_marshal_value_peek_ulong(v)    g_value_get_ulong (v)
#define g_marshal_value_peek_int64(v)    g_value_get_int64 (v)
#define g_marshal_value_peek_uint64(v)   g_value_get_uint64 (v)
#define g_marshal_value_peek_enum(v)     g_value_get_enum (v)
#define g_marshal_value_peek_flags(v)    g_value_get_flags (v)
#define g_marshal_value_peek_float(v)    g_value_get_float (v)
#define g_marshal_value_peek_double(v)   g_value_get_double (v)
#define g_marshal_value_peek_string(v)   (char*) g_value_get_string (v)
#define g_marshal_value_peek_param(v)    g_value_get_param (v)
#define g_marshal_value_peek_boxed(v)    g_value_get_boxed (v)
#define g_marshal_value_peek_pointer(v)  g_value_get_pointer (v)
#define g_marshal_value_peek_object(v)   g_value_get_object (v)
#define g_marshal_value_peek_variant(v)  g_value_get_variant (v)
#else /* !G_ENABLE_DEBUG */
/* WARNING: This code accesses GValues directly, which is UNSUPPORTED API.
 *          Do not access GValues directly in your code. Instead, use the
 *          g_value_get_*() functions
 */
#define g_marshal_value_peek_boolean(v)  (v)->data[0].v_int
#define g_marshal_value_peek_char(v)     (v)->data[0].v_int
#define g_marshal_value_peek_uchar(v)    (v)->data[0].v_uint
#define g_marshal_value_peek_int(v)      (v)->data[0].v_int
#define g_marshal_value_peek_uint(v)     (v)->data[0].v_uint
#define g_marshal_value_peek_long(v)     (v)->data[0].v_long
#define g_marshal_value_peek_ulong(v)    (v)->data[0].v_ulong
#define g_marshal_value_peek_int64(v)    (v)->data[0].v_int64
#define g_marshal_value_peek_uint64(v)   (v)->data[0].v_uint64
#define g_marshal_value_peek_enum(v)     (v)->data[0].v_long
#define g_marshal_value_peek_flags(v)    (v)->data[0].v_ulong
#define g_marshal_value_peek_float(v)    (v)->data[0].v_float
#define g_marshal_value_peek_double(v)   (v)->data[0].v_double
#define g_marshal_value_peek_string(v)   (v)->data[0].v_pointer
#define g_marshal_value_peek_param(v)    (v)->data[0].v_pointer
#define g_marshal_value_peek_boxed(v)    (v)->data[0].v_pointer
#define g_marshal_value_peek_pointer(v)  (v)->data[0].v_pointer
#define g_marshal_value_peek_object(v)   (v)->data[0].v_pointer
#define g_marshal_value_peek_variant(v)  (v)->data[0].v_pointer
#endif /* !G_ENABLE_DEBUG */


/* UINT:UINT (gstrtpbin-marshal.list:1) */
void
gst_rtp_bin_marshal_UINT__UINT (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef guint (*GMarshalFunc_UINT__UINT) (gpointer data1,
      guint arg_1, gpointer data2);
  register GMarshalFunc_UINT__UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  guint v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 2);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_UINT__UINT) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1,
      g_marshal_value_peek_uint (param_values + 1), data2);

  g_value_set_uint (return_value, v_return);
}

/* BOXED:UINT (gstrtpbin-marshal.list:2) */
void
gst_rtp_bin_marshal_BOXED__UINT (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef gpointer (*GMarshalFunc_BOXED__UINT) (gpointer data1,
      guint arg_1, gpointer data2);
  register GMarshalFunc_BOXED__UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  gpointer v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 2);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_BOXED__UINT) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1,
      g_marshal_value_peek_uint (param_values + 1), data2);

  g_value_take_boxed (return_value, v_return);
}

/* BOXED:UINT,UINT (gstrtpbin-marshal.list:3) */
void
gst_rtp_bin_marshal_BOXED__UINT_UINT (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef gpointer (*GMarshalFunc_BOXED__UINT_UINT) (gpointer data1,
      guint arg_1, guint arg_2, gpointer data2);
  register GMarshalFunc_BOXED__UINT_UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  gpointer v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_BOXED__UINT_UINT) (marshal_data ? marshal_data :
      cc->callback);

  v_return = callback (data1,
      g_marshal_value_peek_uint (param_values + 1),
      g_marshal_value_peek_uint (param_values + 2), data2);

  g_value_take_boxed (return_value, v_return);
}

/* OBJECT:UINT (gstrtpbin-marshal.list:4) */
void
gst_rtp_bin_marshal_OBJECT__UINT (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef GObject *(*GMarshalFunc_OBJECT__UINT) (gpointer data1,
      guint arg_1, gpointer data2);
  register GMarshalFunc_OBJECT__UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  GObject *v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 2);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_OBJECT__UINT) (marshal_data ? marshal_data : cc->callback);

  v_return = callback (data1,
      g_marshal_value_peek_uint (param_values + 1), data2);

  g_value_take_object (return_value, v_return);
}

/* VOID:UINT,OBJECT (gstrtpbin-marshal.list:5) */
void
gst_rtp_bin_marshal_VOID__UINT_OBJECT (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_OBJECT) (gpointer data1,
      guint arg_1, gpointer arg_2, gpointer data2);
  register GMarshalFunc_VOID__UINT_OBJECT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_VOID__UINT_OBJECT) (marshal_data ? marshal_data :
      cc->callback);

  callback (data1,
      g_marshal_value_peek_uint (param_values + 1),
      g_marshal_value_peek_object (param_values + 2), data2);
}

/* VOID:UINT (gstrtpbin-marshal.list:6) */

/* VOID:UINT,UINT (gstrtpbin-marshal.list:7) */
void
gst_rtp_bin_marshal_VOID__UINT_UINT (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT_UINT) (gpointer data1,
      guint arg_1, guint arg_2, gpointer data2);
  register GMarshalFunc_VOID__UINT_UINT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_VOID__UINT_UINT) (marshal_data ? marshal_data :
      cc->callback);

  callback (data1,
      g_marshal_value_peek_uint (param_values + 1),
      g_marshal_value_peek_uint (param_values + 2), data2);
}

/* VOID:OBJECT,OBJECT (gstrtpbin-marshal.list:8) */
void
gst_rtp_bin_marshal_VOID__OBJECT_OBJECT (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__OBJECT_OBJECT) (gpointer data1,
      gpointer arg_1, gpointer arg_2, gpointer data2);
  register GMarshalFunc_VOID__OBJECT_OBJECT callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_VOID__OBJECT_OBJECT) (marshal_data ? marshal_data :
      cc->callback);

  callback (data1,
      g_marshal_value_peek_object (param_values + 1),
      g_marshal_value_peek_object (param_values + 2), data2);
}

/* UINT64:BOOL,UINT64 (gstrtpbin-marshal.list:9) */
void
gst_rtp_bin_marshal_UINT64__BOOLEAN_UINT64 (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef guint64 (*GMarshalFunc_UINT64__BOOLEAN_UINT64) (gpointer data1,
      gboolean arg_1, guint64 arg_2, gpointer data2);
  register GMarshalFunc_UINT64__BOOLEAN_UINT64 callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;
  guint64 v_return;

  g_return_if_fail (return_value != NULL);
  g_return_if_fail (n_param_values == 3);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_UINT64__BOOLEAN_UINT64) (marshal_data ? marshal_data :
      cc->callback);

  v_return = callback (data1,
      g_marshal_value_peek_boolean (param_values + 1),
      g_marshal_value_peek_uint64 (param_values + 2), data2);

  g_value_set_uint64 (return_value, v_return);
}

/* VOID:UINT64 (gstrtpbin-marshal.list:10) */
void
gst_rtp_bin_marshal_VOID__UINT64 (GClosure * closure,
    GValue * return_value G_GNUC_UNUSED,
    guint n_param_values,
    const GValue * param_values,
    gpointer invocation_hint G_GNUC_UNUSED, gpointer marshal_data)
{
  typedef void (*GMarshalFunc_VOID__UINT64) (gpointer data1,
      guint64 arg_1, gpointer data2);
  register GMarshalFunc_VOID__UINT64 callback;
  register GCClosure *cc = (GCClosure *) closure;
  register gpointer data1, data2;

  g_return_if_fail (n_param_values == 2);

  if (G_CCLOSURE_SWAP_DATA (closure)) {
    data1 = closure->data;
    data2 = g_value_peek_pointer (param_values + 0);
  } else {
    data1 = g_value_peek_pointer (param_values + 0);
    data2 = closure->data;
  }
  callback =
      (GMarshalFunc_VOID__UINT64) (marshal_data ? marshal_data : cc->callback);

  callback (data1, g_marshal_value_peek_uint64 (param_values + 1), data2);
}
