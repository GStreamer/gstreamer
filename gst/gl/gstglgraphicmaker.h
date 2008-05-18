#ifndef _GST_GLGRAPHICMAKER_H_
#define _GST_GLGRAPHICMAKER_H_

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>

#include "gstglbuffer.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_GRAPHICMAKER            (gst_gl_graphicmaker_get_type())
#define GST_GL_GRAPHICMAKER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_GRAPHICMAKER,GstGLGraphicmaker))
#define GST_IS_GL_GRAPHICMAKER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_GRAPHICMAKER))
#define GST_GL_GRAPHICMAKER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_GRAPHICMAKER,GstGLGraphicmakerClass))
#define GST_IS_GL_GRAPHICMAKER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_GRAPHICMAKER))
#define GST_GL_GRAPHICMAKER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_GRAPHICMAKER,GstGLGraphicmakerClass))

typedef struct _GstGLGraphicmaker GstGLGraphicmaker;
typedef struct _GstGLGraphicmakerClass GstGLGraphicmakerClass;


struct _GstGLGraphicmaker
{
  GstBaseTransform base_transform;

  GstPad *srcpad;
  GstPad *sinkpad;

  GstGLDisplay *display;
  GstVideoFormat video_format;
  gint width;
  gint height;  

  gboolean peek;

  gint glcontext_width;
  gint glcontext_height;
  CRCB clientReshapeCallback;
  CDCB clientDrawCallback;
};

struct _GstGLGraphicmakerClass
{
  GstBaseTransformClass base_transform_class;
};

GType gst_gl_graphicmaker_get_type (void);

G_END_DECLS

#endif /* _GST_GLGRAPHICMAKER_H_ */
