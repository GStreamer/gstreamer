
#ifndef __gst_interfaces_marshal_MARSHAL_H__
#define __gst_interfaces_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:OBJECT,ULONG (tuner-marshal.list:1) */
extern void gst_interfaces_marshal_VOID__OBJECT_ULONG (GClosure     *closure,
                                                       GValue       *return_value,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      invocation_hint,
                                                       gpointer      marshal_data);

/* VOID:OBJECT,INT (tuner-marshal.list:2) */
extern void gst_interfaces_marshal_VOID__OBJECT_INT (GClosure     *closure,
                                                     GValue       *return_value,
                                                     guint         n_param_values,
                                                     const GValue *param_values,
                                                     gpointer      invocation_hint,
                                                     gpointer      marshal_data);

G_END_DECLS

#endif /* __gst_interfaces_marshal_MARSHAL_H__ */

