#ifndef _GST_GLVIDEOMAKER_H_
#define _GST_GLVIDEOMAKER_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstglbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_VIDEOMAKER            (gst_gl_videomaker_get_type())
#define GST_GL_VIDEOMAKER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_VIDEOMAKER,GstGLVideomaker))
#define GST_IS_GL_VIDEOMAKER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_VIDEOMAKER))
#define GST_GL_VIDEOMAKER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_VIDEOMAKER,GstGLVideomakerClass))
#define GST_IS_GL_VIDEOMAKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_VIDEOMAKER))
#define GST_GL_VIDEOMAKER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_VIDEOMAKER,GstGLVideomakerClass))

typedef struct _GstGLVideomaker GstGLVideomaker;
typedef struct _GstGLVideomakerClass GstGLVideomakerClass;

//typedef void (*GstGLVideomakerProcessFunc) (GstGLVideomaker *, guint8 *, guint);

struct _GstGLVideomaker
{
  GstBaseTransform base_transform;

  GstGLDisplay *display;
  GstVideoFormat video_format;
  gint width;
  gint height;
};

struct _GstGLVideomakerClass
{
  GstBaseTransformClass base_transform_class;
};

GType gst_gl_videomaker_get_type (void);

G_END_DECLS

#endif /* _GST_GLVIDEOMAKER_H_ */
