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

//Message type
typedef enum {
    GST_GL_DISPLAY_ACTION_CREATE,
    GST_GL_DISPLAY_ACTION_DESTROY,
	GST_GL_DISPLAY_ACTION_VISIBLE,
    GST_GL_DISPLAY_ACTION_PREPARE,
    GST_GL_DISPLAY_ACTION_CHANGE,
    GST_GL_DISPLAY_ACTION_CLEAR,
    GST_GL_DISPLAY_ACTION_VIDEO,
    GST_GL_DISPLAY_ACTION_REDISPLAY,
    GST_GL_DISPLAY_ACTION_GENFBO,
    GST_GL_DISPLAY_ACTION_DELFBO,
    GST_GL_DISPLAY_ACTION_USEFBO
	
} GstGLDisplayAction;


//Message to communicate with the glut thread
typedef struct _GstGLDisplayMsg {
    GstGLDisplayAction action;
    gint glutWinId;
    GstGLDisplay* display; 
} GstGLDisplayMsg;


//Texture pool elements
typedef struct _GstGLDisplayTex {
    GLuint texture;
    GLuint texture_u;
    GLuint texture_v;
} GstGLDisplayTex;


//Client callbacks
typedef void (* CRCB) ( GLuint, GLuint );
typedef gboolean (* CDCB) ( GLuint, GLuint, GLuint);

//opengl scene callback
typedef void (* GLCB) ( GLuint, GLuint, GLuint);

struct _GstGLDisplay {
    GObject object;

    GMutex* mutex;

	GQueue* texturePool;
    
    GCond* cond_make;
    GCond* cond_fill;
    GCond* cond_clear;
    GCond* cond_video;
    GCond* cond_generateFBO;
    GCond* cond_useFBO;
    GCond* cond_destroyFBO;

    GCond* cond_create;
    GCond* cond_destroy;
    gint glutWinId;
    gulong winId;
    GString* title;
    gint win_xpos;
    gint win_ypos;
    gint glcontext_width;
    gint glcontext_height;
    gboolean visible;
    gboolean isAlive;

    //intput frame buffer object (video -> GL)
    GLuint fbo;
    GLuint depthBuffer;
    GLuint textureFBO;
    GLuint textureFBOWidth;
    GLuint textureFBOHeight;

    //graphic frame buffer object (GL texture -> GL scene)
    GLuint graphicFBO;
    GLuint graphicDepthBuffer;
    GLuint graphicTexture;

    //filter frame buffer object (GL -> GL)
    GLuint requestedFBO;
    GLuint requestedDepthBuffer;
    GLuint requestedTextureFBO;
    GLuint requestedTextureFBOWidth;
    GLuint requestedTextureFBOHeight;
    GLuint usedFBO;
    GLuint usedDepthBuffer;
    GLuint usedTextureFBO;
    GLuint usedTextureFBOWidth;
    GLuint usedTextureFBOHeight;
    GLCB glsceneFBO_cb;
    GLuint inputTextureWidth;
    GLuint inputTextureHeight;
    GLuint inputTexture;
    GLuint rejectedFBO;
    GLuint rejectedDepthBuffer;
    GLuint rejectedTextureFBO;

    //displayed texture
    GLuint displayedTexture;
    GLuint displayedTextureWidth;
    GLuint displayedTextureHeight;

    GLuint requestedTexture;
    GLuint requestedTexture_u;
    GLuint requestedTexture_v;
	GstVideoFormat requestedVideo_format;
    GLuint requestedTextureWidth;
    GLuint requestedTextureHeight;
     
    GLuint candidateTexture;
    GLuint candidateTexture_u;
    GLuint candidateTexture_v;
    GstVideoFormat candidateVideo_format;
    GLuint candidateTextureWidth;
    GLuint candidateTextureHeight;
    gpointer candidateData;
    
    GLuint currentTexture;
    GLuint currentTexture_u;
    GLuint currentTexture_v;
    GstVideoFormat currentVideo_format;
    GLuint currentTextureWidth;
    GLuint currentTextureHeight;

    GLuint textureTrash;
    GLuint textureTrash_u;
    GLuint textureTrash_v;

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
void gst_gl_display_initGLContext (GstGLDisplay* display, 
                                   GLint x, GLint y, 
                                   GLint graphic_width, GLint graphic_height,
                                   GLint video_width, GLint video_height,
                                   gulong winId,
                                   gboolean visible);
void gst_gl_display_setClientReshapeCallback (GstGLDisplay* display, CRCB cb);
void gst_gl_display_setClientDrawCallback (GstGLDisplay* display, CDCB cb);
void gst_gl_display_setVisibleWindow (GstGLDisplay* display, gboolean visible);
void gst_gl_display_textureRequested (GstGLDisplay* display, GstVideoFormat format, 
                                      gint width, gint height, guint* texture,
                                      guint* texture_u, guint* texture_v);
void gst_gl_display_textureChanged (GstGLDisplay* display, GstVideoFormat video_format, 
                                    GLuint texture, GLuint texture_u, GLuint texture_v, 
                                    gint width, gint height, gpointer data, GLuint* outputTexture);
void gst_gl_display_clearTexture (GstGLDisplay* display, guint texture, 
                                  guint texture_u, guint texture_v);

void gst_gl_display_videoChanged (GstGLDisplay* display, GstVideoFormat video_format,
                                  gpointer data);
gboolean gst_gl_display_postRedisplay (GstGLDisplay* display, GLuint texture, gint width, gint height);
void gst_gl_display_requestFBO (GstGLDisplay* display, gint width, gint height, 
                                guint* fbo, guint* depthbuffer, guint* texture);
void gst_gl_display_useFBO (GstGLDisplay* display, gint textureFBOWidth, gint textureFBOheight, 
                            guint fbo, guint depthbuffer, guint textureFBO, GLCB cb,
                            guint inputTextureWidth, guint inputTextureHeight, guint inputTexture);
void gst_gl_display_rejectFBO (GstGLDisplay* display, guint fbo, 
                               guint depthbuffer, guint texture);
void gst_gl_display_set_windowId (GstGLDisplay* display, gulong winId);
void gst_gl_display_resetGLcontext (GstGLDisplay* display, 
                                    gint glcontext_width, gint glcontext_height);

#endif
