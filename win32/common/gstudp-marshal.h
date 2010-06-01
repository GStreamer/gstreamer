
#ifndef __gst_udp_marshal_MARSHAL_H__
#define __gst_udp_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* VOID:STRING,INT (gstudp-marshal.list:1) */
extern void gst_udp_marshal_VOID__STRING_INT (GClosure     *closure,
                                              GValue       *return_value,
                                              guint         n_param_values,
                                              const GValue *param_values,
                                              gpointer      invocation_hint,
                                              gpointer      marshal_data);

/* BOXED:STRING,INT (gstudp-marshal.list:2) */
extern void gst_udp_marshal_BOXED__STRING_INT (GClosure     *closure,
                                               GValue       *return_value,
                                               guint         n_param_values,
                                               const GValue *param_values,
                                               gpointer      invocation_hint,
                                               gpointer      marshal_data);

G_END_DECLS

#endif /* __gst_udp_marshal_MARSHAL_H__ */

