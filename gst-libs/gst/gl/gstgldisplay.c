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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgldisplay.h"
#include <gst/gst.h>

static void gst_gl_display_finalize (GObject * object);
static gpointer gst_gl_display_glutThreadFunc (GstGLDisplay* display);
static void gst_gl_display_glutCreateWindow (GstGLDisplay* display);
static void gst_gl_display_glutGenerateFBO (GstGLDisplay *display);
static void gst_gl_display_glutUseFBO (GstGLDisplay *display);
static void gst_gl_display_glutDestroyFBO (GstGLDisplay *display);
static void gst_gl_display_glutDestroyWindow (GstGLDisplay* display);
static void gst_gl_display_glutSetVisibleWindow (GstGLDisplay* display);
static void gst_gl_display_glutPrepareTexture (GstGLDisplay* display);
static void gst_gl_display_glutUpdateTexture (GstGLDisplay* display);
static void gst_gl_display_glutCleanTexture (GstGLDisplay* display);
static void gst_gl_display_glutUpdateVideo (GstGLDisplay* display);
static void gst_gl_display_glutPostRedisplay (GstGLDisplay* display);
static void gst_gl_display_glut_idle (void);
static void gst_gl_display_glutDispatchAction (GstGLDisplayMsg *msg);
static gboolean gst_gl_display_checkMsgValidity (GstGLDisplayMsg *msg);
void gst_gl_display_lock (GstGLDisplay* display);
void gst_gl_display_unlock (GstGLDisplay* display);
void gst_gl_display_postMessage (GstGLDisplayAction action, GstGLDisplay* display);
void gst_gl_display_onReshape(gint width, gint height);
void gst_gl_display_draw (void);
void gst_gl_display_onClose (void);
void gst_gl_display_make_texture (GstGLDisplay* display);
void gst_gl_display_fill_texture (GstGLDisplay* display);
void gst_gl_display_draw_texture (GstGLDisplay* display);
void gst_gl_display_draw_graphic (GstGLDisplay* display);
void gst_gl_display_fill_video (GstGLDisplay* display);
GLhandleARB gst_gl_display_loadGLSLprogram (gchar* textFProgram);
void checkFramebufferStatus(void);
GST_BOILERPLATE (GstGLDisplay, gst_gl_display, GObject, G_TYPE_OBJECT);


//------------------------------------------------------------
//-------------------- Glut context management ---------------
//------------------------------------------------------------ 

//(key=int glutWinId) and (value=GstGLDisplay *display)
static GHashTable *gst_gl_display_map = NULL;

//all glut functions and opengl primitives are called in this thread
static GThread *gst_gl_display_glutThread = NULL;

//-timepoped by glutIdleFunc
static GAsyncQueue *gst_gl_display_messageQueue = NULL;


//------------------------------------------------------------
//---------------------- For klass GstGLDisplay ---------------
//------------------------------------------------------------

static void
gst_gl_display_base_init (gpointer g_class)
{
}

static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{ 
    G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;
}


static void
gst_gl_display_init (GstGLDisplay *display, GstGLDisplayClass *klass)
{
    display->mutex = g_mutex_new ();
    display->texturePool = g_queue_new ();
    display->cond_make = g_cond_new ();
    display->cond_fill = g_cond_new ();
    display->cond_clear = g_cond_new ();
    display->cond_video = g_cond_new ();
    display->cond_generateFBO = g_cond_new ();
    display->cond_useFBO = g_cond_new ();
    display->cond_destroyFBO = g_cond_new ();
    display->cond_create = g_cond_new ();
    display->cond_destroy = g_cond_new ();

    display->fbo = 0;
    display->depthBuffer = 0;
    display->textureFBO = 0;
    display->textureFBOWidth = 0;
    display->textureFBOHeight = 0;

    display->graphicFBO = 0;
    display->graphicDepthBuffer = 0;
    display->graphicTexture = 0;

    display->requestedFBO = 0;
    display->requestedDepthBuffer = 0;
    display->requestedTextureFBO = 0;
    display->requestedTextureFBOWidth = 0;
    display->requestedTextureFBOHeight = 0;
    display->usedFBO = 0;
    display->usedDepthBuffer = 0;
    display->usedTextureFBO = 0;
    display->usedTextureFBOWidth = 0;
    display->usedTextureFBOHeight = 0;
    display->glsceneFBO_cb = NULL;
    display->inputTextureWidth = 0;
    display->inputTextureHeight = 0;
    display->inputTexture = 0;
    display->rejectedFBO = 0;
    display->rejectedDepthBuffer = 0;
    display->rejectedTextureFBO = 0;

    display->requestedTexture = 0;
    display->requestedTexture_u = 0;
    display->requestedTexture_v = 0;
    display->requestedVideo_format = 0;
    display->requestedTextureWidth = 0;
    display->requestedTextureHeight = 0;

    display->candidateTexture = 0;
    display->candidateTexture_u = 0;
    display->candidateTexture_v = 0;
    display->candidateVideo_format = 0;
    display->candidateTextureWidth = 0;
    display->candidateTextureHeight = 0;
    display->candidateData = NULL;

    display->currentTexture = 0;
    display->currentTexture_u = 0;
    display->currentTexture_v = 0;
    display->currentVideo_format = 0;
    display->currentTextureWidth = 0;
    display->currentTextureHeight = 0;

    display->textureTrash = 0;
    display->textureTrash_u = 0;
    display->textureTrash_v = 0;

    display->videoFBO = 0;
    display->videoDepthBuffer = 0;
    display->videoTexture = 0;
    display->videoTexture_u = 0;
    display->videoTexture_v = 0;
    display->outputWidth = 0;
    display->outputHeight = 0;
    display->outputVideo_format = 0;
    display->outputData = NULL;

    display->glutWinId = -1;
    display->winId = 0;
    display->win_xpos = 0;
    display->win_ypos = 0;
    display->glcontext_width = 0;
    display->glcontext_height = 0;
    display->visible = FALSE;
    display->isAlive = TRUE;
    display->clientReshapeCallback = NULL;
    display->clientDrawCallback = NULL;
    display->title = g_string_new ("OpenGL renderer ");

    display->GLSLProgram_YUY2 = 0;
    display->GLSLProgram_UYVY = 0;
    display->GLSLProgram_I420_YV12 = 0;
    display->GLSLProgram_AYUV = 0;

    display->GLSLProgram_to_YUY2 = 0;
    display->GLSLProgram_to_UYVY = 0;
    display->GLSLProgram_to_I420_YV12 = 0;
    display->GLSLProgram_to_AYUV = 0;

    //YUY2:r,g,a
    //UYVY:a,b,r
    display->textFProgram_YUY2_UYVY =
	    "uniform sampler2DRect Ytex, UVtex;\n"
	    "void main(void) {\n"
	    "  float fx, fy, y, u, v, r, g, b;\n"
	    "  fx   = gl_TexCoord[0].x;\n"	  
	    "  fy   = gl_TexCoord[0].y;\n"
	    "  y = texture2DRect(Ytex,vec2(fx,fy)).%c;\n"
	    "  u = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
	    "  v = texture2DRect(UVtex,vec2(fx*0.5,fy)).%c;\n"
	    "  y=1.164*(y-0.0627);\n"
	    "  u=u-0.5;\n"
	    "  v=v-0.5;\n"
	    "  r = y+1.5958*v;\n"
	    "  g = y-0.39173*u-0.81290*v;\n"
	    "  b = y+2.017*u;\n"
	    "  gl_FragColor = vec4(r, g, b, 1.0);\n"
	    "}\n";

    display->textFProgram_I420_YV12 =
	    "uniform sampler2DRect Ytex,Utex,Vtex;\n"
	    "void main(void) {\n"
	    "  float r,g,b,y,u,v;\n"
	    "  vec2 nxy=gl_TexCoord[0].xy;\n"
	    "  y=texture2DRect(Ytex,nxy).r;\n"
	    "  u=texture2DRect(Utex,nxy*0.5).r;\n"
	    "  v=texture2DRect(Vtex,nxy*0.5).r;\n"
	    "  y=1.1643*(y-0.0625);\n"
	    "  u=u-0.5;\n"
	    "  v=v-0.5;\n"
	    "  r=y+1.5958*v;\n"
	    "  g=y-0.39173*u-0.81290*v;\n"
	    "  b=y+2.017*u;\n"
	    "  gl_FragColor=vec4(r,g,b,1.0);\n"
	    "}\n";

    display->textFProgram_AYUV =
	    "uniform sampler2DRect tex;\n"
	    "void main(void) {\n"
	    "  float r,g,b,y,u,v;\n"
	    "  vec2 nxy=gl_TexCoord[0].xy;\n"
	    "  y=texture2DRect(tex,nxy).r;\n"
	    "  u=texture2DRect(tex,nxy).g;\n"
	    "  v=texture2DRect(tex,nxy).b;\n"
	    "  y=1.1643*(y-0.0625);\n"
	    "  u=u-0.5;\n"
	    "  v=v-0.5;\n"
	    "  r=y+1.5958*v;\n"
	    "  g=y-0.39173*u-0.81290*v;\n"
	    "  b=y+2.017*u;\n"
	    "  gl_FragColor=vec4(r,g,b,1.0);\n"
	    "}\n";

    //YUY2:y2,u,y1,v
    //UYVY:v,y1,u,y2
    display->textFProgram_to_YUY2_UYVY =
	    "uniform sampler2DRect tex;\n"
	    "void main(void) {\n"
	    "  float fx,fy,r,g,b,r2,g2,b2,y1,y2,u,v;\n"
        "  fx = gl_TexCoord[0].x;\n"	  
	    "  fy = gl_TexCoord[0].y;\n"
        "  r=texture2DRect(tex,vec2(fx*2.0,fy)).r;\n"
	    "  g=texture2DRect(tex,vec2(fx*2.0,fy)).g;\n"
	    "  b=texture2DRect(tex,vec2(fx*2.0,fy)).b;\n"
        "  r2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).r;\n"
	    "  g2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).g;\n"
	    "  b2=texture2DRect(tex,vec2(fx*2.0+1.0,fy)).b;\n"
        "  y1=0.299011*r + 0.586987*g + 0.114001*b;\n"
        "  y2=0.299011*r2 + 0.586987*g2 + 0.114001*b2;\n"
        "  u=-0.148246*r -0.29102*g + 0.439266*b;\n"
        "  v=0.439271*r - 0.367833*g - 0.071438*b ;\n"
        "  y1=0.858885*y1 + 0.0625;\n"
        "  y2=0.858885*y2 + 0.0625;\n"
	    "  u=u + 0.5;\n"
	    "  v=v + 0.5;\n"
        "  gl_FragColor=vec4(%s);\n"
	    "}\n";

    display->textFProgram_to_I420_YV12 =
        "uniform sampler2DRect tex;\n"
        "uniform float w, h;\n"
	    "void main(void) {\n"
	    "  float r,g,b,r2,b2,g2,y,u,v;\n"
	    "  vec2 nxy=gl_TexCoord[0].xy;\n"
        "  vec2 nxy2=mod(2.0*nxy, vec2(w, h));\n"
        "  r=texture2DRect(tex,nxy).r;\n"
	    "  g=texture2DRect(tex,nxy).g;\n"
	    "  b=texture2DRect(tex,nxy).b;\n"
        "  r2=texture2DRect(tex,nxy2).r;\n"
	    "  g2=texture2DRect(tex,nxy2).g;\n"
	    "  b2=texture2DRect(tex,nxy2).b;\n"
        "  y=0.299011*r + 0.586987*g + 0.114001*b;\n"
        "  u=-0.148246*r2 -0.29102*g2 + 0.439266*b2;\n"
        "  v=0.439271*r2 - 0.367833*g2 - 0.071438*b2 ;\n"
        "  y=0.858885*y + 0.0625;\n"
	    "  u=u + 0.5;\n"
	    "  v=v + 0.5;\n"
	    "  gl_FragData[0] = vec4(y, 0.0, 0.0, 1.0);\n"
        "  gl_FragData[1] = vec4(u, 0.0, 0.0, 1.0);\n"
        "  gl_FragData[2] = vec4(v, 0.0, 0.0, 1.0);\n"
	    "}\n";

    display->textFProgram_to_AYUV =
	    "uniform sampler2DRect tex;\n"
	    "void main(void) {\n"
	    "  float r,g,b,y,u,v;\n"
	    "  vec2 nxy=gl_TexCoord[0].xy;\n"
	    "  r=texture2DRect(tex,nxy).r;\n"
	    "  g=texture2DRect(tex,nxy).g;\n"
	    "  b=texture2DRect(tex,nxy).b;\n"
        "  y=0.299011*r + 0.586987*g + 0.114001*b;\n"
        "  u=-0.148246*r -0.29102*g + 0.439266*b;\n"
        "  v=0.439271*r - 0.367833*g - 0.071438*b ;\n"
        "  y=0.858885*y + 0.0625;\n"
	    "  u=u + 0.5;\n"
	    "  v=v + 0.5;\n"
	    "  gl_FragColor=vec4(y,u,v,1.0);\n"
	    "}\n";
}

