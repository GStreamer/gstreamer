/*
 * GStreamer
 * Copyright (C) 2008 Cyril Comparon <cyril.comparon@gmail.com>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglbumper.h"

#define GST_CAT_DEFAULT gst_gl_bumper_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
  GST_ELEMENT_DETAILS ("OpenGL bumper filter",
    "Filter/Effect",
    "Bump mapping filter",
    "Cyril Comparon <cyril.comparon@gmail.com>, Julien Isorce <julien.isorce@gmail.com>");

enum
{
  PROP_0,
  PROP_LOCATION
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_bumper_debug, "glbumper", 0, "glbumper element");

GST_BOILERPLATE_FULL (GstGLBumper, gst_gl_bumper, GstGLFilter,
  GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_bumper_set_property (GObject * object, guint prop_id,
	const GValue * value, GParamSpec * pspec);
static void gst_gl_bumper_get_property (GObject * object, guint prop_id,
  GValue * value, GParamSpec * pspec);

static void gst_gl_bumper_reset (GstGLFilter* filter);
static void gst_gl_bumper_init_shader (GstGLFilter* filter);
static gboolean gst_gl_bumper_filter (GstGLFilter * filter,
  GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_bumper_callback (gint width, gint height, guint texture, gpointer stuff);

//vertex source
static const gchar *bumper_v_src =
  "attribute vec3 aTangent;\n"
  "\n"
  "varying vec3 vNormal;\n"
  "varying vec3 vTangent;\n"
  "varying vec3 vVertexToLight0;\n"
  "varying vec3 vVertexToLight1;\n"
  "\n"
  "void main()\n"
  "{\n"
	"  // transform the vertex\n"
	"  gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;\n"
  "\n"
	"  // transform the normal and the tangent to scene coords\n"
	"  vNormal = normalize(gl_NormalMatrix * gl_Normal);\n"
	"  vTangent = normalize(gl_NormalMatrix * aTangent);\n"
  "\n"
	"  // transforming the vertex position to modelview-space\n"
	"  //const vec4 vertexInSceneCoords = gl_ModelViewMatrix * gl_Vertex;\n"
  "\n"
	"  // calculate the vector from the vertex position to the light position\n"
	"  vVertexToLight0 = normalize(gl_LightSource[0].position).xyz;\n"
	"  vVertexToLight1 = normalize(gl_LightSource[1].position).xyz;\n"
  "\n"
	"  // transit vertex color\n"
	"  gl_FrontColor = gl_BackColor = gl_Color;\n"
  "\n"
	"  // use the two first sets of texture coordinates in the fragment shader\n"
  "  gl_TexCoord[0] = gl_MultiTexCoord0;\n"
  "  gl_TexCoord[1] = gl_MultiTexCoord1;\n"
  "}\n";

//fragment source
static const gchar *bumper_f_src =
  "#extension GL_ARB_texture_rectangle : enable\n"
  "uniform sampler2DRect texture0;\n"
  "uniform sampler2DRect texture1;\n"
  "\n"
  "varying vec3 vNormal;\n"
  "varying vec3 vTangent;\n"
  "varying vec3 vVertexToLight0;\n"
  "varying vec3 vVertexToLight1;\n"
  "\n"
  "void main()\n"
  "{\n"
	"  // get the color of the textures\n"
  "  vec4 textureColor = texture2DRect(texture0, gl_TexCoord[0].st);\n"
  "  vec3 normalmapItem = texture2DRect(texture1, gl_TexCoord[1].st).xyz * 2.0 - 1.0;\n"
  "\n"
	"  // calculate matrix that transform from tangent space to normalmap space (contrary of intuition)\n"
  "  vec3 binormal = cross(vNormal, vTangent);\n"
	"  mat3 tangentSpace2normalmapSpaceMat = mat3(vTangent, binormal, vNormal);\n"
  "\n"
	"  // disturb the normal\n"
	"  vec3 disturbedNormal = tangentSpace2normalmapSpaceMat * normalmapItem;\n"
  "\n"
	"  // calculate the diffuse term and clamping it to [0;1]\n"
	"  float diffuseTerm0 = clamp(dot(disturbedNormal, vVertexToLight0), 0.0, 1.0);\n"
	"  float diffuseTerm1 = clamp(dot(disturbedNormal, vVertexToLight1), 0.0, 1.0);\n"
  "\n"	
	"  vec3 irradiance = (diffuseTerm0 * gl_LightSource[0].diffuse.rgb + diffuseTerm1 * gl_LightSource[1].diffuse.rgb);\n"
  "\n"
	"  // calculate the final color\n"
	"  gl_FragColor = vec4(irradiance * textureColor.rgb, textureColor.w);\n"
  "}\n";

//Called in the gl thread
static void
gst_gl_bumper_init_resources (GstGLFilter *filter)
{
  GstGLBumper *bumper = GST_GL_BUMPER (filter);

  GError *error = NULL;
  //GdkPixbuf *pixbuf = NULL;

  bumper->pixbuf = gdk_pixbuf_new_from_file (bumper->location, &error);
  bumper->bumpmap_width = gdk_pixbuf_get_width (bumper->pixbuf);
  bumper->bumpmap_height = gdk_pixbuf_get_height (bumper->pixbuf);
  
  glGenTextures (1, &bumper->bumpmap);
  glBindTexture(GL_TEXTURE_RECTANGLE_ARB, bumper->bumpmap);
  glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
                bumper->bumpmap_width, bumper->bumpmap_height, 0,
                gdk_pixbuf_get_has_alpha (bumper->pixbuf) ? GL_RGBA : GL_RGB,
                GL_UNSIGNED_BYTE, gdk_pixbuf_get_pixels (bumper->pixbuf));

}

//Called in the gl thread
static void
gst_gl_bumper_reset_resources (GstGLFilter *filter)
{
  GstGLBumper *bumper = GST_GL_BUMPER (filter);
  
  if (bumper->bumpmap)
  {
    glDeleteTextures (1, &bumper->bumpmap);
    bumper->bumpmap = 0;
  }
}

static void
gst_gl_bumper_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_bumper_class_init (GstGLBumperClass* klass)
{
  GObjectClass* gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_bumper_set_property;
  gobject_class->get_property = gst_gl_bumper_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_bumper_filter;
  GST_GL_FILTER_CLASS (klass)->display_init_cb = gst_gl_bumper_init_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb = gst_gl_bumper_reset_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_bumper_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_bumper_reset;

  g_object_class_install_property (gobject_class,
    PROP_LOCATION, g_param_spec_string ("location",
    "Normal map location", 
    "Normal map location",
    NULL, G_PARAM_READWRITE));
}

static void
gst_gl_bumper_init (GstGLBumper* bumper,
    GstGLBumperClass* klass)
{
    bumper->shader = NULL;
    bumper->bumpmap = 0;
    bumper->bumpmap_width = 0;
    bumper->bumpmap_height = 0;
    bumper->pixbuf = NULL;
    bumper->location = NULL;
}

static void
gst_gl_bumper_reset (GstGLFilter* filter)
{
  GstGLBumper* bumper_filter = GST_GL_BUMPER(filter);

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_display_del_shader (filter->display, bumper_filter->shader);
}

static void
gst_gl_bumper_set_property (GObject* object, guint prop_id,
				      const GValue* value, GParamSpec* pspec)
{
  GstGLBumper *bumper = GST_GL_BUMPER (object);

  switch (prop_id)
  {
  case PROP_LOCATION:
    if (bumper->location != NULL) 
      g_free (bumper->location);
    bumper->location = g_value_dup_string (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_bumper_get_property (GObject* object, guint prop_id,
				      GValue* value, GParamSpec* pspec)
{
  GstGLBumper *bumper = GST_GL_BUMPER (object);

  switch (prop_id)
  {
  case PROP_LOCATION:
    g_value_set_string (value, bumper->location);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_bumper_init_shader (GstGLFilter* filter)
{
  GstGLBumper *bumper = GST_GL_BUMPER (filter);

  //blocking call, wait the opengl thread has compiled the shader
  gst_gl_display_gen_shader (filter->display, bumper_v_src, bumper_f_src, &bumper->shader);
}

static gboolean
gst_gl_bumper_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
				GstGLBuffer* outbuf)
{
  gpointer bumper_filter = GST_GL_BUMPER (filter);

  //blocking call, use a FBO
  gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
			  filter->fbo, filter->depthbuffer, outbuf->texture, gst_gl_bumper_callback,
			  inbuf->width, inbuf->height, inbuf->texture,
			  //bumper_filter->fovy, bumper_filter->aspect, bumper_filter->znear, bumper_filter->zfar,
        45, (gdouble)filter->width/(gdouble)filter->height, 0.1, 50,
        GST_GL_DISPLAY_PROJECTION_PERSPECIVE, bumper_filter);

  return TRUE;
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_bumper_callback (gint width, gint height, guint texture, gpointer stuff)
{
  static GLfloat	xrot = 0;
  static GLfloat	yrot = 0;				
  static GLfloat	zrot = 0;
  
  GstGLBumper* bumper = GST_GL_BUMPER (stuff);
  GstGLFilter* filter = GST_GL_FILTER (stuff);
  GLint locTangent = 0;

  //choose the lights
  GLfloat light_direction0[] = { 1.0, 0.0, -1.0, 0.0 }; // light goes along -x
  GLfloat light_direction1[] = { -1.0, 0.0, -1.0, 0.0 }; // light goes along x
  GLfloat light_diffuse0[] = { 1.0, 1.0, 1.0, 1.0 };
  GLfloat light_diffuse1[] = { 1.0, 1.0, 1.0, 1.0 };
  GLfloat mat_diffuse[] = { 1.0, 1.0, 1.0, 1.0 };

  //eye point
  glMatrixMode(GL_PROJECTION);
  gluLookAt(0.0,  0.0, -6.0,
            0.0,  0.0,  0.0,
            0.0,  1.0,  0.0);
  glMatrixMode(GL_MODELVIEW);

  //scene conf
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
  glShadeModel(GL_SMOOTH);

  //set the lights
  glLightfv(GL_LIGHT0, GL_POSITION, light_direction0);
  glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse0);
  glLightfv(GL_LIGHT1, GL_POSITION, light_direction1);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, light_diffuse1);
  glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diffuse);
  glColorMaterial(GL_FRONT_AND_BACK, GL_DIFFUSE);
  glEnable(GL_COLOR_MATERIAL);
  glEnable(GL_LIGHTING);
  glEnable(GL_LIGHT0);
  glEnable(GL_LIGHT1);

  //configure shader
  gst_gl_shader_use (bumper->shader);
  locTangent = gst_gl_shader_get_attribute_location (bumper->shader, "aTangent");

  //set the normal map
  glActiveTextureARB (GL_TEXTURE1_ARB);
  gst_gl_shader_set_uniform_1i (bumper->shader, "texture1", 1);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, bumper->bumpmap);

  //set the video texture
  glActiveTextureARB (GL_TEXTURE0_ARB);
  gst_gl_shader_set_uniform_1i (bumper->shader, "texture0", 0);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);

  //glTranslatef(2.0f, 2.0f, 5.0f);

  glRotatef(xrot,1.0f,0.0f,0.0f);
  glRotatef(yrot,0.0f,1.0f,0.0f);
  glRotatef(zrot,0.0f,0.0f,1.0f);

  //Cube
  glBegin(GL_QUADS);

    // front face
    glNormal3d(0.0, 0.0, -1.0);
    glVertexAttrib3dARB(locTangent, 0.0, 1.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, 0.0);
    glVertex3d( 1.0,  1.0, -1.0); // B
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, bumper->bumpmap_height);
    glVertex3d( 1.0, -1.0, -1.0); // A
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, bumper->bumpmap_height);
    glVertex3d(-1.0, -1.0, -1.0); // D
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, 0.0);
    glVertex3d(-1.0,  1.0, -1.0); // C

    // right face
    glNormal3d(-1.0, 0.0, 0.0);
    glVertexAttrib3dARB(locTangent, 0.0, 1.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, 0.0);
    glVertex3d(-1.0,  1.0, -1.0); // C
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, bumper->bumpmap_height);
    glVertex3d(-1.0, -1.0, -1.0); // D
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, bumper->bumpmap_height);
    glVertex3d(-1.0, -1.0,  1.0); // H
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, 0.0);
    glVertex3d(-1.0,  1.0,  1.0); // G

    // back face
    glNormal3d(0.0, 0.0, 1.0);
    glVertexAttrib3dARB(locTangent, 0.0, 1.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, 0.0);
    glVertex3d(-1.0,  1.0,  1.0); // G
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, bumper->bumpmap_height);
    glVertex3d(-1.0, -1.0,  1.0); // H
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, bumper->bumpmap_height);
    glVertex3d( 1.0, -1.0,  1.0); // E
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, 0.0);
    glVertex3d( 1.0,  1.0,  1.0); // F

    // left face
    glNormal3d(1.0, 0.0, 0.0);
    glVertexAttrib3dARB(locTangent, 0.0, 1.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, 0.0);
    glVertex3d( 1.0,  1.0,  1.0); // F
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, bumper->bumpmap_height);
    glVertex3d( 1.0, -1.0,  1.0); // E
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, bumper->bumpmap_height);
    glVertex3d( 1.0, -1.0, -1.0); // A
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, 0.0);
    glVertex3d( 1.0,  1.0, -1.0); // B

    // top face
    glNormal3d(0.0, 1.0, 0.0);
    glVertexAttrib3dARB(locTangent, 0.0, 0.0, 1.0);
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, 0.0);
    glVertex3d( 1.0,  1.0,  1.0); // F
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, bumper->bumpmap_height);
    glVertex3d( 1.0,  1.0, -1.0); // B
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, bumper->bumpmap_height);
    glVertex3d(-1.0,  1.0, -1.0); // C
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, 0.0);
    glVertex3d(-1.0,  1.0,  1.0); // G

    // bottom face
    glNormal3d(0.0, -1.0, 0.0);
    glVertexAttrib3dARB(locTangent, 0.0, 0.0, -1.0);
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, 0.0);
    glVertex3d( 1.0, -1.0, -1.0); // A
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, 0.0, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, 0.0, bumper->bumpmap_height);
    glVertex3d( 1.0, -1.0,  1.0); // E
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, height);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, bumper->bumpmap_height);
    glVertex3d(-1.0, -1.0,  1.0); // H
    glMultiTexCoord2dARB(GL_TEXTURE0_ARB, width, 0.0);
    glMultiTexCoord2dARB(GL_TEXTURE1_ARB, bumper->bumpmap_width, 0.0);
    glVertex3d(-1.0, -1.0, -1.0); // D
  glEnd();

  glUseProgram(0);
  glDisable(GL_LIGHT0);
  glDisable(GL_LIGHT1);
  glDisable(GL_LIGHTING);
  glDisable(GL_COLOR_MATERIAL);

  xrot+=1.0f;
  yrot+=0.9f;
  zrot+=1.1f;
}
