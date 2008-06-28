/* 
 * GStreamer
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

#ifndef __GST_GL_H__
#define __GST_GL_H__

#include <GL/glew.h>
#include <gstfreeglut.h>

#include <gst/gst.h>
#include <gst/video/video.h>

#define GST_TYPE_GL_DISPLAY \
      (gst_gl_display_get_type())
#define GST_GL_DISPLAY(obj) \
      (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_DISPLAY,GstGLDisplay))
#define GST_GL_DISPLAY_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_DISPLAY,GstGLDisplayClass))
#define GST_IS_GL_DISPLAY(obj) \
      (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_DISPLAY))
#define GST_IS_GL_DISPLAY_CLASS(klass) \
      (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_DISPLAY))

typedef struct _GstGLDisplay GstGLDisplay;
typedef struct _GstGLDisplayClass GstGLDisplayClass;

//Color space conversion metthod
typedef enum {
    GST_GL_DISPLAY_CONVERSION_GLSL,   //ARB_fragment_shade
    GST_GL_DISPLAY_CONVERSION_MATRIX, //ARB_imaging
	GST_GL_DISPLAY_CONVERSION_MESA,   //MESA_ycbcr_texture
} GstGLDisplayConversion;


//Message type
typedef enum {
    GST_GL_DISPLAY_ACTION_CREATE,
    GST_GL_DISPLAY_ACTION_DESTROY,
	GST_GL_DISPLAY_ACTION_VISIBLE,
    GST_GL_DISPLAY_ACTION_RESHAPE,
    GST_GL_DISPLAY_ACTION_INIT_UPLOAD,
    GST_GL_DISPLAY_ACTION_PREPARE,
    GST_GL_DISPLAY_ACTION_CHANGE,
    GST_GL_DISPLAY_ACTION_CLEAR,
    GST_GL_DISPLAY_ACTION_VIDEO,
    GST_GL_DISPLAY_ACTION_REDISPLAY,
    GST_GL_DISPLAY_ACTION_GENFBO,
    GST_GL_DISPLAY_ACTION_DELFBO,
    GST_GL_DISPLAY_ACTION_USEFBO,
    GST_GL_DISPLAY_ACTION_USEFBO2,
    GST_GL_DISPLAY_ACTION_INIT_DOWNLOAD,
    GST_GL_DISPLAY_ACTION_GENSHADER,
    GST_GL_DISPLAY_ACTION_DELSHADER
	
} GstGLDisplayAction;


//Message to communicate with the gl thread
typedef struct _GstGLDisplayMsg {
    GstGLDisplayAction action;
    gint glutWinId;
    GstGLDisplay* display; 
} GstGLDisplayMsg;


//Texture pool elements
typedef struct _GstGLDisplayTex {
    GLuint texture;
} GstGLDisplayTex;


//Client callbacks
typedef void (* CRCB) ( GLuint, GLuint );
typedef gboolean (* CDCB) ( GLuint, GLuint, GLuint);

//opengl scene callback
typedef void (* GLCB) ( GLuint, GLuint, GLuint, GLhandleARB);
typedef void (* GLCB2) ( gpointer* p1, gpointer* p2, gint w, gint h);

struct _GstGLDisplay {
    GObject object;

    GMutex* mutex;

	GQueue* texturePool;
    
    GCond* cond_init_upload;
    GCond* cond_make;
    GCond* cond_fill;
    GCond* cond_clear;
    GCond* cond_video;
    GCond* cond_generateFBO;
    GCond* cond_useFBO;
    GCond* cond_useFBO2;
    GCond* cond_destroyFBO;
    GCond* cond_init_download;
    GCond* cond_initShader;
    GCond* cond_destroyShader;

    GCond* cond_create;
    GCond* cond_destroy;
    gint glutWinId;
    gulong winId;
    GString* title;
    gint win_xpos;
    gint win_ypos;
    gboolean visible;
    gboolean isAlive;

    //intput frame buffer object (video -> GL)
    GLuint fbo;
    GLuint depthBuffer;
    GLuint textureFBO;
    GLuint textureFBOWidth;
    GLuint textureFBOHeight;
    GstVideoFormat upload_video_format;

    //filter frame buffer object (GL -> GL)
    GLuint requestedFBO;
    GLuint requestedDepthBuffer;
    GLuint requestedTextureFBOWidth;
    GLuint requestedTextureFBOHeight;

    GLuint usedFBO;
    GLuint usedDepthBuffer;
    GLuint usedTextureFBO;
    GLuint usedTextureFBOWidth;
    GLuint usedTextureFBOHeight;
    GLCB glsceneFBO_cb;
    GLCB2 glsceneFBO_cb2;
    gpointer* p1;
    gpointer* p2;
    GLuint inputTextureWidth;
    GLuint inputTextureHeight;
    GLuint inputTexture;
    GLuint rejectedFBO;
    GLuint rejectedDepthBuffer;

    //displayed texture
    GLuint displayedTexture;
    GLuint displayedTextureWidth;
    GLuint displayedTextureHeight;

    gint resize_width;
    gint resize_height;

    GLuint preparedTexture;
    
    GLuint currentTexture;
    GLuint currentTexture_u;
    GLuint currentTexture_v;
    GLuint currentTextureWidth;
    GLuint currentTextureHeight;
    GstVideoFormat currentVideo_format;
    gpointer currentData;

    GLuint textureTrash;

    //output frame buffer object (GL -> video)
    GLuint videoFBO;
    GLuint videoDepthBuffer;
    GLuint videoTexture;
    GLuint videoTexture_u;
    GLuint videoTexture_v;
    gint outputWidth;
    gint outputHeight;
    GstVideoFormat outputVideo_format;
    gpointer outputData;
    GLenum multipleRT[3];

    //recorded texture
    GLuint recordedTexture;
    GLuint recordedTextureWidth;
    GLuint recordedTextureHeight;

    //colorspace conversion method
    GstGLDisplayConversion colorspace_conversion;

    //from video to texture

	gchar* textFProgram_YUY2_UYVY;
	GLhandleARB GLSLProgram_YUY2;
	GLhandleARB GLSLProgram_UYVY;
	
	gchar* textFProgram_I420_YV12;
	GLhandleARB GLSLProgram_I420_YV12;

	gchar* textFProgram_AYUV;
	GLhandleARB GLSLProgram_AYUV;

    //from texture to video
    
    gchar* textFProgram_to_YUY2_UYVY;
	GLhandleARB GLSLProgram_to_YUY2;
	GLhandleARB GLSLProgram_to_UYVY;

    gchar* textFProgram_to_I420_YV12;
	GLhandleARB GLSLProgram_to_I420_YV12;

    gchar* textFProgram_to_AYUV;
	GLhandleARB GLSLProgram_to_AYUV;

    //requested shader
    gchar* requestedTextShader;
    GLhandleARB requestedHandleShader;
    GLhandleARB usedHandleShader;
    GLhandleARB rejectedHandleShader;
	
    //client callbacks
    CRCB clientReshapeCallback;
    CDCB clientDrawCallback;
};


struct _GstGLDisplayClass {
    GObjectClass object_class;
};

GType gst_gl_display_get_type (void);


//------------------------------------------------------------
//-------------------- Public declarations ------------------
//------------------------------------------------------------ 
GstGLDisplay *gst_gl_display_new (void);
void gst_gl_display_init_gl_context (GstGLDisplay* display, 
                                     GLint x, GLint y, 
                                     GLint width, GLint height,
                                     gulong winId,
                                     gboolean visible);
void gst_gl_display_setClientReshapeCallback (GstGLDisplay* display, CRCB cb);
void gst_gl_display_setClientDrawCallback (GstGLDisplay* display, CDCB cb);
void gst_gl_display_setVisibleWindow (GstGLDisplay* display, gboolean visible);
void gst_gl_display_resizeWindow (GstGLDisplay* display, gint width, gint height);
void gst_gl_display_init_upload (GstGLDisplay* display, GstVideoFormat video_format,
                                 guint gl_width, guint gl_height);
void gst_gl_display_prepare_texture (GstGLDisplay* display, guint* pTexture);
void gst_gl_display_do_upload (GstGLDisplay* display, GstVideoFormat video_format,
                               gint video_width, gint video_height, gpointer data,
                               guint gl_width, guint gl_height, guint pTexture);
void gst_gl_display_clearTexture (GstGLDisplay* display, guint texture);

void gst_gl_display_videoChanged (GstGLDisplay* display, GstVideoFormat video_format, 
                                  gint width, gint height, GLuint recordedTexture, gpointer data);
gboolean gst_gl_display_postRedisplay (GstGLDisplay* display, GLuint texture, gint width, gint height);
void gst_gl_display_requestFBO (GstGLDisplay* display, gint width, gint height, 
                                guint* fbo, guint* depthbuffer);
void gst_gl_display_useFBO (GstGLDisplay* display, gint textureFBOWidth, gint textureFBOheight, 
                            guint fbo, guint depthbuffer, guint textureFBO, GLCB cb,
                            guint inputTextureWidth, guint inputTextureHeight, guint inputTexture,
                            GLhandleARB handleShader);
void gst_gl_display_useFBO2 (GstGLDisplay* display, gint textureFBOWidth, gint textureFBOheight, 
                            guint fbo, guint depthbuffer, guint textureFBO, GLCB2 cb,
                            gpointer* p1, gpointer* p2);
void gst_gl_display_rejectFBO (GstGLDisplay* display, guint fbo, 
                               guint depthbuffer);
void gst_gl_display_init_download (GstGLDisplay* display, GstVideoFormat video_format, 
                                   gint width, gint height);
void gst_gl_display_initShader (GstGLDisplay* display, gchar* textShader, GLhandleARB* handleShader);
void gst_gl_display_destroyShader (GstGLDisplay* display, GLhandleARB shader);
void gst_gl_display_set_windowId (GstGLDisplay* display, gulong winId);

#endif