static void
gst_gl_display_finalize (GObject *object)
{
    GstGLDisplay *display = GST_GL_DISPLAY (object);
    
    //request glut window destruction
    //blocking call because display must be alive
    gst_gl_display_lock (display);
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_DESTROY, display);
    g_cond_wait (display->cond_destroy, display->mutex);
    gst_gl_display_unlock (display);

	if (display->texturePool) {
        g_queue_free (display->texturePool);
        display->texturePool = NULL;
    }  
	
    if (display->title) {
        g_string_free (display->title, TRUE);
        display->title = NULL;
    }  
    if (display->mutex) {
        g_mutex_free (display->mutex);
        display->mutex = NULL;
    }
    if (display->cond_make) {
        g_cond_free (display->cond_make);
        display->cond_make = NULL;
    }
    if (display->cond_fill) {
        g_cond_free (display->cond_fill);
        display->cond_fill = NULL;
    }
    if (display->cond_clear) {
        g_cond_free (display->cond_clear);
        display->cond_clear = NULL;
    }
    if (display->cond_video) {
        g_cond_free (display->cond_video);
        display->cond_video = NULL;
    }
    if (display->cond_generateFBO) {
        g_cond_free (display->cond_generateFBO);
        display->cond_generateFBO = NULL;
    }
    if (display->cond_useFBO) {
        g_cond_free (display->cond_useFBO);
        display->cond_useFBO = NULL;
    }
    if (display->cond_destroyFBO) {
        g_cond_free (display->cond_destroyFBO);
        display->cond_destroyFBO = NULL;
    }
    if (display->cond_create) {
        g_cond_free (display->cond_create);
        display->cond_create = NULL;
    }
    if (display->cond_destroy) {
        g_cond_free (display->cond_destroy);
        display->cond_destroy = NULL;
    }
    if (display->clientReshapeCallback)
        display->clientReshapeCallback = NULL;
    if (display->clientDrawCallback)
        display->clientDrawCallback = NULL;
    if (display->glsceneFBO_cb)
        display->glsceneFBO_cb = NULL;
    
    //at this step, the next condition imply that the last display has been pushed 
    if (g_hash_table_size (gst_gl_display_map) == 0)
    {
        g_thread_join (gst_gl_display_glutThread);
        g_print ("Glut thread joined\n");
        gst_gl_display_glutThread = NULL;
        g_async_queue_unref (gst_gl_display_messageQueue);
        g_hash_table_unref  (gst_gl_display_map);
    }
}


/* The glut thread handles glut events and GstGLDisplayMsg messages */
static gpointer
gst_gl_display_glutThreadFunc (GstGLDisplay *display)
{
    static char *argv = "gst-launch-0.10";
    static gint argc = 1; 

    //-display  DISPLAY
    //Specify the X server to connect to. If not specified, the value of the DISPLAY environment variable is used.
    //Should be pass through a glimagesink property
    glutInit(&argc, &argv);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);
    
    glutIdleFunc (gst_gl_display_glut_idle);

    gst_gl_display_lock (display);
    gst_gl_display_glutCreateWindow (display);
    gst_gl_display_unlock (display);

    g_print ("Glut mainLoop start\n");
    glutMainLoop ();
    g_print ("Glut mainLoop exited\n");

    return NULL;
}


