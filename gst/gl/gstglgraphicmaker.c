#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglgraphicmaker.h"


#define GST_CAT_DEFAULT gst_gl_graphicmaker_debug
	GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* inpect details */
static const GstElementDetails element_details = {
    "glgraphicmaker",
    "Transform filter",
    "output an opengl scene flux",
    "Jhonny Bravo and Kelly"
  };

/* Source pad definition */
static GstStaticPadTemplate gst_gl_graphicmaker_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS)
    );

/* Source pad definition */
static GstStaticPadTemplate gst_gl_graphicmaker_sink_pad_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";"
        GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

/* Properties */
enum
{
  PROP_0,
  PROP_GLCONTEXT_WIDTH,
  PROP_GLCONTEXT_HEIGHT,
  PROP_CLIENT_RESHAPE_CALLBACK,
  PROP_CLIENT_DRAW_CALLBACK
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_graphicmaker_debug, "glgraphicmaker", 0, "glgraphicmaker element");

GST_BOILERPLATE_FULL (GstGLGraphicmaker, gst_gl_graphicmaker, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static void gst_gl_graphicmaker_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_graphicmaker_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_graphicmaker_reset (GstGLGraphicmaker* graphicmaker);
static gboolean gst_gl_graphicmaker_set_caps (GstBaseTransform * bt,
    GstCaps * incaps, GstCaps * outcaps);
static GstCaps *gst_gl_graphicmaker_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps);
static gboolean gst_gl_graphicmaker_start (GstBaseTransform * bt);
static gboolean gst_gl_graphicmaker_stop (GstBaseTransform * bt);
static GstFlowReturn gst_gl_graphicmaker_prepare_output_buffer (GstBaseTransform *
    trans, GstBuffer * input, gint size, GstCaps * caps, GstBuffer ** buf);
static GstFlowReturn gst_gl_graphicmaker_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_graphicmaker_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, guint * size);


static void
gst_gl_graphicmaker_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_graphicmaker_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_graphicmaker_sink_pad_template));
}

static void
gst_gl_graphicmaker_class_init (GstGLGraphicmakerClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_graphicmaker_set_property;
  gobject_class->get_property = gst_gl_graphicmaker_get_property;

  GST_BASE_TRANSFORM_CLASS (klass)->transform_caps =
      gst_gl_graphicmaker_transform_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->transform = gst_gl_graphicmaker_transform;
  GST_BASE_TRANSFORM_CLASS (klass)->start = gst_gl_graphicmaker_start;
  GST_BASE_TRANSFORM_CLASS (klass)->stop = gst_gl_graphicmaker_stop;
  GST_BASE_TRANSFORM_CLASS (klass)->set_caps = gst_gl_graphicmaker_set_caps;
  GST_BASE_TRANSFORM_CLASS (klass)->get_unit_size = gst_gl_graphicmaker_get_unit_size;
  GST_BASE_TRANSFORM_CLASS (klass)->prepare_output_buffer =
      gst_gl_graphicmaker_prepare_output_buffer;

  
  g_object_class_install_property (gobject_class, PROP_GLCONTEXT_WIDTH,
      g_param_spec_int ("glcontext_width", "OpenGL context width",
          "Change the opengl context width", 0, INT_MAX, 0, 
          G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_GLCONTEXT_HEIGHT,
      g_param_spec_int ("glcontext_height", "OpenGL context height",
          "Change the opengl context height", 0, INT_MAX, 0, 
          G_PARAM_WRITABLE));
  
  g_object_class_install_property (gobject_class, PROP_CLIENT_RESHAPE_CALLBACK,
      g_param_spec_pointer ("client_reshape_callback", "Client reshape callback",
          "Executed in next glut loop iteration when window size is changed", 
          G_PARAM_WRITABLE));
  
  g_object_class_install_property (gobject_class, PROP_CLIENT_DRAW_CALLBACK,
      g_param_spec_pointer ("client_draw_callback", "Client draw callback",
          "Executed in next glut loop iteration when glutPostRedisplay is called", 
          G_PARAM_WRITABLE));
}

static void
gst_gl_graphicmaker_init (GstGLGraphicmaker* graphicmaker, GstGLGraphicmakerClass * klass)
{
    gst_gl_graphicmaker_reset (graphicmaker);
}

static void
gst_gl_graphicmaker_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstGLGraphicmaker* graphicmaker = GST_GL_GRAPHICMAKER (object);

    switch (prop_id) {
      case PROP_GLCONTEXT_WIDTH:
      {
          graphicmaker->glcontext_width = g_value_get_int (value);
          break;
      }
      case PROP_GLCONTEXT_HEIGHT:
      {
          graphicmaker->glcontext_height = g_value_get_int (value);
          break;
      }
      case PROP_CLIENT_RESHAPE_CALLBACK:
      {
          graphicmaker->clientReshapeCallback = g_value_get_pointer (value);
          break;
      }
      case PROP_CLIENT_DRAW_CALLBACK:
      {
          graphicmaker->clientDrawCallback = g_value_get_pointer (value);
          break;
      }
      default:
          G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
          break;
    }
}

