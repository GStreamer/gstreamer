/* 
 * gstplugin.h: sample header file for plug-in
 */

#ifndef __GST_GDK_PIXBUF_H__
#define __GST_GDK_PIXBUF_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/* #define's don't like whitespacey bits */
#define GST_TYPE_GDK_PIXBUF \
  (gst_gdk_pixbuf_get_type())
#define GST_GDK_PIXBUF(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GDK_PIXBUF,GstGdkPixbuf))
#define GST_GDK_PIXBUF_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GDK_PIXBUF,GstGdkPixbuf))
#define GST_IS_GDK_PIXBUF(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GDK_PIXBUF))
#define GST_IS_GDK_PIXBUF_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GDK_PIXBUF))

typedef struct _GstGdkPixbuf      GstGdkPixbuf;
typedef struct _GstGdkPixbufClass GstGdkPixbufClass;

struct _GstGdkPixbuf
{
  GstElement element;

  GstPad *sinkpad, *srcpad;

  int width;
  int height;
  int rowstride;
  unsigned int image_size;
};

struct _GstGdkPixbufClass 
{
  GstElementClass parent_class;
};

GType gst_gdk_pixbuf_get_type (void);

G_END_DECLS

#endif /* __GST_GDK_PIXBUF_H__ */