/* Called by the idle function or when creating glut_thread */
static void
gst_gl_display_glutCreateWindow (GstGLDisplay *display)
{
    gint glutWinId = 0;
    gchar buffer[5];
    GLenum err = 0;

    //prepare opengl context
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowPosition(display->win_xpos, display->win_ypos);
    glutInitWindowSize(display->glcontext_width, display->glcontext_height); 	

    //create opengl context
    sprintf(buffer, "%d", glutWinId);

    display->title =  g_string_append (display->title, buffer);
    glutWinId = glutCreateWindow (display->title->str, display->winId);

    g_print ("Context %d created\n", glutWinId);

    if (display->visible)
        glutShowWindow ();
    else
        glutHideWindow ();

    //Init glew
    err = glewInit();
    if (err != GLEW_OK)
        GST_DEBUG ("Error: %s", glewGetErrorString(err));
    else
    {
        //OpenGL > 2.1.0 and Glew > 1.5.0
        GString* opengl_version = g_string_new ((gchar*) glGetString (GL_VERSION));
        gboolean check_versions = g_str_has_prefix (opengl_version->str, "2.1");
        GString* glew_version = g_string_new ((gchar*) glewGetString (GLEW_VERSION));
        check_versions = check_versions && g_str_has_prefix (glew_version->str, "1.5");


        GST_DEBUG ("GL_VERSION: %s", opengl_version->str);
        GST_DEBUG ("GLEW_VERSION: %s", glew_version->str);
        
        GST_DEBUG ("GL_VENDOR: %s\n", glGetString (GL_VENDOR));
        GST_DEBUG ("GL_RENDERER: %s\n", glGetString (GL_RENDERER));

        g_string_free (opengl_version, TRUE);
        g_string_free (glew_version, TRUE);

        if (!check_versions)
        {
            GST_DEBUG ("Required OpenGL > 2.1.0 and Glew > 1.5.0");
            g_assert_not_reached ();
        }
    }

    if (GLEW_EXT_framebuffer_object)
    {
        GST_DEBUG ("Context %d, EXT_framebuffer_object supported: yes", glutWinId);

        //-- init intput frame buffer object (video -> GL)

        //setup FBO
        glGenFramebuffersEXT (1, &display->fbo);
        glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->fbo);

        //setup the render buffer for depth	
        glGenRenderbuffersEXT(1, &display->depthBuffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->depthBuffer);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
            display->textureFBOWidth, display->textureFBOHeight);

        //setup a texture to render to
        glGenTextures (1, &display->textureFBO);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->textureFBO);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 
            display->textureFBOWidth, display->textureFBOHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
            GL_TEXTURE_RECTANGLE_ARB, display->textureFBO, 0);

        //attach the depth render buffer to the FBO
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, 
            GL_RENDERBUFFER_EXT, display->depthBuffer);

        g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
            GL_FRAMEBUFFER_COMPLETE_EXT);

        //unbind the FBO
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

        //-- init graphic frame buffer object (GL texture -> GL scene)

        //setup FBO
        glGenFramebuffersEXT (1, &display->graphicFBO);
        glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->graphicFBO);

        //setup the render buffer for depth	
        glGenRenderbuffersEXT(1, &display->graphicDepthBuffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->graphicDepthBuffer);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, 
            display->glcontext_width, display->glcontext_height);

        //setup a texture to render to
        glGenTextures (1, &display->graphicTexture);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->graphicTexture);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 
            display->glcontext_width, display->glcontext_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
            GL_TEXTURE_RECTANGLE_ARB, display->graphicTexture, 0);

        //attach the depth render buffer to the FBO
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, 
            GL_RENDERBUFFER_EXT, display->graphicDepthBuffer);

        g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
            GL_FRAMEBUFFER_COMPLETE_EXT);

        //unbind the FBO
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

        //-- init output frame buffer object (GL -> video)

        display->outputWidth = display->glcontext_width;
        display->outputHeight = display->glcontext_height;

        //setup FBO
        glGenFramebuffersEXT (1, &display->videoFBO);
        glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->videoFBO);

        //setup the render buffer for depth	
        glGenRenderbuffersEXT(1, &display->videoDepthBuffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->videoDepthBuffer);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT, 
            display->outputWidth, display->outputHeight);

        //setup a first texture to render to
        glGenTextures (1, &display->videoTexture);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->videoTexture);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 
            display->outputWidth, display->outputHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the first texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
            GL_TEXTURE_RECTANGLE_ARB, display->videoTexture, 0);

        //setup a second texture to render to
        glGenTextures (1, &display->videoTexture_u);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->videoTexture_u);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 
            display->outputWidth, display->outputHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the second texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT, 
            GL_TEXTURE_RECTANGLE_ARB, display->videoTexture_u, 0);

        //setup a third texture to render to
        glGenTextures (1, &display->videoTexture_v);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->videoTexture_v);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 
            display->outputWidth, display->outputHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the third texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT, 
            GL_TEXTURE_RECTANGLE_ARB, display->videoTexture_v, 0);

        //attach the depth render buffer to the FBO
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, 
            GL_RENDERBUFFER_EXT, display->videoDepthBuffer);

        checkFramebufferStatus();

        g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
            GL_FRAMEBUFFER_COMPLETE_EXT);

        //unbind the FBO
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

        display->multipleRT[0] = GL_COLOR_ATTACHMENT0_EXT;
        display->multipleRT[1] = GL_COLOR_ATTACHMENT1_EXT;
        display->multipleRT[2] = GL_COLOR_ATTACHMENT2_EXT;

    }
    else    
    {
        GST_DEBUG ("Context %d, EXT_framebuffer_object supported: no", glutWinId);
        g_assert_not_reached ();
    }

	//check if fragment program is available, then load them
	if (GLEW_ARB_vertex_program)
	{
        gchar program[2048];

        GST_DEBUG ("Context %d, ARB_fragment_program supported: yes", glutWinId);

        //from video to texture

        sprintf (program, display->textFProgram_YUY2_UYVY, 'r', 'g', 'a');

        display->GLSLProgram_YUY2 = gst_gl_display_loadGLSLprogram (program);

        sprintf (program, display->textFProgram_YUY2_UYVY, 'a', 'b', 'r');

        display->GLSLProgram_UYVY = gst_gl_display_loadGLSLprogram (program);

        display->GLSLProgram_I420_YV12 = gst_gl_display_loadGLSLprogram (display->textFProgram_I420_YV12);

        display->GLSLProgram_AYUV = gst_gl_display_loadGLSLprogram (display->textFProgram_AYUV);

        //from texture to video

        sprintf (program, display->textFProgram_to_YUY2_UYVY, "y2,u,y1,v");
        display->GLSLProgram_to_YUY2 = gst_gl_display_loadGLSLprogram (program);

        sprintf (program, display->textFProgram_to_YUY2_UYVY, "v,y1,u,y2");
        display->GLSLProgram_to_UYVY = gst_gl_display_loadGLSLprogram (program);

        display->GLSLProgram_to_I420_YV12 = gst_gl_display_loadGLSLprogram (display->textFProgram_to_I420_YV12);

        display->GLSLProgram_to_AYUV = gst_gl_display_loadGLSLprogram (display->textFProgram_to_AYUV);
	}
	else 
    {
        GST_DEBUG ("Context %d, ARB_fragment_program supported: no", glutWinId);
        g_assert_not_reached ();
    }

    //setup callbacks
    glutReshapeFunc (gst_gl_display_onReshape);
    glutDisplayFunc (gst_gl_display_draw);
    glutCloseFunc (gst_gl_display_onClose);

    //insert glut context to the map
    display->glutWinId = glutWinId;
    g_hash_table_insert (gst_gl_display_map, GUINT_TO_POINTER (glutWinId), display);

    //check glut id validity
    g_assert (glutGetWindow() == glutWinId);
    GST_DEBUG ("Context %d initialized", display->glutWinId);

    //release display constructor
    g_cond_signal (display->cond_create);
}