static void
gst_gl_graphicmaker_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
  //GstGLGraphicmaker *graphicmaker = GST_GL_GRAPHICMAKER (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_graphicmaker_reset (GstGLGraphicmaker* graphicmaker)
{ 
    if (graphicmaker->display) {
        g_object_unref (graphicmaker->display);
        graphicmaker->display = NULL;
    }
    graphicmaker->peek = FALSE;

    graphicmaker->glcontext_width = 0;
    graphicmaker->glcontext_height = 0;
    graphicmaker->clientReshapeCallback = NULL;
    graphicmaker->clientDrawCallback = NULL;
}

static gboolean
gst_gl_graphicmaker_start (GstBaseTransform * bt)
{
    GstGLGraphicmaker* graphicmaker = GST_GL_GRAPHICMAKER (bt);

    return TRUE;
}

static gboolean
gst_gl_graphicmaker_stop (GstBaseTransform * bt)
{
  GstGLGraphicmaker* graphicmaker = GST_GL_GRAPHICMAKER (bt);

  gst_gl_graphicmaker_reset (graphicmaker);

  return TRUE;
}

static GstCaps *
gst_gl_graphicmaker_transform_caps (GstBaseTransform * bt,
    GstPadDirection direction, GstCaps * caps)
{
	GstGLGraphicmaker* graphicmaker;
	GstStructure* structure;
	GstCaps* newcaps, *newothercaps;
	GstStructure* newstruct;
	const GValue* width_value;
	const GValue* height_value;
	const GValue* framerate_value;
	const GValue* par_value;

	graphicmaker = GST_GL_GRAPHICMAKER (bt);

	GST_ERROR ("transform caps %" GST_PTR_FORMAT, caps);

	structure = gst_caps_get_structure (caps, 0);

	width_value = gst_structure_get_value (structure, "width");
	height_value = gst_structure_get_value (structure, "height");
	framerate_value = gst_structure_get_value (structure, "framerate");
	par_value = gst_structure_get_value (structure, "pixel-aspect-ratio");

	if (direction == GST_PAD_SRC) 
	{
		newothercaps = gst_caps_new_simple ("video/x-raw-rgb", NULL);
		newstruct = gst_caps_get_structure (newothercaps, 0);
		gst_structure_set_value (newstruct, "width", width_value);
		gst_structure_set_value (newstruct, "height", height_value);
		gst_structure_set_value (newstruct, "framerate", framerate_value);
		if (par_value)
			gst_structure_set_value (newstruct, "pixel-aspect-ratio", par_value);
		else
			gst_structure_set (newstruct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
							   1, 1, NULL);
		newcaps = gst_caps_new_simple ("video/x-raw-yuv", NULL);
		gst_caps_append(newcaps, newothercaps);
        newstruct = gst_caps_get_structure (newcaps, 0);
	    gst_structure_set_value (newstruct, "width", width_value);
	    gst_structure_set_value (newstruct, "height", height_value);
	} 
	else 
    {
        newcaps = gst_caps_new_simple ("video/x-raw-gl", NULL);
        newstruct = gst_caps_get_structure (newcaps, 0);
        if (graphicmaker->glcontext_width != 0 && graphicmaker->glcontext_height != 0)
        {
            GValue value_w = { 0 };
            GValue value_h = { 0 };
            g_value_init (&value_w, G_TYPE_INT);
            g_value_init (&value_h, G_TYPE_INT);
            g_value_set_int (&value_w, graphicmaker->glcontext_width);
            g_value_set_int (&value_h, graphicmaker->glcontext_height);
            gst_structure_set_value (newstruct, "width", &value_w);
	        gst_structure_set_value (newstruct, "height", &value_h);
            g_value_unset (&value_w);
            g_value_unset (&value_h);
        }
        else
        {
            gst_structure_set_value (newstruct, "width", width_value);
	        gst_structure_set_value (newstruct, "height", height_value);
        }
    }

	
	gst_structure_set_value (newstruct, "framerate", framerate_value);
	if (par_value)
		gst_structure_set_value (newstruct, "pixel-aspect-ratio", par_value);
	else
		gst_structure_set (newstruct, "pixel-aspect-ratio", GST_TYPE_FRACTION,
		                   1, 1, NULL);

	GST_ERROR ("new caps %" GST_PTR_FORMAT, newcaps);

	return newcaps;
}

