GstElement* 
gst_elementfactory_make (const gchar *factoryname, const gchar *name);

GstPad*                 
gst_element_get_pad (GstElement *element, const gchar *name);

gint
gst_element_set_state (GstElement *element, GstElementState state);

typedef enum {
  GST_STATE_VOID_PENDING        = 0,
  GST_STATE_NULL                = (1 << 0),
  GST_STATE_READY               = (1 << 1),
  GST_STATE_PAUSED              = (1 << 2),
  GST_STATE_PLAYING             = (1 << 3),
} GstElementState;