/* Called by the idle funtion */
static void
gst_gl_display_glutGenerateFBO (GstGLDisplay *display)
{
    
    glutSetWindow (display->glutWinId);
   
    //-- generate frame buffer object

    //setup FBO
    glGenFramebuffersEXT (1, &display->requestedFBO);
    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->requestedFBO);

    //setup the render buffer for depth	
    glGenRenderbuffersEXT(1, &display->requestedDepthBuffer);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->requestedDepthBuffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
        display->requestedTextureFBOWidth, display->requestedTextureFBOHeight);

    //setup a texture to render to
    glGenTextures (1, &display->requestedTextureFBO);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->requestedTextureFBO);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8, 
        display->requestedTextureFBOWidth, display->requestedTextureFBOHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    //attach the texture to the FBO to renderer to
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, 
        GL_TEXTURE_RECTANGLE_ARB, display->requestedTextureFBO, 0);

    //attach the depth render buffer to the FBO
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, 
        GL_RENDERBUFFER_EXT, display->requestedDepthBuffer);

    g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
        GL_FRAMEBUFFER_COMPLETE_EXT);

    //unbind the FBO
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    g_cond_signal (display->cond_generateFBO);
}


/* Called by the idle funtion */
static void
gst_gl_display_glutUseFBO (GstGLDisplay *display)
{
    
    glutSetWindow (display->glutWinId);

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->usedFBO);

    glPushAttrib(GL_VIEWPORT_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluPerspective(45, (gfloat)display->usedTextureFBOWidth/(gfloat)display->usedTextureFBOHeight, 0.1, 100);	
    //gluOrtho2D(0.0, display->usedTextureFBOWidth, 0.0, display->usedTextureFBOHeight);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, display->usedTextureFBOWidth, display->usedTextureFBOHeight);

    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //the opengl scene
    display->glsceneFBO_cb (display->inputTextureWidth, display->inputTextureHeight, display->inputTexture);

    glDrawBuffer(GL_NONE);

    glUseProgramObjectARB (0);

    glDisable(GL_TEXTURE_RECTANGLE_ARB);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);
    
    g_cond_signal (display->cond_useFBO);
}


/* Called by the idle funtion */
static void
gst_gl_display_glutDestroyFBO (GstGLDisplay *display)
{
    
    glutSetWindow (display->glutWinId);

    glDeleteFramebuffersEXT (1, &display->rejectedFBO); 
    glDeleteRenderbuffersEXT(1, &display->rejectedDepthBuffer);
    glDeleteTextures (1, &display->rejectedTextureFBO);
    display->rejectedFBO = 0;
    display->rejectedDepthBuffer = 0;
    display->rejectedTextureFBO = 0;

    g_cond_signal (display->cond_destroyFBO);
}


/* Called by the idle function */
static void
gst_gl_display_glutDestroyWindow (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);
    glutReshapeFunc (NULL);
    glutDestroyWindow (display->glutWinId);
    glUseProgramObjectARB (0);

    glDeleteObjectARB (display->GLSLProgram_YUY2);
    glDeleteObjectARB (display->GLSLProgram_UYVY);
    glDeleteObjectARB (display->GLSLProgram_I420_YV12);
    glDeleteObjectARB (display->GLSLProgram_AYUV);

    glDeleteObjectARB (display->GLSLProgram_to_YUY2);
    glDeleteObjectARB (display->GLSLProgram_to_UYVY);
    glDeleteObjectARB (display->GLSLProgram_to_I420_YV12);
    glDeleteObjectARB (display->GLSLProgram_to_AYUV);

    glDeleteFramebuffersEXT (1, &display->fbo); 
    glDeleteRenderbuffersEXT(1, &display->depthBuffer);
    glDeleteTextures (1, &display->textureFBO);

    glDeleteFramebuffersEXT (1, &display->graphicFBO); 
    glDeleteRenderbuffersEXT(1, &display->graphicDepthBuffer);
    glDeleteTextures (1, &display->graphicTexture);

    glDeleteFramebuffersEXT (1, &display->videoFBO); 
    glDeleteRenderbuffersEXT(1, &display->videoDepthBuffer);
    glDeleteTextures (1, &display->videoTexture);
    glDeleteTextures (1, &display->videoTexture_u);
    glDeleteTextures (1, &display->videoTexture_v);

    //clean up the texture pool
    while (g_queue_get_length (display->texturePool))
    {
	    GstGLDisplayTex* tex = g_queue_pop_head (display->texturePool);
    	
	    //delete textures
	    glDeleteTextures (1, &tex->texture);
	    if (tex->texture_u) {
		    glDeleteTextures (1, &tex->texture_u);
	    }
	    if (tex->texture_v) {
		    glDeleteTextures (1, &tex->texture_v);
	    }
    }

    g_hash_table_remove (gst_gl_display_map, GINT_TO_POINTER (display->glutWinId)); 
    g_print ("glut window destroyed: %d\n", display->glutWinId);

    //if the map is empty, leaveMainloop and join the thread
    if (g_hash_table_size (gst_gl_display_map) == 0)
        glutLeaveMainLoop ();

    //release display destructor
    g_cond_signal (display->cond_destroy);
}


/* Called by the idle function */
static void
gst_gl_display_glutSetVisibleWindow (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);
	if (display->visible)
        glutShowWindow ();
    else
        glutHideWindow ();
}


/* Called by the idle function */
static void
gst_gl_display_glutPrepareTexture (GstGLDisplay * display)
{
    glutSetWindow (display->glutWinId);
    gst_gl_display_make_texture (display);
    g_cond_signal (display->cond_make);
}


/* Called by the idle function */
static void
gst_gl_display_glutUpdateTexture (GstGLDisplay * display)
{
    glutSetWindow (display->glutWinId);
    gst_gl_display_fill_texture (display);
    gst_gl_display_draw_texture (display);
    g_cond_signal (display->cond_fill);
}


/* Called by the idle function */
static void
gst_gl_display_glutCleanTexture (GstGLDisplay * display)
{
    GstGLDisplayTex* tex = NULL;

    glutSetWindow (display->glutWinId);

    //contructuct a texture pool element
    tex = g_new0 (GstGLDisplayTex, 1);
    tex->texture = display->textureTrash;
    tex->texture_u = display->textureTrash_u;
    tex->texture_v = display->textureTrash_v;

    display->textureTrash = 0;
    display->textureTrash_u = 0;
    display->textureTrash_v = 0;

    //add tex to the pool, it makes texture allocation reusable
    g_queue_push_tail (display->texturePool, tex);

    g_cond_signal (display->cond_clear);
}


/* Called by the idle function */
static void
gst_gl_display_glutUpdateVideo (GstGLDisplay * display)
{
    glutSetWindow (display->glutWinId);
    gst_gl_display_draw_graphic (display);
    gst_gl_display_fill_video (display);
    g_cond_signal (display->cond_video);
}


/* Called by the idle function */
static void
gst_gl_display_glutPostRedisplay (GstGLDisplay * display)
{
    glutSetWindow (display->glutWinId);
    glutPostRedisplay ();
}


/* Called continuously from freeglut while no events are pending */
static void 
gst_gl_display_glut_idle (void)
{   
    GTimeVal timeout;
    GstGLDisplayMsg *msg;

    //check for pending actions that require a glut context
    g_get_current_time (&timeout);
    g_time_val_add (&timeout, 1000000L); //timeout 1 sec
    msg = g_async_queue_timed_pop (gst_gl_display_messageQueue, &timeout);
    if (msg) 
    {
        if (gst_gl_display_checkMsgValidity (msg))
            gst_gl_display_glutDispatchAction (msg);
        while (g_async_queue_length (gst_gl_display_messageQueue))
        {
            msg = g_async_queue_pop (gst_gl_display_messageQueue);
            if (gst_gl_display_checkMsgValidity (msg))
                gst_gl_display_glutDispatchAction (msg);  
        }
    }
    else g_print ("timeout reached in idle func\n");
}


