
#ifndef __gst_play_marshal_MARSHAL_H__
#define __gst_play_marshal_MARSHAL_H__

#include	<glib-object.h>

G_BEGIN_DECLS

/* BOOLEAN:OBJECT,BOXED (gstplay-marshal.list:1) */
extern void gst_play_marshal_BOOLEAN__OBJECT_BOXED (GClosure     *closure,
                                                    GValue       *return_value,
                                                    guint         n_param_values,
                                                    const GValue *param_values,
                                                    gpointer      invocation_hint,
                                                    gpointer      marshal_data);

/* BOOLEAN:OBJECT,OBJECT,OBJECT (gstplay-marshal.list:2) */
extern void gst_play_marshal_BOOLEAN__OBJECT_OBJECT_OBJECT (GClosure     *closure,
                                                            GValue       *return_value,
                                                            guint         n_param_values,
                                                            const GValue *param_values,
                                                            gpointer      invocation_hint,
                                                            gpointer      marshal_data);

/* BOXED:OBJECT,BOXED (gstplay-marshal.list:3) */
extern void gst_play_marshal_BOXED__OBJECT_BOXED (GClosure     *closure,
                                                  GValue       *return_value,
                                                  guint         n_param_values,
                                                  const GValue *param_values,
                                                  gpointer      invocation_hint,
                                                  gpointer      marshal_data);

/* VOID:OBJECT,BOOLEAN (gstplay-marshal.list:4) */
extern void gst_play_marshal_VOID__OBJECT_BOOLEAN (GClosure     *closure,
                                                   GValue       *return_value,
                                                   guint         n_param_values,
                                                   const GValue *param_values,
                                                   gpointer      invocation_hint,
                                                   gpointer      marshal_data);

/* ENUM:OBJECT,OBJECT,BOXED (gstplay-marshal.list:5) */
extern void gst_play_marshal_ENUM__OBJECT_OBJECT_BOXED (GClosure     *closure,
                                                        GValue       *return_value,
                                                        guint         n_param_values,
                                                        const GValue *param_values,
                                                        gpointer      invocation_hint,
                                                        gpointer      marshal_data);

/* ENUM:OBJECT,BOXED,OBJECT (gstplay-marshal.list:6) */
extern void gst_play_marshal_ENUM__OBJECT_BOXED_OBJECT (GClosure     *closure,
                                                        GValue       *return_value,
                                                        guint         n_param_values,
                                                        const GValue *param_values,
                                                        gpointer      invocation_hint,
                                                        gpointer      marshal_data);

/* BOXED:OBJECT,BOXED,BOXED (gstplay-marshal.list:7) */
extern void gst_play_marshal_BOXED__OBJECT_BOXED_BOXED (GClosure     *closure,
                                                        GValue       *return_value,
                                                        guint         n_param_values,
                                                        const GValue *param_values,
                                                        gpointer      invocation_hint,
                                                        gpointer      marshal_data);

/* BOXED:INT (gstplay-marshal.list:8) */
extern void gst_play_marshal_BOXED__INT (GClosure     *closure,
                                         GValue       *return_value,
                                         guint         n_param_values,
                                         const GValue *param_values,
                                         gpointer      invocation_hint,
                                         gpointer      marshal_data);

/* OBJECT:BOXED (gstplay-marshal.list:9) */
extern void gst_play_marshal_OBJECT__BOXED (GClosure     *closure,
                                            GValue       *return_value,
                                            guint         n_param_values,
                                            const GValue *param_values,
                                            gpointer      invocation_hint,
                                            gpointer      marshal_data);

/* OBJECT:INT (gstplay-marshal.list:10) */
extern void gst_play_marshal_OBJECT__INT (GClosure     *closure,
                                          GValue       *return_value,
                                          guint         n_param_values,
                                          const GValue *param_values,
                                          gpointer      invocation_hint,
                                          gpointer      marshal_data);

/* INT64:VOID (gstplay-marshal.list:11) */
extern void gst_play_marshal_INT64__VOID (GClosure     *closure,
                                          GValue       *return_value,
                                          guint         n_param_values,
                                          const GValue *param_values,
                                          gpointer      invocation_hint,
                                          gpointer      marshal_data);

/* VOID:OBJECT,INT64,INT64 (gstplay-marshal.list:12) */
extern void gst_play_marshal_VOID__OBJECT_INT64_INT64 (GClosure     *closure,
                                                       GValue       *return_value,
                                                       guint         n_param_values,
                                                       const GValue *param_values,
                                                       gpointer      invocation_hint,
                                                       gpointer      marshal_data);

G_END_DECLS

#endif /* __gst_play_marshal_MARSHAL_H__ */