static gboolean
gst_gl_graphicmaker_set_caps (GstBaseTransform* bt, GstCaps* incaps,
    GstCaps* outcaps)
{
    GstGLGraphicmaker* graphicmaker;
    gboolean ret;

    graphicmaker = GST_GL_GRAPHICMAKER (bt);

    GST_DEBUG ("called with %" GST_PTR_FORMAT, incaps);

    ret = gst_video_format_parse_caps (incaps, &graphicmaker->video_format,
        &graphicmaker->width, &graphicmaker->height);

    if (!ret) {
      GST_DEBUG ("bad caps");
      return FALSE;
    }

    graphicmaker->display = gst_gl_display_new ();

    //client opengl context size
    if (graphicmaker->glcontext_width != 0 && graphicmaker->glcontext_height != 0)
        gst_gl_display_initGLContext (graphicmaker->display, 0, 0, 
            graphicmaker->glcontext_width, graphicmaker->glcontext_height,
            graphicmaker->width, graphicmaker->height, 0, FALSE);
    //default opengl context size
    else
    {
        //init unvisible opengl context
		static gint glcontext_y = 0;
        gst_gl_display_initGLContext (graphicmaker->display, 
            50, glcontext_y++ * (graphicmaker->height+50) + 50,
            graphicmaker->width, graphicmaker->height,
            graphicmaker->width, graphicmaker->height, 0, FALSE);
    }

    //set the client reshape callback
    gst_gl_display_setClientReshapeCallback (graphicmaker->display, 
        graphicmaker->clientReshapeCallback);
    
    //set the client draw callback
    gst_gl_display_setClientDrawCallback (graphicmaker->display, 
        graphicmaker->clientDrawCallback);

    return ret;
}

static gboolean
gst_gl_graphicmaker_get_unit_size (GstBaseTransform * trans, GstCaps * caps,
                               guint * size)
{
    gboolean ret;
    GstStructure *structure;
    gint width;
    gint height;

    structure = gst_caps_get_structure (caps, 0);
    if (gst_structure_has_name (structure, "video/x-raw-gl")) 
    {
        GstVideoFormat video_format;

        ret = gst_gl_buffer_format_parse_caps (caps, &video_format, &width, &height);
        if (ret) 
            *size = gst_gl_buffer_format_get_size (video_format, width, height);
    } 
    else 
    {
        GstVideoFormat video_format;

        ret = gst_video_format_parse_caps (caps, &video_format, &width, &height);
        if (ret) 
            *size = gst_video_format_get_size (video_format, width, height);
    }

    return TRUE;
}

static GstFlowReturn
gst_gl_graphicmaker_prepare_output_buffer (GstBaseTransform* trans,
    GstBuffer* input, gint size, GstCaps* caps, GstBuffer** buf)
{
    GstGLGraphicmaker* graphicmaker;
    GstGLBuffer* gl_outbuf;

    graphicmaker = GST_GL_GRAPHICMAKER (trans);

    //blocking call

    //client opengl context size
    if (graphicmaker->glcontext_width != 0 && graphicmaker->glcontext_height != 0)
        gl_outbuf = gst_gl_buffer_new_from_video_format (graphicmaker->display,
            graphicmaker->video_format, 
            graphicmaker->glcontext_width, graphicmaker->glcontext_height,
            graphicmaker->width, graphicmaker->height);
    //default opengl context size
    else
        gl_outbuf = gst_gl_buffer_new_from_video_format (graphicmaker->display,
            graphicmaker->video_format, 
            graphicmaker->width, graphicmaker->height,
            graphicmaker->width, graphicmaker->height);
    *buf = GST_BUFFER (gl_outbuf);
    gst_buffer_set_caps (*buf, caps);

    return GST_FLOW_OK;
}


static GstFlowReturn
gst_gl_graphicmaker_transform (GstBaseTransform* trans, GstBuffer* inbuf,
    GstBuffer* outbuf)
{
    GstGLGraphicmaker* graphicmaker;
    GstGLBuffer* gl_outbuf = GST_GL_BUFFER (outbuf);

    graphicmaker = GST_GL_GRAPHICMAKER (trans);

    GST_DEBUG ("making graphic %p size %d",
        GST_BUFFER_DATA (inbuf), GST_BUFFER_SIZE (inbuf));

    //blocking call
    gst_gl_display_textureChanged(graphicmaker->display, graphicmaker->video_format,
        gl_outbuf->texture, gl_outbuf->texture_u, gl_outbuf->texture_v, 
        gl_outbuf->width, gl_outbuf->height, GST_BUFFER_DATA (inbuf));

    return GST_FLOW_OK;
}