/* Called by the glut idle function */
static void
gst_gl_display_glutDispatchAction (GstGLDisplayMsg* msg)
{
    gst_gl_display_lock (msg->display);
    switch (msg->action)
    {
        case GST_GL_DISPLAY_ACTION_CREATE:
            gst_gl_display_glutCreateWindow (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_DESTROY:
            gst_gl_display_glutDestroyWindow (msg->display);
            break;
		case GST_GL_DISPLAY_ACTION_VISIBLE:
            gst_gl_display_glutSetVisibleWindow (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_PREPARE:
            gst_gl_display_glutPrepareTexture (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_CHANGE:
            gst_gl_display_glutUpdateTexture (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_CLEAR:
            gst_gl_display_glutCleanTexture (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_VIDEO:
            gst_gl_display_glutUpdateVideo (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_REDISPLAY:
            gst_gl_display_glutPostRedisplay (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_GENFBO:
            gst_gl_display_glutGenerateFBO (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_DELFBO:
            gst_gl_display_glutDestroyFBO (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_USEFBO:
            gst_gl_display_glutUseFBO (msg->display);
            break;
        default:
            g_assert_not_reached ();
    }
    gst_gl_display_unlock (msg->display);
    g_free (msg);
}


/* Return false if the message is out of date */
static gboolean
gst_gl_display_checkMsgValidity (GstGLDisplayMsg *msg)
{
    gboolean valid = TRUE;

    switch (msg->action)
    {
        case GST_GL_DISPLAY_ACTION_CREATE:
            valid = TRUE;
            break;
        case GST_GL_DISPLAY_ACTION_DESTROY:   
		case GST_GL_DISPLAY_ACTION_VISIBLE:            
        case GST_GL_DISPLAY_ACTION_PREPARE:           
        case GST_GL_DISPLAY_ACTION_CHANGE:           
        case GST_GL_DISPLAY_ACTION_CLEAR:          
        case GST_GL_DISPLAY_ACTION_VIDEO:
        case GST_GL_DISPLAY_ACTION_REDISPLAY:
        case GST_GL_DISPLAY_ACTION_GENFBO:
        case GST_GL_DISPLAY_ACTION_DELFBO:
        case GST_GL_DISPLAY_ACTION_USEFBO:
            //msg is out of date if the associated display is not in the map
            if (!g_hash_table_lookup (gst_gl_display_map, GINT_TO_POINTER (msg->glutWinId)))
                valid = FALSE;
            break;
        default:
            g_assert_not_reached ();
    }

    return valid;
}


//------------------------------------------------------------
//---------------------- For each GstGLDisplay ---------------
//------------------------------------------------------------

GstGLDisplay *
gst_gl_display_new (void)
{
    return g_object_new (GST_TYPE_GL_DISPLAY, NULL);
}

/* Init an opengl context */
void
gst_gl_display_initGLContext (GstGLDisplay *display, 
                              GLint x, GLint y, 
                              GLint graphic_width, GLint graphic_height,
                              GLint video_width, GLint video_height,
                              gulong winId,
                              gboolean visible)
{  
    gst_gl_display_lock (display);

    display->winId = winId;
    display->win_xpos = x;
    display->win_ypos = y;
    display->glcontext_width = graphic_width;
    display->glcontext_height = graphic_height;
    display->textureFBOWidth = video_width;
    display->textureFBOHeight = video_height;
    display->visible = visible;

    //if no glut_thread exists, create it with a window associated to the display
    if (!gst_gl_display_map)
    {
        gst_gl_display_messageQueue = g_async_queue_new ();
		gst_gl_display_map = g_hash_table_new (g_direct_hash, g_direct_equal);
        gst_gl_display_glutThread = g_thread_create (
            (GThreadFunc) gst_gl_display_glutThreadFunc, display, TRUE, NULL);
        g_cond_wait (display->cond_create, display->mutex);
    }
    //request glut window creation
    else 
    {
        //blocking call because glut context must be alive
        gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_CREATE, display);
        g_cond_wait (display->cond_create, display->mutex);
    }
    gst_gl_display_unlock (display);    
}


void 
gst_gl_display_lock (GstGLDisplay * display)
{
    g_mutex_lock (display->mutex);
}


void 
gst_gl_display_unlock (GstGLDisplay * display)
{
    g_mutex_unlock (display->mutex);
}


/* Post a message that will be handled by the idle function */
void
gst_gl_display_postMessage (GstGLDisplayAction action, GstGLDisplay* display)
{
    GstGLDisplayMsg* msg = g_new0 (GstGLDisplayMsg, 1);
    msg->action = action;
    msg->glutWinId = display->glutWinId;
    msg->display = display;
    g_async_queue_push (gst_gl_display_messageQueue, msg);
}


/* Called by gst_gl elements */
void
gst_gl_display_setClientReshapeCallback (GstGLDisplay * display, CRCB cb)
{
    gst_gl_display_lock (display);
    display->clientReshapeCallback = cb;
    gst_gl_display_unlock (display);
}


/* Called by gst_gl elements */
void
gst_gl_display_setClientDrawCallback (GstGLDisplay * display, CDCB cb)
{
    gst_gl_display_lock (display);
    display->clientDrawCallback = cb;
    gst_gl_display_unlock (display);
}


/* Called by gst gl elements */
void 
gst_gl_display_setVisibleWindow (GstGLDisplay * display, gboolean visible)
{
    gst_gl_display_lock (display);
    if (display->visible != visible)
    {
        display->visible = visible;
        gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_VISIBLE, display);
    }
    gst_gl_display_unlock (display);
}


/* Called by gstglbuffer */
void 
gst_gl_display_textureRequested (GstGLDisplay * display, GstVideoFormat video_format, 
                                 gint width, gint height, guint *texture,
                                 guint *texture_u, guint *texture_v)
{
    gst_gl_display_lock (display);
    display->requestedVideo_format = video_format;
    display->requestedTextureWidth = width;
    display->requestedTextureHeight = height;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_PREPARE, display);
    g_cond_wait (display->cond_make, display->mutex);
    *texture = display->requestedTexture;
    *texture_u = display->requestedTexture_u;
    *texture_v = display->requestedTexture_v;
    gst_gl_display_unlock (display);
}


/* Called by gst_gl elements */
void 
gst_gl_display_textureChanged (GstGLDisplay* display, GstVideoFormat video_format, 
                               GLuint texture, GLuint texture_u, GLuint texture_v, 
                               gint width, gint height, gpointer data)
{
    gst_gl_display_lock (display);
    display->candidateTexture = texture;
    display->candidateTexture_u = texture_u;
    display->candidateTexture_v = texture_v;
    display->candidateVideo_format = video_format;
    display->candidateTextureWidth = width;
    display->candidateTextureHeight = height;
    display->candidateData = data;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_CHANGE, display);
    g_cond_wait (display->cond_fill, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by gstglbuffer */
void 
gst_gl_display_clearTexture (GstGLDisplay* display, guint texture, 
                             guint texture_u, guint texture_v)
{
    gst_gl_display_lock (display);
    display->textureTrash = texture;
    display->textureTrash_u = texture_u;
    display->textureTrash_v = texture_v;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_CLEAR, display);
    g_cond_wait (display->cond_clear, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by gst_gl elements */
void 
gst_gl_display_videoChanged (GstGLDisplay* display, GstVideoFormat video_format,
                             gpointer data)
{
    gst_gl_display_lock (display);
    //data size is aocciated to the glcontext size
    display->outputVideo_format = video_format;
    display->outputData = data;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_VIDEO, display);
    g_cond_wait (display->cond_video, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by gst_gl elements */
gboolean 
gst_gl_display_postRedisplay (GstGLDisplay* display)
{
    gboolean isAlive = TRUE;
    
    gst_gl_display_lock (display);
    isAlive = display->isAlive;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_REDISPLAY, display);
    gst_gl_display_unlock (display);

    return isAlive;

}


/* Called by gst_gl elements */
void 
gst_gl_display_requestFBO (GstGLDisplay* display, gint width, gint height, 
                           guint* fbo, guint* depthbuffer, guint* texture)
{
    gst_gl_display_lock (display);
    display->requestedTextureFBOWidth = width;
    display->requestedTextureFBOHeight = height;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_GENFBO, display);
    g_cond_wait (display->cond_generateFBO, display->mutex);
    *fbo = display->requestedFBO;
    *depthbuffer = display->requestedDepthBuffer;
    *texture = display->requestedTextureFBO;
    gst_gl_display_unlock (display);
}


/* Called by gst_gl elements */
void 
gst_gl_display_useFBO (GstGLDisplay* display, gint textureFBOWidth, gint textureFBOheight, 
                       guint fbo, guint depthbuffer, guint textureFBO, GLCB cb,
                       guint inputTextureWidth, guint inputTextureHeight, guint inputTexture)
{
    gst_gl_display_lock (display);
    display->usedFBO = fbo;
    display->usedDepthBuffer = depthbuffer;
    display->usedTextureFBO = textureFBO;
    display->usedTextureFBOWidth = textureFBOWidth;
    display->usedTextureFBOHeight = textureFBOheight;
    display->glsceneFBO_cb = cb;
    display->inputTextureWidth = inputTextureWidth;
    display->inputTextureHeight = inputTextureHeight;
    display->inputTexture = inputTexture;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_USEFBO, display);
    g_cond_wait (display->cond_useFBO, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by gst_gl elements */
void 
gst_gl_display_rejectFBO (GstGLDisplay* display, guint fbo, 
                          guint depthbuffer, guint texture)
{
    gst_gl_display_lock (display);
    display->rejectedFBO = fbo;
    display->rejectedDepthBuffer = depthbuffer;
    display->rejectedTextureFBO = texture;
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_DELFBO, display);
    g_cond_wait (display->cond_destroyFBO, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by gst_gl elements */
void 
gst_gl_display_set_windowId (GstGLDisplay* display, gulong winId)
{
    static gint glheight = 0;

    gst_gl_display_lock (display);
    gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_DESTROY, display);
    g_cond_wait (display->cond_destroy, display->mutex);
    gst_gl_display_unlock (display);

    if (g_hash_table_size (gst_gl_display_map) == 0)
    {
        g_thread_join (gst_gl_display_glutThread);
        g_print ("Glut thread joined when setting winId\n");
        gst_gl_display_glutThread = NULL;
        g_async_queue_unref (gst_gl_display_messageQueue);
        g_hash_table_unref  (gst_gl_display_map);
        gst_gl_display_map = NULL;
    }

    //init opengl context
    gst_gl_display_initGLContext (display, 
        50, glheight++ * (display->glcontext_height+50) + 50, 
        display->glcontext_width, display->glcontext_height,
        display->textureFBOWidth, display->textureFBOHeight,
        winId,
        TRUE);

}


/* glutReshapeFunc callback */ 
void 
gst_gl_display_onReshape(gint width, gint height)
{ 	 
    gint glutWinId = 0;
    GstGLDisplay *display = NULL;
  
    //retrieve the display associated to the glut context
    glutWinId = glutGetWindow ();
    display = g_hash_table_lookup (gst_gl_display_map, GINT_TO_POINTER (glutWinId));

    //glutGetWindow return 0 if no windows exists, then g_hash_table_lookup return NULL
    if (display == NULL) return;

    gst_gl_display_lock (display);

    //check if a client reshape callback is registered
    if (display->clientReshapeCallback) 
        display->clientReshapeCallback(width, height);

    //default reshape
    else
    {
        glViewport(0, 0, width, height);
  	    glMatrixMode(GL_PROJECTION);
  	    glLoadIdentity();  
        gluOrtho2D(0, width, 0, height);
   	    glMatrixMode(GL_MODELVIEW);
    }

    gst_gl_display_unlock (display);
} 

/* glutDisplayFunc callback */ 
void gst_gl_display_draw(void)
{
    gint glutWinId = 0;
    GstGLDisplay *display = NULL;
  
    //retrieve the display associated to the glut context
    glutWinId = glutGetWindow ();
    display = g_hash_table_lookup (gst_gl_display_map, GINT_TO_POINTER (glutWinId));
    
    //glutGetWindow return 0 if no windows exists, then g_hash_table_lookup return NULL
    if (display == NULL) return;

    //lock the display because gstreamer elements 
    //(and so the main thread) may modify it
    gst_gl_display_lock (display);
    
    //check if video format has been setup
    if (!display->currentVideo_format)
	{
		gst_gl_display_unlock (display);
		return;
	}

    //opengl scene  
    
    glUseProgramObjectARB (0);
    glDisable (GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

    //check if a client draw callback is registered
    if (display->clientDrawCallback) 
    {
		gboolean doRedisplay = 
            display->clientDrawCallback(display->textureFBO,
				display->textureFBOWidth, display->textureFBOHeight);
        
        glFlush();
        glutSwapBuffers();

        if (doRedisplay)
            gst_gl_display_postMessage (GST_GL_DISPLAY_ACTION_REDISPLAY, display);
    }
    //default opengl scene
    else
    {   

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode (GL_PROJECTION);
        glLoadIdentity ();      
	    
	    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->textureFBO);
        glEnable (GL_TEXTURE_RECTANGLE_ARB);

        glBegin (GL_QUADS);
            glTexCoord2i (display->textureFBOWidth, 0);
            glVertex2f (1.0f, 1.0f);
            glTexCoord2i (0, 0);
            glVertex2f (-1.0f, 1.0f);
            glTexCoord2i (0, display->textureFBOHeight);
            glVertex2f (-1.0f, -1.0f);
            glTexCoord2i (display->textureFBOWidth, display->textureFBOHeight);
            glVertex2f (1.0f, -1.0f);
        glEnd ();

        glDisable(GL_TEXTURE_RECTANGLE_ARB);

		glFlush();
        glutSwapBuffers();

	}//end default opengl scene

    gst_gl_display_unlock (display);
}


/* glutCloseFunc callback */ 
void gst_gl_display_onClose (void)
{
    gint glutWinId = 0;
    GstGLDisplay* display = NULL;
  
    //retrieve the display associated to the glut context
    glutWinId = glutGetWindow ();
    display = g_hash_table_lookup (gst_gl_display_map, GINT_TO_POINTER (glutWinId));
    
    //glutGetWindow return 0 if no windows exists, then g_hash_table_lookup return NULL
    if (display == NULL) return;
    
    GST_DEBUG ("on close");

    gst_gl_display_lock (display);
    display->isAlive = FALSE;
    gst_gl_display_unlock (display);
}


/* called by gst_gl_display_glutPrepareTexture (in the glut thread) */
void gst_gl_display_make_texture (GstGLDisplay * display)
{ 
    GstGLDisplayTex* tex = NULL;

    //check if there is a tex available in the pool
    if (g_queue_get_length (display->texturePool))
	    tex = g_queue_pop_head (display->texturePool);
    	
    //one tex is available
    if (tex)
	    display->requestedTexture = tex->texture;
    else
	    glGenTextures (1, &display->requestedTexture);

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->requestedTexture);
    switch (display->requestedVideo_format) {
	    case GST_VIDEO_FORMAT_RGBx:
	    case GST_VIDEO_FORMAT_BGRx:
	    case GST_VIDEO_FORMAT_xRGB:
	    case GST_VIDEO_FORMAT_xBGR:
	    case GST_VIDEO_FORMAT_RGBA:
	    case GST_VIDEO_FORMAT_BGRA:
	    case GST_VIDEO_FORMAT_ARGB:
	    case GST_VIDEO_FORMAT_ABGR:
	    case GST_VIDEO_FORMAT_AYUV:
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
			    display->requestedTextureWidth, display->requestedTextureHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		    break;

	    case GST_VIDEO_FORMAT_RGB:
	    case GST_VIDEO_FORMAT_BGR:
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
			    display->requestedTextureWidth, display->requestedTextureHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		    break;

	    case GST_VIDEO_FORMAT_YUY2:
	    case GST_VIDEO_FORMAT_UYVY:
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
			    display->requestedTextureWidth, display->requestedTextureHeight,
			    0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);

		    //one tex is available
		    if (tex)
			    display->requestedTexture_u = tex->texture_u;
		    else
			    glGenTextures (1, &display->requestedTexture_u);

		    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->requestedTexture_u);
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
			    display->requestedTextureWidth, display->requestedTextureHeight,
			    0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		    break;

	    case GST_VIDEO_FORMAT_I420:
	    case GST_VIDEO_FORMAT_YV12:
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
			    display->requestedTextureWidth, display->requestedTextureHeight,
			    0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

		    //one tex is available
		    if (tex)
			    display->requestedTexture_u = tex->texture_u;
		    else
			    glGenTextures (1, &display->requestedTexture_u);

		    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->requestedTexture_u);
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
			    GST_ROUND_UP_2 (display->requestedTextureWidth) / 2,
			    GST_ROUND_UP_2 (display->requestedTextureHeight) / 2,
			    0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

		    //one tex is available
		    if (tex)
			    display->requestedTexture_v = tex->texture_v;
		    else
			    glGenTextures (1, &display->requestedTexture_v);

		    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->requestedTexture_v);
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
			    GST_ROUND_UP_2 (display->requestedTextureWidth) / 2,
			    GST_ROUND_UP_2 (display->requestedTextureHeight) / 2,
			    0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
		    break;

	    default:
		    g_assert_not_reached ();
    }
    if (tex)
	    g_free (tex);
}


/* called by gst_gl_display_glutUpdateTexture (in the glut thread) */
void
gst_gl_display_fill_texture (GstGLDisplay * display)
{
    GstVideoFormat video_format = display->candidateVideo_format;
    gint width = display->candidateTextureWidth;
    gint height = display->candidateTextureHeight;
    gpointer data = display->candidateData;

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->candidateTexture);

    switch (video_format) {
        case GST_VIDEO_FORMAT_RGBx:
            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                GL_RGBA, GL_UNSIGNED_BYTE, data);
            break;
        case GST_VIDEO_FORMAT_BGRx:
            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                GL_BGRA, GL_UNSIGNED_BYTE, data);
            break;
        case GST_VIDEO_FORMAT_AYUV:
        case GST_VIDEO_FORMAT_xRGB:
            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                GL_BGRA, GL_UNSIGNED_INT_8_8_8_8, data);
            break;
        case GST_VIDEO_FORMAT_xBGR:
            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, data);
            break;
        case GST_VIDEO_FORMAT_YUY2:
        case GST_VIDEO_FORMAT_UYVY:
            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, data);

            glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->candidateTexture_u);
            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                GST_ROUND_UP_2 (width) / 2, height,
                GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
            break;
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
            {
                gint offsetU = 0;
                gint offsetV = 0;

                switch (video_format) 
	            {
		            case GST_VIDEO_FORMAT_I420:
			            offsetU = 1;
			            offsetV = 2;
			            break;
		            case GST_VIDEO_FORMAT_YV12:
			            offsetU = 2;
			            offsetV = 1;
			            break;
		            default:
			            g_assert_not_reached ();
	            }

                glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE, data);

                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->candidateTexture_u);
                glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                    GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    (guint8 *) data +
                    gst_video_format_get_component_offset (video_format, offsetU, width, height));

                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->candidateTexture_v);
                glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                    GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    (guint8 *) data +
                    gst_video_format_get_component_offset (video_format, offsetV, width, height));    
            }
            break;
        default:
            g_assert_not_reached ();
    }

    //candidate textures can now be used
    display->currentTexture = display->candidateTexture;
    display->currentTexture_u = display->candidateTexture_u;
    display->currentTexture_v = display->candidateTexture_v;
    display->currentVideo_format = display->candidateVideo_format;
    display->currentTextureWidth = display->candidateTextureWidth;
    display->currentTextureHeight = display->candidateTextureHeight;

}


/* called by gst_gl_display_glutUpdateTexture (in the glut thread) */
void
gst_gl_display_draw_texture (GstGLDisplay* display)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->fbo);

    glPushAttrib(GL_VIEWPORT_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, display->textureFBOWidth, 0.0, display->textureFBOHeight);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, display->textureFBOWidth, display->textureFBOHeight);

    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	switch (display->currentVideo_format)
	{
		case GST_VIDEO_FORMAT_RGBx:
		case GST_VIDEO_FORMAT_BGRx:
		case GST_VIDEO_FORMAT_xRGB:
		case GST_VIDEO_FORMAT_xBGR:
			{
				glMatrixMode (GL_PROJECTION);
				glLoadIdentity ();
		        
				glEnable(GL_TEXTURE_RECTANGLE_ARB);
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->currentTexture);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
				glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
			}
			break;

		case GST_VIDEO_FORMAT_YUY2:
		case GST_VIDEO_FORMAT_UYVY:
			{
				gint i=0;
				GLhandleARB GLSLProgram_YUY2_UYVY = 0;
				
				switch (display->currentVideo_format) 
				{
					case GST_VIDEO_FORMAT_YUY2:
						GLSLProgram_YUY2_UYVY = display->GLSLProgram_YUY2;
						break;
					case GST_VIDEO_FORMAT_UYVY:
						GLSLProgram_YUY2_UYVY = display->GLSLProgram_UYVY;
						break;
					default:
						g_assert_not_reached ();
				}

                glUseProgramObjectARB (GLSLProgram_YUY2_UYVY);

				glMatrixMode (GL_PROJECTION);
				glLoadIdentity ();
				
				glActiveTextureARB(GL_TEXTURE1_ARB);
				i = glGetUniformLocationARB (GLSLProgram_YUY2_UYVY, "UVtex");
				glUniform1iARB (i, 1);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->currentTexture_u);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glActiveTextureARB(GL_TEXTURE0_ARB);
				i = glGetUniformLocationARB (GLSLProgram_YUY2_UYVY, "Ytex");
				glUniform1iARB (i, 0);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->currentTexture);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			break;

		case GST_VIDEO_FORMAT_I420:
		case GST_VIDEO_FORMAT_YV12:
			{
				gint i=0;

				glUseProgramObjectARB (display->GLSLProgram_I420_YV12);

				glMatrixMode (GL_PROJECTION);
				glLoadIdentity ();
				
				glActiveTextureARB(GL_TEXTURE1_ARB);
				i = glGetUniformLocationARB (display->GLSLProgram_I420_YV12, "Utex");
				glUniform1iARB (i, 1);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->currentTexture_u);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

				glActiveTextureARB(GL_TEXTURE2_ARB);
				i = glGetUniformLocationARB (display->GLSLProgram_I420_YV12, "Vtex");
				glUniform1iARB (i, 2);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->currentTexture_v);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glActiveTextureARB(GL_TEXTURE0_ARB);
				i = glGetUniformLocationARB (display->GLSLProgram_I420_YV12, "Ytex");
				glUniform1iARB (i, 0);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->currentTexture);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			break;

		case GST_VIDEO_FORMAT_AYUV:
			{
				gint i=0;

				glUseProgramObjectARB (display->GLSLProgram_AYUV);

				glMatrixMode (GL_PROJECTION);
				glLoadIdentity ();

                glActiveTextureARB(GL_TEXTURE0_ARB);
				i = glGetUniformLocationARB (display->GLSLProgram_AYUV, "tex");
				glUniform1iARB (i, 0);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->currentTexture);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
			}
			break;

		default:
			g_assert_not_reached ();

	}//end switch display->currentVideo_format

    glBegin (GL_QUADS);
        glTexCoord2i (display->currentTextureWidth, 0);
        glVertex2f (1.0f, -1.0f);
        glTexCoord2i (0, 0);
        glVertex2f (-1.0f, -1.0f);
        glTexCoord2i (0, display->currentTextureHeight);
        glVertex2f (-1.0f, 1.0f);
        glTexCoord2i (display->currentTextureWidth, display->currentTextureHeight);
        glVertex2f (1.0f, 1.0f);
    glEnd ();

    glDrawBuffer(GL_NONE);

    glUseProgramObjectARB (0);

    glDisable(GL_TEXTURE_RECTANGLE_ARB);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    checkFramebufferStatus();
}


/* called by gst_gl_display_glutUpdateTexture (in the glut thread) */
void
gst_gl_display_draw_graphic (GstGLDisplay* display)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->graphicFBO);

    glPushAttrib(GL_VIEWPORT_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluPerspective(45, (gfloat)display->glcontext_width/(gfloat)display->glcontext_height, 0.1, 100);	
    //gluOrtho2D(0.0, display->glcontext_width, 0.0, display->glcontext_height);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, display->glcontext_width, display->glcontext_height);

    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);


    //check if a client draw callback is registered
    if (display->clientDrawCallback) 
    {
        display->clientDrawCallback(display->textureFBO,
			display->textureFBOWidth, display->textureFBOHeight);
    }
    else
    {
        glMatrixMode (GL_PROJECTION);
	    glLoadIdentity ();

        glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->textureFBO);
        glEnable (GL_TEXTURE_RECTANGLE_ARB);
        
        glBegin (GL_QUADS);
            glTexCoord2i (display->textureFBOWidth, 0);
            glVertex2f (1.0f, 1.0f);
            glTexCoord2i (0, 0);
            glVertex2f (-1.0f, 1.0f);
            glTexCoord2i (0, display->textureFBOHeight);
            glVertex2f (-1.0f, -1.0f);
            glTexCoord2i (display->textureFBOWidth, display->textureFBOHeight);
            glVertex2f (1.0f, -1.0f);
        glEnd ();
    }

    glDrawBuffer(GL_NONE);

    glUseProgramObjectARB (0);

    glDisable(GL_TEXTURE_RECTANGLE_ARB);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    checkFramebufferStatus();
}


/* called by gst_gl_display_glutUpdateVideo (in the glut thread) */
void
gst_gl_display_fill_video (GstGLDisplay* display)
{
  
    gint width = display->outputWidth;
    gint height = display->outputHeight;
    GstVideoFormat outputVideo_format = display->outputVideo_format;
    gpointer data = display->outputData;
    
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->videoFBO);

    glPushAttrib(GL_VIEWPORT_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, width, 0.0, height);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, width, height);

    switch (outputVideo_format)
    {
        case GST_VIDEO_FORMAT_RGBx:
        case GST_VIDEO_FORMAT_BGRx:
        case GST_VIDEO_FORMAT_xRGB:
        case GST_VIDEO_FORMAT_xBGR:
            {            
                glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

                glClearColor(0.0, 0.0, 0.0, 0.0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                
                glMatrixMode (GL_PROJECTION);
	            glLoadIdentity ();
                
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->graphicTexture);
                glEnable(GL_TEXTURE_RECTANGLE_ARB);
            }
            break;

        case GST_VIDEO_FORMAT_YUY2:
        case GST_VIDEO_FORMAT_UYVY:
            {
                gint i=0;
                GLhandleARB GLSLProgram_to_YUY2_UYVY = 0;
				
				switch (outputVideo_format) 
				{
					case GST_VIDEO_FORMAT_YUY2:
						GLSLProgram_to_YUY2_UYVY = display->GLSLProgram_to_YUY2;
						break;
					case GST_VIDEO_FORMAT_UYVY:
						GLSLProgram_to_YUY2_UYVY = display->GLSLProgram_to_UYVY;
						break;
					default:
						g_assert_not_reached ();
				}   

                glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

                glClearColor(0.0, 0.0, 0.0, 0.0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	            glUseProgramObjectARB (GLSLProgram_to_YUY2_UYVY);

	            glMatrixMode (GL_PROJECTION);
	            glLoadIdentity ();

                glActiveTextureARB(GL_TEXTURE0_ARB);
                i = glGetUniformLocationARB (GLSLProgram_to_YUY2_UYVY, "tex");
                glUniform1iARB (i, 0);
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->graphicTexture);
            }
            break;

        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
	        {
                gint i=0;
	            
                glDrawBuffers(3, display->multipleRT);
                
                glClearColor(0.0, 0.0, 0.0, 0.0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	            glUseProgramObjectARB (display->GLSLProgram_to_I420_YV12);

	            glMatrixMode (GL_PROJECTION);
	            glLoadIdentity ();

                glActiveTextureARB(GL_TEXTURE0_ARB);
                i = glGetUniformLocationARB (display->GLSLProgram_to_I420_YV12, "tex");
                glUniform1iARB (i, 0);
                i = glGetUniformLocationARB (display->GLSLProgram_to_I420_YV12, "w");
                glUniform1fARB (i, (gfloat)width);
                i = glGetUniformLocationARB (display->GLSLProgram_to_I420_YV12, "h");
                glUniform1fARB (i, (gfloat)height);
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->graphicTexture);
			}
			break;

        case GST_VIDEO_FORMAT_AYUV:
            {
	            gint i=0;

                glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);

                glClearColor(0.0, 0.0, 0.0, 0.0);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	            glUseProgramObjectARB (display->GLSLProgram_to_AYUV);

	            glMatrixMode (GL_PROJECTION);
	            glLoadIdentity ();

                glActiveTextureARB(GL_TEXTURE0_ARB);
                i = glGetUniformLocationARB (display->GLSLProgram_to_AYUV, "tex");
                glUniform1iARB (i, 0);
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->graphicTexture);
            }
            break;

		default:
			g_assert_not_reached ();

	}//end switch display->currentVideo_format

    glBegin (GL_QUADS);
        glTexCoord2i (width, 0);
        glVertex2f (1.0f, 1.0f);
        glTexCoord2i (0, 0);
        glVertex2f (-1.0f, 1.0f);
        glTexCoord2i (0, height);
        glVertex2f (-1.0f, -1.0f);
        glTexCoord2i (width, height);
        glVertex2f (1.0f, -1.0f);
    glEnd (); 

    glDrawBuffer(GL_NONE);

    glUseProgramObjectARB (0);

    glDisable(GL_TEXTURE_RECTANGLE_ARB);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);

    checkFramebufferStatus();

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->videoFBO);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);

    switch (outputVideo_format) {
        case GST_VIDEO_FORMAT_RGBx:
            glReadPixels (0, 0, width, height, GL_RGBA,
                GL_UNSIGNED_BYTE, data);
            break;
        case GST_VIDEO_FORMAT_BGRx:
            glReadPixels (0, 0, width, height, GL_BGRA,
                GL_UNSIGNED_BYTE, data);
            break;
        case GST_VIDEO_FORMAT_xBGR:
            glReadPixels (0, 0, width, height, GL_RGBA,
                GL_UNSIGNED_INT_8_8_8_8, data);
            break;
        case GST_VIDEO_FORMAT_AYUV:
        case GST_VIDEO_FORMAT_xRGB:
            glReadPixels (0, 0, width, height, GL_BGRA,
                GL_UNSIGNED_INT_8_8_8_8, data);
            break;
        case GST_VIDEO_FORMAT_YUY2:
        case GST_VIDEO_FORMAT_UYVY:
            glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, height, GL_BGRA,
                GL_UNSIGNED_INT_8_8_8_8_REV, data);
            break;
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
            {
                gint offsetU = 0;
                gint offsetV = 0;

                switch (outputVideo_format) 
		        {
			        case GST_VIDEO_FORMAT_I420:
				        offsetU = 1;
				        offsetV = 2;
				        break;
			        case GST_VIDEO_FORMAT_YV12:
				        offsetU = 2;
				        offsetV = 1;
				        break;
			        default:
				        g_assert_not_reached ();
		        }

                glReadPixels (0, 0, width, height, GL_LUMINANCE,
                    GL_UNSIGNED_BYTE, data);

                glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
                glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2, 
                    GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    (guint8 *) data +
                    gst_video_format_get_component_offset (outputVideo_format, offsetU, width, height));

                glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
                glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2, 
                    GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    (guint8 *) data +
                    gst_video_format_get_component_offset (outputVideo_format, offsetV, width, height));
            }
            break;
        default:
            g_assert_not_reached ();
    }

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
    checkFramebufferStatus();
}


/* called by gst_gl_display_glutCreateWindow (in the glut thread) */
GLhandleARB
gst_gl_display_loadGLSLprogram (gchar* textFProgram) 
{
    GLhandleARB FHandle = 0;
    GLhandleARB PHandle = 0;
    gchar s[32768]; 
    gint i = 0;

    //Set up program objects
    PHandle = glCreateProgramObjectARB ();

    //Compile the shader
    FHandle = glCreateShaderObjectARB (GL_FRAGMENT_SHADER_ARB);
    glShaderSourceARB (FHandle, 1, (const GLcharARB**)&textFProgram, NULL);
    glCompileShaderARB (FHandle);

    //Print the compilation log
    glGetObjectParameterivARB (FHandle, GL_OBJECT_COMPILE_STATUS_ARB, &i);
    glGetInfoLogARB (FHandle, sizeof(s)/sizeof(char), NULL, s);
    GST_DEBUG ("Compile Log: %s", s);

    //link the shader
    glAttachObjectARB (PHandle, FHandle);
    glLinkProgramARB (PHandle);

    //Print the link log
    glGetInfoLogARB (PHandle, sizeof(s)/sizeof(char), NULL, s);
    GST_DEBUG ("Link Log: %s", s);

    return PHandle;
}


/* Called by gst_gl_display_fill_video */
void 
checkFramebufferStatus(void)
{
    GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);

    switch(status)
    {
        case GL_FRAMEBUFFER_COMPLETE_EXT:
            break;

        case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
            GST_DEBUG("GL_FRAMEBUFFER_UNSUPPORTED_EXT");
            break;

        default:
            GST_DEBUG("General FBO error");
    }
}
