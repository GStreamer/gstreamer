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

GST_BOILERPLATE (GstGLDisplay, gst_gl_display, GObject, G_TYPE_OBJECT);
static void gst_gl_display_finalize (GObject* object);

/* GL thread loop */
static gpointer gst_gl_display_thread_func (GstGLDisplay* display);
static void gst_gl_display_thread_loop (void);
static void gst_gl_display_thread_dispatch_action (GstGLDisplayMsg *msg);
static gboolean gst_gl_display_thread_check_msg_validity (GstGLDisplayMsg *msg);

/* Called in the gl thread, protected by lock and unlock */
static void gst_gl_display_thread_create_context (GstGLDisplay* display);
static void gst_gl_display_thread_destroy_context (GstGLDisplay* display);
static void gst_gl_display_thread_change_context (GstGLDisplay* display);
static void gst_gl_display_thread_set_visible_context (GstGLDisplay* display);
static void gst_gl_display_thread_resize_context (GstGLDisplay* display);
static void gst_gl_display_thread_redisplay (GstGLDisplay* display);
static void gst_gl_display_thread_gen_texture (GstGLDisplay* display);
static void gst_gl_display_thread_del_texture (GstGLDisplay* display);
static void gst_gl_display_thread_init_upload (GstGLDisplay* display);
static void gst_gl_display_thread_do_upload (GstGLDisplay* display);
static void gst_gl_display_thread_init_download (GstGLDisplay *display);
static void gst_gl_display_thread_do_download (GstGLDisplay* display);
static void gst_gl_display_thread_gen_fbo (GstGLDisplay *display);
static void gst_gl_display_thread_use_fbo (GstGLDisplay *display);
static void gst_gl_display_thread_del_fbo (GstGLDisplay *display);
static void gst_gl_display_thread_gen_shader (GstGLDisplay *display);
static void gst_gl_display_thread_del_shader (GstGLDisplay *display);

/* private methods */
void gst_gl_display_lock (GstGLDisplay* display);
void gst_gl_display_unlock (GstGLDisplay* display);
void gst_gl_display_post_message (GstGLDisplayAction action, GstGLDisplay* display);
void gst_gl_display_on_resize(gint width, gint height);
void gst_gl_display_on_draw (void);
void gst_gl_display_on_close (void);
void gst_gl_display_glgen_texture (GstGLDisplay* display, guint* pTexture);
void gst_gl_display_gldel_texture (GstGLDisplay* display, guint* pTexture);
GLhandleARB gst_gl_display_load_fragment_shader (gchar* textFProgram);
void gst_gl_display_check_framebuffer_status (void);

/* To not make gst_gl_display_thread_do_upload
 * and gst_gl_display_thread_do_download too big */
static void gst_gl_display_thread_do_upload_make (GstGLDisplay* display, GLuint* pTexture, 
                                                  GLuint* pTexture_u, GLuint* pTexture_v);
static void gst_gl_display_thread_do_upload_fill (GstGLDisplay* display, GLuint* pTexture,
                                                  GLuint* pTexture_u, GLuint* pTexture_v,
                                                  GstVideoFormat* pVideo_format);
static void gst_gl_display_thread_do_upload_draw (GstGLDisplay* display, GLuint texture, 
                                                  GLuint texture_u, GLuint texture_v,
                                                  GstVideoFormat video_format);
static void gst_gl_display_thread_do_download_draw (GstGLDisplay* display);


//------------------------------------------------------------
//-------------------- GL context management -----------------
//------------------------------------------------------------

//(key=int glutWinId) and (value=GstGLDisplay *display)
static GHashTable* gst_gl_display_map = NULL;

//all glut functions and opengl primitives are called in this thread
static GThread* gst_gl_display_glutThread = NULL;

//-timepoped by glutIdleFunc
static GAsyncQueue* gst_gl_display_messageQueue = NULL;


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
    //thread safe
    display->mutex = g_mutex_new ();

    //gl context
    display->glutWinId = -1;
    display->winId = 0;
    display->title = g_string_new ("OpenGL renderer ");
    display->win_xpos = 0;
    display->win_ypos = 0;
    display->visible = FALSE;
    display->isAlive = TRUE;
    display->texturePool = g_queue_new ();

    //conditions
    display->cond_create_context = g_cond_new ();
    display->cond_destroy_context = g_cond_new ();
    display->cond_change_context = g_cond_new ();
    display->cond_gen_texture = g_cond_new ();
    display->cond_del_texture = g_cond_new ();
    display->cond_init_upload = g_cond_new ();   
    display->cond_do_upload = g_cond_new ();
    display->cond_init_download = g_cond_new ();
    display->cond_do_download = g_cond_new ();
    display->cond_gen_fbo = g_cond_new ();
    display->cond_use_fbo = g_cond_new ();
    display->cond_del_fbo = g_cond_new (); 
    display->cond_gen_shader = g_cond_new ();
    display->cond_del_shader = g_cond_new ();

    //action redisplay
    display->redisplay_texture = 0;
    display->redisplay_texture_width = 0;
    display->redisplay_texture_height = 0;

    //action resize
    display->resize_width = 0;
    display->resize_height = 0;

    //action gen and del texture
    display->gen_texture = 0;
    display->del_texture = 0;

    //client callbacks
    display->clientReshapeCallback = NULL;
    display->clientDrawCallback = NULL;

    //upload
    display->upload_fbo = 0;
    display->upload_depth_buffer = 0;
    display->upload_texture = 0;
    display->upload_width = 0;
    display->upload_height = 0;
    display->upload_video_format = 0;
    display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_GLSL;
    display->upload_data_with = 0;
    display->upload_data_height = 0;
    display->upload_data = NULL;

    //filter gen fbo
    display->gen_fbo_width = 0;
    display->gen_fbo_height = 0;
    display->generated_fbo = 0;
    display->generated_depth_buffer = 0;

    //filter use fbo
    display->use_fbo = 0;
    display->use_depth_buffer = 0;
    display->use_fbo_texture = 0;
    display->use_fbo_width = 0;
    display->use_fbo_height = 0;
    display->use_fbo_scene_cb = NULL;
    display->use_fbo_proj_param1 = 0;
    display->use_fbo_proj_param2 = 0;
    display->use_fbo_proj_param3 = 0;
    display->use_fbo_proj_param4 = 0;
    display->use_fbo_projection = 0;
    display->use_fbo_stuff = NULL;
    display->input_texture_width = 0;
    display->input_texture_height = 0;
    display->input_texture = 0;

    //filter del fbo
    display->del_fbo = 0;
    display->del_depth_buffer = 0;

    //download
    display->download_fbo = 0;
    display->download_depth_buffer = 0;
    display->download_texture = 0;
    display->download_texture_u = 0;
    display->download_texture_v = 0;
    display->download_width = 0;
    display->download_height = 0;
    display->download_video_format = 0;
    display->download_data = NULL;
    display->ouput_texture = 0;
    display->ouput_texture_width = 0;
    display->ouput_texture_height = 0;

    //action gen and del shader
    display->gen_text_shader = NULL;
    display->gen_handle_shader = 0;
    display->del_handle_shader = 0;

    //fragement shader upload
    display->GLSLProgram_YUY2 = 0;
    display->GLSLProgram_UYVY = 0;
    display->GLSLProgram_I420_YV12 = 0;
    display->GLSLProgram_AYUV = 0;

    //fragement shader download
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
gst_gl_display_finalize (GObject* object)
{
    GstGLDisplay* display = GST_GL_DISPLAY (object);

    //request glut window destruction
    //blocking call because display must be alive
    gst_gl_display_lock (display);
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_DESTROY_CONTEXT, display);
    g_cond_wait (display->cond_destroy_context, display->mutex);
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
    if (display->cond_del_shader) {
        g_cond_free (display->cond_del_shader);
        display->cond_del_shader = NULL;
    }
    if (display->cond_gen_shader) {
        g_cond_free (display->cond_gen_shader);
        display->cond_gen_shader = NULL;
    }
    if (display->cond_del_fbo) {
        g_cond_free (display->cond_del_fbo);
        display->cond_del_fbo = NULL;
    }
    if (display->cond_use_fbo) {
        g_cond_free (display->cond_use_fbo);
        display->cond_use_fbo = NULL;
    }
    if (display->cond_gen_fbo) {
        g_cond_free (display->cond_gen_fbo);
        display->cond_gen_fbo = NULL;
    }
    if (display->cond_do_download) {
        g_cond_free (display->cond_do_download);
        display->cond_do_download = NULL;
    }
    if (display->cond_init_download) {
        g_cond_free (display->cond_init_download);
        display->cond_init_download = NULL;
    }
    if (display->cond_do_upload) {
        g_cond_free (display->cond_do_upload);
        display->cond_do_upload = NULL;
    }
    if (display->cond_init_upload) {
        g_cond_free (display->cond_init_upload);
        display->cond_init_upload = NULL;
    }
    if (display->cond_del_texture) {
        g_cond_free (display->cond_del_texture);
        display->cond_del_texture = NULL;
    }
    if (display->cond_gen_texture) {
        g_cond_free (display->cond_gen_texture);
        display->cond_gen_texture = NULL;
    }
    if (display->cond_change_context) {
        g_cond_free (display->cond_change_context);
        display->cond_change_context = NULL;
    }
    if (display->cond_destroy_context) {
        g_cond_free (display->cond_destroy_context);
        display->cond_destroy_context = NULL;
    }
    if (display->cond_create_context) {
        g_cond_free (display->cond_create_context);
        display->cond_create_context = NULL;
    }
    if (display->clientReshapeCallback)
        display->clientReshapeCallback = NULL;
    if (display->clientDrawCallback)
        display->clientDrawCallback = NULL;
    if (display->use_fbo_scene_cb)
        display->use_fbo_scene_cb = NULL;
    if (display->use_fbo_stuff)
        display->use_fbo_stuff = NULL;

    //at this step, the next condition imply that the last display has been pushed
    if (g_hash_table_size (gst_gl_display_map) == 0)
    {
        g_thread_join (gst_gl_display_glutThread);
        g_print ("gl thread joined\n");
        gst_gl_display_glutThread = NULL;
        g_async_queue_unref (gst_gl_display_messageQueue);
        g_hash_table_unref  (gst_gl_display_map);
        gst_gl_display_map = NULL;
    }
}


//------------------------------------------------------------
//----------------- BEGIN GL THREAD LOOP ---------------------
//------------------------------------------------------------


/* The gl thread handles GstGLDisplayMsg messages
 * Every OpenGL code lines are called in the gl thread */
static gpointer
gst_gl_display_thread_func (GstGLDisplay *display)
{
    static char *argv = "gst-launch-0.10";
    static gint argc = 1;

    //-display  DISPLAY
    //Specify the X server to connect to. If not specified, the value of the DISPLAY environment variable is used.
    //Should be pass through a glimagesink property
    glutInit(&argc, &argv);
    glutSetOption(GLUT_ACTION_ON_WINDOW_CLOSE, GLUT_ACTION_CONTINUE_EXECUTION);

    glutIdleFunc (gst_gl_display_thread_loop);

    gst_gl_display_lock (display);
    gst_gl_display_thread_create_context (display);
    gst_gl_display_unlock (display);

    g_print ("gl mainLoop started\n");
    glutMainLoop ();
    g_print ("gl mainLoop exited\n");

    return NULL;
}


/* Called in the gl thread */
static void
gst_gl_display_thread_loop (void)
{
    GTimeVal timeout;
    GstGLDisplayMsg *msg;

    //check for pending actions that require a glut context
    g_get_current_time (&timeout);
    g_time_val_add (&timeout, 1000000L); //timeout 1 sec
    msg = g_async_queue_timed_pop (gst_gl_display_messageQueue, &timeout);
    if (msg)
    {
        if (gst_gl_display_thread_check_msg_validity (msg))
            gst_gl_display_thread_dispatch_action (msg);
        while (g_async_queue_length (gst_gl_display_messageQueue))
        {
            msg = g_async_queue_pop (gst_gl_display_messageQueue);
            if (gst_gl_display_thread_check_msg_validity (msg))
                gst_gl_display_thread_dispatch_action (msg);
        }
    }
    else GST_DEBUG ("timeout reached in idle func\n");
}


/* Called in the gl thread loop */
static void
gst_gl_display_thread_dispatch_action (GstGLDisplayMsg* msg)
{
    gst_gl_display_lock (msg->display);
    switch (msg->action)
    {
        case GST_GL_DISPLAY_ACTION_CREATE_CONTEXT:
            gst_gl_display_thread_create_context (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_DESTROY_CONTEXT:
            gst_gl_display_thread_destroy_context (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_CHANGE_CONTEXT:
            gst_gl_display_thread_change_context (msg->display);
            break;
		case GST_GL_DISPLAY_ACTION_VISIBLE_CONTEXT:
            gst_gl_display_thread_set_visible_context (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_RESIZE_CONTEXT:
            gst_gl_display_thread_resize_context (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_REDISPLAY_CONTEXT:
            gst_gl_display_thread_redisplay (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_GEN_TEXTURE:
            gst_gl_display_thread_gen_texture (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_DEL_TEXTURE:
            gst_gl_display_thread_del_texture (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_INIT_UPLOAD:
            gst_gl_display_thread_init_upload (msg->display);
            break;      
        case GST_GL_DISPLAY_ACTION_DO_UPLOAD:
            gst_gl_display_thread_do_upload (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_INIT_DOWNLOAD:
            gst_gl_display_thread_init_download (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_DO_DOWNLOAD:
            gst_gl_display_thread_do_download (msg->display);
            break;     
        case GST_GL_DISPLAY_ACTION_GEN_FBO:
            gst_gl_display_thread_gen_fbo (msg->display);
            break;   
        case GST_GL_DISPLAY_ACTION_USE_FBO:
            gst_gl_display_thread_use_fbo (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_DEL_FBO:
            gst_gl_display_thread_del_fbo (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_GEN_SHADER:
            gst_gl_display_thread_gen_shader (msg->display);
            break;
        case GST_GL_DISPLAY_ACTION_DEL_SHADER:
            gst_gl_display_thread_del_shader (msg->display);
            break;
        default:
            g_assert_not_reached ();
    }
    gst_gl_display_unlock (msg->display);
    g_free (msg);
}


/* Called in the gl thread loop
 * Return false if the message is out of date */
static gboolean
gst_gl_display_thread_check_msg_validity (GstGLDisplayMsg *msg)
{
    gboolean valid = TRUE;

    switch (msg->action)
    {
        case GST_GL_DISPLAY_ACTION_CREATE_CONTEXT:
            //display is not in the map only when we want create one
            valid = TRUE;
            break;
        case GST_GL_DISPLAY_ACTION_DESTROY_CONTEXT:
        case GST_GL_DISPLAY_ACTION_CHANGE_CONTEXT:
		case GST_GL_DISPLAY_ACTION_VISIBLE_CONTEXT:
        case GST_GL_DISPLAY_ACTION_RESIZE_CONTEXT:
        case GST_GL_DISPLAY_ACTION_REDISPLAY_CONTEXT:
        case GST_GL_DISPLAY_ACTION_GEN_TEXTURE:
        case GST_GL_DISPLAY_ACTION_DEL_TEXTURE:
        case GST_GL_DISPLAY_ACTION_INIT_UPLOAD:        
        case GST_GL_DISPLAY_ACTION_DO_UPLOAD:
        case GST_GL_DISPLAY_ACTION_INIT_DOWNLOAD:
        case GST_GL_DISPLAY_ACTION_DO_DOWNLOAD:      
        case GST_GL_DISPLAY_ACTION_GEN_FBO:
        case GST_GL_DISPLAY_ACTION_USE_FBO:
        case GST_GL_DISPLAY_ACTION_DEL_FBO:       
        case GST_GL_DISPLAY_ACTION_GEN_SHADER:
        case GST_GL_DISPLAY_ACTION_DEL_SHADER:
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
//------------------ END GL THREAD LOOP ----------------------
//------------------------------------------------------------


//------------------------------------------------------------
//------------------ BEGIN GL THREAD ACTIONS -----------------
//------------------------------------------------------------

//The following functions are thread safe because
//called by the "gst_gl_display_thread_dispatch_action"
//in a lock/unlock scope.

/* Called in the gl thread */
static void
gst_gl_display_thread_create_context (GstGLDisplay *display)
{
    gint glutWinId = 0;
    gchar buffer[5];
    GLenum err = 0;

    //prepare opengl context
    glutInitDisplayMode(GLUT_RGBA | GLUT_DOUBLE | GLUT_DEPTH);
    glutInitWindowPosition(display->win_xpos, display->win_ypos);
    glutInitWindowSize(display->upload_width, display->upload_height);

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
    {
        GST_DEBUG ("Error: %s", glewGetErrorString(err));
        display->isAlive = FALSE;
    }
    else
    {
        //OpenGL > 2.1.0 and Glew > 1.5.0
        GString* opengl_version = g_string_truncate (g_string_new ((gchar*) glGetString (GL_VERSION)), 3);
        gint opengl_version_major = 0;
        gint opengl_version_minor = 0;
        GString* glew_version = g_string_truncate (g_string_new ((gchar*) glewGetString (GLEW_VERSION)), 3);
        gint glew_version_major = 0;
        gint glew_version_minor = 0;

        sscanf(opengl_version->str, "%d.%d", &opengl_version_major, &opengl_version_minor);
        sscanf(glew_version->str, "%d.%d", &glew_version_major, &glew_version_minor);

        g_print ("GL_VERSION: %s\n", glGetString (GL_VERSION));
        g_print ("GLEW_VERSION: %s\n", glewGetString (GLEW_VERSION));

        g_print ("GL_VENDOR: %s\n", glGetString (GL_VENDOR));
        g_print ("GL_RENDERER: %s\n", glGetString (GL_RENDERER));

        g_string_free (opengl_version, TRUE);
        g_string_free (glew_version, TRUE);

        if ((opengl_version_major < 1 && opengl_version_minor < 4) ||
            (glew_version_major   < 1 && glew_version_minor   < 4) )
        {
            //turn off the pipeline, the old drivers are not yet supported
            g_print ("Required OpenGL >= 1.4.0 and Glew >= 1.4.0\n");
            display->isAlive = FALSE;
        }
    }

    //setup callbacks
    glutReshapeFunc (gst_gl_display_on_resize);
    glutDisplayFunc (gst_gl_display_on_draw);
    glutCloseFunc (gst_gl_display_on_close);

    //insert glut context to the map
    display->glutWinId = glutWinId;
    g_hash_table_insert (gst_gl_display_map, GUINT_TO_POINTER (glutWinId), display);

    //check glut id validity
    g_assert (glutGetWindow() == glutWinId);
    GST_DEBUG ("Context %d initialized", display->glutWinId);

    //release display constructor
    g_cond_signal (display->cond_create_context);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_destroy_context (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);
    glutReshapeFunc (NULL);
    glutDestroyWindow (display->glutWinId);

    //colorspace_conversion specific 
    switch (display->colorspace_conversion)
    {
        case GST_GL_DISPLAY_CONVERSION_MESA:
        case GST_GL_DISPLAY_CONVERSION_MATRIX:
            break;
        case GST_GL_DISPLAY_CONVERSION_GLSL:
            {
                glUseProgramObjectARB (0);
                glDeleteObjectARB (display->GLSLProgram_YUY2);
                glDeleteObjectARB (display->GLSLProgram_UYVY);
                glDeleteObjectARB (display->GLSLProgram_I420_YV12);
                glDeleteObjectARB (display->GLSLProgram_AYUV);
                glDeleteObjectARB (display->GLSLProgram_to_YUY2);
                glDeleteObjectARB (display->GLSLProgram_to_UYVY);
                glDeleteObjectARB (display->GLSLProgram_to_I420_YV12);
                glDeleteObjectARB (display->GLSLProgram_to_AYUV);
            }
            break;
        default:
            g_assert_not_reached ();
    }

    glDeleteFramebuffersEXT (1, &display->upload_fbo);
    glDeleteRenderbuffersEXT(1, &display->upload_depth_buffer);

    glDeleteFramebuffersEXT (1, &display->download_fbo);
    glDeleteRenderbuffersEXT(1, &display->download_depth_buffer);
    glDeleteTextures (1, &display->download_texture);
    glDeleteTextures (1, &display->download_texture_u);
    glDeleteTextures (1, &display->download_texture_v);

    //clean up the texture pool
    while (g_queue_get_length (display->texturePool))
    {
	    GstGLDisplayTex* tex = g_queue_pop_head (display->texturePool);
	    glDeleteTextures (1, &tex->texture);
        g_free (tex);
    }

    g_hash_table_remove (gst_gl_display_map, GINT_TO_POINTER (display->glutWinId));
    g_print ("Context %d destroyed\n", display->glutWinId);

    //if the map is empty, leaveMainloop and join the thread
    if (g_hash_table_size (gst_gl_display_map) == 0)
        glutLeaveMainLoop ();

    //release display destructor
    g_cond_signal (display->cond_destroy_context);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_change_context (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);
    glutChangeWindow (display->winId);
    g_cond_signal (display->cond_change_context);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_set_visible_context (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);
	if (display->visible)
        glutShowWindow ();
    else
        glutHideWindow ();
}


/* Called by the idle function */
static void
gst_gl_display_thread_resize_context (GstGLDisplay* display)
{
    glutSetWindow (display->glutWinId);
    glutReshapeWindow (display->resize_width, display->resize_height);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_redisplay (GstGLDisplay * display)
{
    glutSetWindow (display->glutWinId);
    glutPostRedisplay ();
}


/* Called in the gl thread */
static void
gst_gl_display_thread_gen_texture (GstGLDisplay * display)
{
    glutSetWindow (display->glutWinId);
    //setup a texture to render to (this one will be in a gl buffer)
    gst_gl_display_glgen_texture (display, &display->gen_texture);
    g_cond_signal (display->cond_gen_texture);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_del_texture (GstGLDisplay* display)
{
    glutSetWindow (display->glutWinId);
    gst_gl_display_gldel_texture (display, &display->del_texture);
    g_cond_signal (display->cond_del_texture);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_init_upload (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);
    
    //Frame buffer object is a requirement for every cases
    if (GLEW_EXT_framebuffer_object)
    {
        //a texture must be attached to the FBO
        guint fake_texture = 0;

        g_print ("Context %d, EXT_framebuffer_object supported: yes\n", display->glutWinId);

        //-- init intput frame buffer object (video -> GL)

        //setup FBO
        glGenFramebuffersEXT (1, &display->upload_fbo);
        glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->upload_fbo);

        //setup the render buffer for depth
        glGenRenderbuffersEXT(1, &display->upload_depth_buffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->upload_depth_buffer);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
            display->upload_width, display->upload_height);

        //a fake texture is attached to the upload FBO (cannot init without it)
        glGenTextures (1, &fake_texture);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, fake_texture);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
            display->upload_width, display->upload_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        //attach the texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

        //attach the depth render buffer to the FBO
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
            GL_RENDERBUFFER_EXT, display->upload_depth_buffer);

        gst_gl_display_check_framebuffer_status();

        g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
            GL_FRAMEBUFFER_COMPLETE_EXT);

        //unbind the FBO
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

        glDeleteTextures (1, &fake_texture);
    }
    else
    {
        //turn off the pipeline because Frame buffer object is a requirement
        g_print ("Context %d, EXT_framebuffer_object supported: no\n", display->glutWinId);
        display->isAlive = FALSE;
    }

    switch (display->upload_video_format)
    {
        case GST_VIDEO_FORMAT_RGBx:
        case GST_VIDEO_FORMAT_BGRx:
        case GST_VIDEO_FORMAT_xRGB:
        case GST_VIDEO_FORMAT_xBGR:
        case GST_VIDEO_FORMAT_RGBA:
        case GST_VIDEO_FORMAT_BGRA:
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ABGR:
        case GST_VIDEO_FORMAT_RGB:
        case GST_VIDEO_FORMAT_BGR:
            //color space conversion is not needed
            break;
        case GST_VIDEO_FORMAT_YUY2:
        case GST_VIDEO_FORMAT_UYVY:
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
        case GST_VIDEO_FORMAT_AYUV:
            //color space conversion is needed
            {
	            //check if fragment shader is available, then load them
	            if (GLEW_ARB_fragment_shader)
	            {
                    g_print ("Context %d, ARB_fragment_shader supported: yes\n", display->glutWinId);

                    display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_GLSL;

                    switch (display->upload_video_format)
                    {
                        case GST_VIDEO_FORMAT_YUY2:
                            {
                                gchar program[2048];
                                sprintf (program, display->textFProgram_YUY2_UYVY, 'r', 'g', 'a');
                                display->GLSLProgram_YUY2 = gst_gl_display_load_fragment_shader (program);
                            }
                            break;
                        case GST_VIDEO_FORMAT_UYVY:
                            {
                                gchar program[2048];
                                sprintf (program, display->textFProgram_YUY2_UYVY, 'a', 'b', 'r');
                                display->GLSLProgram_UYVY = gst_gl_display_load_fragment_shader (program);
                            }
                            break;
		                case GST_VIDEO_FORMAT_I420:
                        case GST_VIDEO_FORMAT_YV12:
                            display->GLSLProgram_I420_YV12 = gst_gl_display_load_fragment_shader (display->textFProgram_I420_YV12);
                            break;
                        case GST_VIDEO_FORMAT_AYUV:
                            display->GLSLProgram_AYUV = gst_gl_display_load_fragment_shader (display->textFProgram_AYUV);
                            break;
                        default:
                            g_assert_not_reached ();
                    }     
	            }
                //check if YCBCR MESA is available
	            else if (GLEW_MESA_ycbcr_texture)
	            {
                    //GLSL and Color Matrix are not available on your drivers, switch to YCBCR MESA
                    g_print ("Context %d, ARB_fragment_shader supported: no\n", display->glutWinId);
                    g_print ("Context %d, GLEW_ARB_imaging supported: no\n", display->glutWinId);
                    g_print ("Context %d, GLEW_MESA_ycbcr_texture supported: yes\n", display->glutWinId);
                    
                    display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MESA;

                    switch (display->upload_video_format)
                    {
                        case GST_VIDEO_FORMAT_YUY2:
                        case GST_VIDEO_FORMAT_UYVY:
                            break;
                        case GST_VIDEO_FORMAT_I420:
                        case GST_VIDEO_FORMAT_YV12:
                        case GST_VIDEO_FORMAT_AYUV:
                            //turn off the pipeline because
                            //MESA only support YUY2 and UYVY
                            display->isAlive = FALSE;
                            break;
                        default:
                            g_assert_not_reached ();
                    }     
                }
                //check if color matrix is available
	            else if (GLEW_ARB_imaging)
	            {
                    //GLSL is not available on your drivers, switch to Color Matrix
                    g_print ("Context %d, ARB_fragment_shader supported: no\n", display->glutWinId);
                    g_print ("Context %d, GLEW_ARB_imaging supported: yes\n", display->glutWinId);
                    
                    display->colorspace_conversion = GST_GL_DISPLAY_CONVERSION_MATRIX;
                }
                else
                {
                    g_print ("Context %d, ARB_fragment_shader supported: no\n", display->glutWinId);
                    g_print ("Context %d, GLEW_ARB_imaging supported: no\n", display->glutWinId);
                    g_print ("Context %d, GLEW_MESA_ycbcr_texture supported: no\n", display->glutWinId);
                    
                    //turn off the pipeline because colorspace conversion is not possible
                    display->isAlive = FALSE;
                }
            }
            break;
        default:
            g_assert_not_reached ();
    }

    g_cond_signal (display->cond_init_upload);
}


/* Called by the idle function */
static void
gst_gl_display_thread_do_upload (GstGLDisplay * display)
{
    GLuint texture = 0;
    GLuint texture_u = 0;
    GLuint texture_v = 0;
    GstVideoFormat video_format = 0;

    glutSetWindow (display->glutWinId);

    gst_gl_display_thread_do_upload_make (display, &texture, &texture_u, &texture_v);
    gst_gl_display_thread_do_upload_fill (display, &texture, &texture_u, &texture_v,
        &video_format);
    gst_gl_display_thread_do_upload_draw (display, texture, texture_u, texture_v,
        video_format);

    gst_gl_display_gldel_texture (display, &texture);
    if (texture_u)
        gst_gl_display_gldel_texture (display, &texture_u);
    if (texture_v)
        gst_gl_display_gldel_texture (display, &texture_v);
    g_cond_signal (display->cond_do_upload);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_init_download (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);

    if (GLEW_EXT_framebuffer_object)
    {
        GST_DEBUG ("Context %d, EXT_framebuffer_object supported: yes", display->glutWinId);

        //-- init output frame buffer object (GL -> video)

        //setup FBO
        glGenFramebuffersEXT (1, &display->download_fbo);
        glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->download_fbo);

        //setup the render buffer for depth
        glGenRenderbuffersEXT(1, &display->download_depth_buffer);
        glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->download_depth_buffer);
        glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
            display->download_width, display->download_height);

        //setup a first texture to render to
        glGenTextures (1, &display->download_texture);
        glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->download_texture);
        glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
            display->download_width, display->download_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        //attach the first texture to the FBO to renderer to
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
            GL_TEXTURE_RECTANGLE_ARB, display->download_texture, 0);

        switch (display->download_video_format)
        {
            case GST_VIDEO_FORMAT_RGBx:
            case GST_VIDEO_FORMAT_BGRx:
            case GST_VIDEO_FORMAT_xRGB:
            case GST_VIDEO_FORMAT_xBGR:
            case GST_VIDEO_FORMAT_RGBA:
            case GST_VIDEO_FORMAT_BGRA:
            case GST_VIDEO_FORMAT_ARGB:
            case GST_VIDEO_FORMAT_ABGR:
            case GST_VIDEO_FORMAT_RGB:
            case GST_VIDEO_FORMAT_BGR:
            case GST_VIDEO_FORMAT_YUY2:
            case GST_VIDEO_FORMAT_UYVY:           
            case GST_VIDEO_FORMAT_AYUV:
                //only one attached texture is needed
                break;

            case GST_VIDEO_FORMAT_I420:
            case GST_VIDEO_FORMAT_YV12:
                //setup a second texture to render to
                glGenTextures (1, &display->download_texture_u);
                glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->download_texture_u);
                glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                    display->download_width, display->download_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                //attach the second texture to the FBO to renderer to
                glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT1_EXT,
                    GL_TEXTURE_RECTANGLE_ARB, display->download_texture_u, 0);

                //setup a third texture to render to
                glGenTextures (1, &display->download_texture_v);
                glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->download_texture_v);
                glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                    display->download_width, display->download_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                //attach the third texture to the FBO to renderer to
                glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT2_EXT,
                    GL_TEXTURE_RECTANGLE_ARB, display->download_texture_v, 0);

                display->multipleRT[0] = GL_COLOR_ATTACHMENT0_EXT;
                display->multipleRT[1] = GL_COLOR_ATTACHMENT1_EXT;
                display->multipleRT[2] = GL_COLOR_ATTACHMENT2_EXT;
                break;
            default:
                g_assert_not_reached ();
        }

        //attach the depth render buffer to the FBO
        glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
            GL_RENDERBUFFER_EXT, display->download_depth_buffer);

        gst_gl_display_check_framebuffer_status();

        g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
            GL_FRAMEBUFFER_COMPLETE_EXT);

        //unbind the FBO
        glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);    
    }
    else
    {
        //turn off the pipeline because Frame buffer object is a requirement
        g_print ("Context %d, EXT_framebuffer_object supported: no\n", display->glutWinId);
        display->isAlive = FALSE;
    }

    switch (display->download_video_format)
    {
        case GST_VIDEO_FORMAT_RGBx:
        case GST_VIDEO_FORMAT_BGRx:
        case GST_VIDEO_FORMAT_xRGB:
        case GST_VIDEO_FORMAT_xBGR:
        case GST_VIDEO_FORMAT_RGBA:
        case GST_VIDEO_FORMAT_BGRA:
        case GST_VIDEO_FORMAT_ARGB:
        case GST_VIDEO_FORMAT_ABGR:
        case GST_VIDEO_FORMAT_RGB:
        case GST_VIDEO_FORMAT_BGR:
            //color space conversion is not needed
            break;
        case GST_VIDEO_FORMAT_YUY2:
        case GST_VIDEO_FORMAT_UYVY:
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
        case GST_VIDEO_FORMAT_AYUV:
            //color space conversion is needed
            {
                //check if fragment shader is available, then load them
                //GLSL is a requirement for donwload
                if (GLEW_ARB_fragment_shader)
	            {
                    switch (display->download_video_format)
                    {
                        case GST_VIDEO_FORMAT_YUY2:
                            {
                                gchar program[2048];
                                sprintf (program, display->textFProgram_to_YUY2_UYVY, "y2,u,y1,v");
                                display->GLSLProgram_to_YUY2 = gst_gl_display_load_fragment_shader (program);
                            }
                            break;
                        case GST_VIDEO_FORMAT_UYVY:
                            {
                                gchar program[2048];
                                sprintf (program, display->textFProgram_to_YUY2_UYVY, "v,y1,u,y2");
                                display->GLSLProgram_to_UYVY = gst_gl_display_load_fragment_shader (program);
                            }
                            break;
		                case GST_VIDEO_FORMAT_I420:
                        case GST_VIDEO_FORMAT_YV12:
                            display->GLSLProgram_to_I420_YV12 = gst_gl_display_load_fragment_shader (display->textFProgram_to_I420_YV12);
                            break;
                        case GST_VIDEO_FORMAT_AYUV:
                            display->GLSLProgram_to_AYUV = gst_gl_display_load_fragment_shader (display->textFProgram_to_AYUV);
                            break;
                        default:
                            g_assert_not_reached ();
                    }                                                                
                }
                else
                {
                    //turn off the pipeline because colorspace conversion is not possible
                    GST_DEBUG ("Context %d, ARB_fragment_shader supported: no", display->glutWinId); 
                    display->isAlive = FALSE;
                }
            }
            break;
        default:
            g_assert_not_reached ();
    }

    g_cond_signal (display->cond_init_download);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_do_download (GstGLDisplay * display)
{
    glutSetWindow (display->glutWinId);
    gst_gl_display_thread_do_download_draw (display);
    g_cond_signal (display->cond_do_download);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_gen_fbo (GstGLDisplay *display)
{
    //a texture must be attached to the FBO
    guint fake_texture = 0;

    glutSetWindow (display->glutWinId);

    //-- generate frame buffer object

    //setup FBO
    glGenFramebuffersEXT (1, &display->generated_fbo);
    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, display->generated_fbo);

    //setup the render buffer for depth
    glGenRenderbuffersEXT(1, &display->generated_depth_buffer);
    glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, display->generated_depth_buffer);
    glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_COMPONENT,
        display->gen_fbo_width, display->gen_fbo_height);

    //setup a texture to render to
    glGenTextures (1, &fake_texture);
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, fake_texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
        display->gen_fbo_width, display->gen_fbo_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

    //attach the texture to the FBO to renderer to
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB, fake_texture, 0);

    //attach the depth render buffer to the FBO
    glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT,
        GL_RENDERBUFFER_EXT, display->generated_depth_buffer);

    g_assert (glCheckFramebufferStatusEXT (GL_FRAMEBUFFER_EXT) ==
        GL_FRAMEBUFFER_COMPLETE_EXT);

    //unbind the FBO
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    glDeleteTextures (1, &fake_texture);

    g_cond_signal (display->cond_gen_fbo);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_use_fbo (GstGLDisplay *display)
{
    glutSetWindow (display->glutWinId);

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->use_fbo);

    //setup a texture to render to
    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
        display->use_fbo_width, display->use_fbo_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    //attach the texture to the FBO to renderer to
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB, display->use_fbo_texture, 0);

    glPushAttrib(GL_VIEWPORT_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();

    switch (display->use_fbo_projection)
    {
        case GST_GL_DISPLAY_PROJECTION_ORTHO2D:
            gluOrtho2D(display->use_fbo_proj_param1, display->use_fbo_proj_param2, 
                display->use_fbo_proj_param3, display->use_fbo_proj_param4);
            break;
        case GST_GL_DISPLAY_PROJECTION_PERSPECIVE:
            gluPerspective(display->use_fbo_proj_param1, display->use_fbo_proj_param2, 
                display->use_fbo_proj_param3, display->use_fbo_proj_param4);
            break;
        default:
            g_assert_not_reached ();
    }

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, display->use_fbo_width, display->use_fbo_height);

    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    //the opengl scene
    display->use_fbo_scene_cb (display->input_texture_width, display->input_texture_height, 
        display->input_texture, display->use_fbo_stuff);

    glDrawBuffer(GL_NONE);

    //in case of the developer forgot the de-init use of GLSL in the scene code
    if (display->colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
        glUseProgramObjectARB (0);

    glDisable(GL_TEXTURE_RECTANGLE_ARB);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    g_cond_signal (display->cond_use_fbo);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_del_fbo (GstGLDisplay* display)
{
    glutSetWindow (display->glutWinId);

    glDeleteFramebuffersEXT (1, &display->del_fbo);
    glDeleteRenderbuffersEXT(1, &display->del_depth_buffer);
    display->del_fbo = 0;
    display->del_depth_buffer = 0;

    g_cond_signal (display->cond_del_fbo);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_gen_shader (GstGLDisplay* display)
{
    glutSetWindow (display->glutWinId);
    if (GLEW_ARB_fragment_shader)                
        display->gen_handle_shader = 
            gst_gl_display_load_fragment_shader (display->gen_text_shader);
    else
    {
        g_print ("One of the filter required ARB_fragment_shader\n");
        display->isAlive = FALSE;
    }
    g_cond_signal (display->cond_gen_shader);
}


/* Called in the gl thread */
static void
gst_gl_display_thread_del_shader (GstGLDisplay* display)
{
    glutSetWindow (display->glutWinId);
    glDeleteObjectARB (display->del_handle_shader);
    g_cond_signal (display->cond_del_shader);
}


//------------------------------------------------------------
//------------------ BEGIN GL THREAD ACTIONS -----------------
//------------------------------------------------------------


//------------------------------------------------------------
//---------------------- BEGIN PRIVATE -----------------------
//------------------------------------------------------------


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


/* Post a message that will be handled by the gl thread 
 * Must be preceded by gst_gl_display_lock 
 * and followed by gst_gl_display_unlock
 * Called in the public functions */
void
gst_gl_display_post_message (GstGLDisplayAction action, GstGLDisplay* display)
{
    GstGLDisplayMsg* msg = g_new0 (GstGLDisplayMsg, 1);
    msg->action = action;
    msg->glutWinId = display->glutWinId;
    msg->display = display;
    g_async_queue_push (gst_gl_display_messageQueue, msg);
}


/* glutReshapeFunc callback */
void
gst_gl_display_on_resize (gint width, gint height)
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
void gst_gl_display_on_draw(void)
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
    if (!display->redisplay_texture)
	{
		gst_gl_display_unlock (display);
		return;
	}

    //opengl scene

    //make sure that the environnement is clean
    if (display->colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
        glUseProgramObjectARB (0);
    glDisable (GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, 0);

    //check if a client draw callback is registered
    if (display->clientDrawCallback)
    {
		gboolean doRedisplay =
            display->clientDrawCallback(display->redisplay_texture,
				display->redisplay_texture_width, display->redisplay_texture_height);

        glFlush();
        glutSwapBuffers();

        if (doRedisplay)
            gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_REDISPLAY_CONTEXT, display);
    }
    //default opengl scene
    else
    {

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode (GL_PROJECTION);
        glLoadIdentity ();

	    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->redisplay_texture);
        glEnable (GL_TEXTURE_RECTANGLE_ARB);

        glBegin (GL_QUADS);
            glTexCoord2i (display->redisplay_texture_width, 0);
            glVertex2f (1.0f, 1.0f);
            glTexCoord2i (0, 0);
            glVertex2f (-1.0f, 1.0f);
            glTexCoord2i (0, display->redisplay_texture_height);
            glVertex2f (-1.0f, -1.0f);
            glTexCoord2i (display->redisplay_texture_width, display->redisplay_texture_height);
            glVertex2f (1.0f, -1.0f);
        glEnd ();

        glDisable(GL_TEXTURE_RECTANGLE_ARB);

		glFlush();
        glutSwapBuffers();

	}//end default opengl scene

    gst_gl_display_unlock (display);
}


/* glutCloseFunc callback */
void gst_gl_display_on_close (void)
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


/* Generate a texture if no one is available in the pool
 * Called in the gl thread */
void
gst_gl_display_glgen_texture (GstGLDisplay* display, guint* pTexture)
{
    //check if there is a texture available in the pool
    GstGLDisplayTex* tex = g_queue_pop_head (display->texturePool);
    if (tex)
    {
        *pTexture = tex->texture;
        g_free (tex);
    }
    //otherwise one more texture is generated
    //note that this new texture is added in the pool
    //only after being used
    else
        glGenTextures (1, pTexture);
}


/* Delete a texture, actually the texture is just added to the pool 
 * Called in the gl thread */
void
gst_gl_display_gldel_texture (GstGLDisplay* display, guint* pTexture)
{
    //Each existing texture is destroyed only when the pool is destroyed
    //The pool of textures is deleted in the GstGLDisplay destructor

    //contruct a texture pool element
    GstGLDisplayTex* tex = g_new0 (GstGLDisplayTex, 1);
    tex->texture = *pTexture;
    *pTexture = 0;

    //add tex to the pool, it makes texture allocation reusable
    g_queue_push_tail (display->texturePool, tex);
}


/* called in the gl thread */
GLhandleARB
gst_gl_display_load_fragment_shader (gchar* textFProgram)
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


/* called in the gl thread */
void
gst_gl_display_check_framebuffer_status(void)
{
    GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);

    switch(status)
    {
        case GL_FRAMEBUFFER_COMPLETE_EXT:
            break;

        case GL_FRAMEBUFFER_UNSUPPORTED_EXT:
            g_print ("GL_FRAMEBUFFER_UNSUPPORTED_EXT\n");
            break;

        default:
            g_print ("General FBO error\n");
    }
}


//------------------------------------------------------------
//---------------------  END PRIVATE -------------------------
//------------------------------------------------------------


//------------------------------------------------------------
//---------------------- BEGIN PUBLIC ------------------------
//------------------------------------------------------------


/* Called by the first gl element of a video/x-raw-gl flow */
GstGLDisplay*
gst_gl_display_new (void)
{
    return g_object_new (GST_TYPE_GL_DISPLAY, NULL);
}


/* Create an opengl context (one context for one GstGLDisplay) 
 * Called by the first gl element of a video/x-raw-gl flow */
void
gst_gl_display_create_context (GstGLDisplay *display,
                               GLint x, GLint y,
                               GLint width, GLint height,
                               gulong winId,
                               gboolean visible)
{
    gst_gl_display_lock (display);

    display->winId = winId;
    display->win_xpos = x;
    display->win_ypos = y;
    display->upload_width = width;
    display->upload_height = height;
    display->visible = visible;

    //if no glut_thread exists, create it with a window associated to the display
    if (!gst_gl_display_map)
    {
        gst_gl_display_messageQueue = g_async_queue_new ();
		gst_gl_display_map = g_hash_table_new (g_direct_hash, g_direct_equal);
        gst_gl_display_glutThread = g_thread_create (
            (GThreadFunc) gst_gl_display_thread_func, display, TRUE, NULL);
        g_cond_wait (display->cond_create_context, display->mutex);
    }
    //request glut window creation
    else
    {
        //blocking call because glut context must be alive
        gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_CREATE_CONTEXT, display);
        g_cond_wait (display->cond_create_context, display->mutex);
    }
    gst_gl_display_unlock (display);
}


/* Called by the glimagesink element */
void
gst_gl_display_set_visible_context (GstGLDisplay* display, gboolean visible)
{
    gst_gl_display_lock (display);
    if (display->visible != visible)
    {
        display->visible = visible;
        gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_VISIBLE_CONTEXT, display);
    }
    gst_gl_display_unlock (display);
}


/* Called by the glimagesink element */
void
gst_gl_display_resize_context (GstGLDisplay* display, gint width, gint height)
{
    gst_gl_display_lock (display);
    display->resize_width = width;
    display->resize_height = height;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_RESIZE_CONTEXT, display);
    gst_gl_display_unlock (display);
}


/* Called by the glimagesink element */
gboolean
gst_gl_display_redisplay (GstGLDisplay* display, GLuint texture, gint width , gint height)
{
    gboolean isAlive = TRUE;

    gst_gl_display_lock (display);
    isAlive = display->isAlive;
    if (texture)
    {
        display->redisplay_texture = texture;
        display->redisplay_texture_width = width;
        display->redisplay_texture_height = height;
    }
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_REDISPLAY_CONTEXT, display);
    gst_gl_display_unlock (display);

    return isAlive;
}


/* Called by gst_gl_buffer_new */
void
gst_gl_display_gen_texture (GstGLDisplay* display, guint* pTexture)
{
    gst_gl_display_lock (display);
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_GEN_TEXTURE, display);
    g_cond_wait (display->cond_gen_texture, display->mutex);
    *pTexture = display->gen_texture;
    gst_gl_display_unlock (display);
}


/* Called by gst_gl_buffer_finalize */
void
gst_gl_display_del_texture (GstGLDisplay* display, guint texture)
{
    gst_gl_display_lock (display);
    display->del_texture = texture;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_DEL_TEXTURE, display);
    g_cond_wait (display->cond_del_texture, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by the first gl element of a video/x-raw-gl flow */
void
gst_gl_display_init_upload (GstGLDisplay* display, GstVideoFormat video_format,
                            guint gl_width, guint gl_height)
{
    gst_gl_display_lock (display);
    display->upload_video_format = video_format;
    display->upload_width = gl_width;
    display->upload_height = gl_height;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_INIT_UPLOAD, display);
    g_cond_wait (display->cond_init_upload, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by the first gl element of a video/x-raw-gl flow */
void
gst_gl_display_do_upload (GstGLDisplay* display, guint texture,
                          gint data_width, gint data_height, 
                          gpointer data)
{
    gst_gl_display_lock (display);
    display->upload_texture = texture;
    display->upload_data_with = data_width;
    display->upload_data_height = data_height;
    display->upload_data = data;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_DO_UPLOAD, display);
    g_cond_wait (display->cond_do_upload, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by the gldownload and glcolorscale element */
void
gst_gl_display_init_download (GstGLDisplay* display, GstVideoFormat video_format,
                              gint width, gint height)
{
    gst_gl_display_lock (display);
    display->download_video_format = video_format;
    display->download_width = width;
    display->download_height = height;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_INIT_DOWNLOAD, display);
    g_cond_wait (display->cond_init_download, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by the gldownload and glcolorscale element */
void
gst_gl_display_do_download (GstGLDisplay* display, GLuint texture,
                            gint width, gint height,
                            gpointer data)
{
    gst_gl_display_lock (display);
    //data size is aocciated to the glcontext size
    display->download_data = data;
    display->ouput_texture = texture;
    display->ouput_texture_width = width;
    display->ouput_texture_height = height;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_DO_DOWNLOAD, display);
    g_cond_wait (display->cond_do_download, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by gltestsrc and glfilter */
void
gst_gl_display_gen_fbo (GstGLDisplay* display, gint width, gint height,
                        guint* fbo, guint* depthbuffer)
{
    gst_gl_display_lock (display);
    display->gen_fbo_width = width;
    display->gen_fbo_height = height;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_GEN_FBO, display);
    g_cond_wait (display->cond_gen_fbo, display->mutex);
    *fbo = display->generated_fbo;
    *depthbuffer = display->generated_depth_buffer;
    gst_gl_display_unlock (display);
}


/* Called by glfilter */
void
gst_gl_display_use_fbo (GstGLDisplay* display, gint texture_fbo_width, gint texture_fbo_height,
                        guint fbo, guint depth_buffer, guint texture_fbo, GLCB cb,
                        gint input_texture_width, gint input_texture_height, guint input_texture,
                        gdouble proj_param1, gdouble proj_param2,
                        gdouble proj_param3, gdouble proj_param4,
                        GstGLDisplayProjection projection, gpointer* stuff)
{
    gst_gl_display_lock (display);
    display->use_fbo = fbo;
    display->use_depth_buffer = depth_buffer;
    display->use_fbo_texture = texture_fbo;
    display->use_fbo_width = texture_fbo_width;
    display->use_fbo_height = texture_fbo_height;
    display->use_fbo_scene_cb = cb;
    display->use_fbo_proj_param1 = proj_param1;
    display->use_fbo_proj_param2 = proj_param2;
    display->use_fbo_proj_param3 = proj_param3;
    display->use_fbo_proj_param4 = proj_param4;
    display->use_fbo_projection = projection;
    display->use_fbo_stuff = stuff;
    display->input_texture_width = input_texture_width;
    display->input_texture_height = input_texture_height;
    display->input_texture = input_texture;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_USE_FBO, display);
    g_cond_wait (display->cond_use_fbo, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by gltestsrc and glfilter */
void
gst_gl_display_del_fbo (GstGLDisplay* display, guint fbo,
                        guint depth_buffer)
{
    gst_gl_display_lock (display);
    display->del_fbo = fbo;
    display->del_depth_buffer = depth_buffer;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_DEL_FBO, display);
    g_cond_wait (display->cond_del_fbo, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by glfilter */
void
gst_gl_display_gen_shader (GstGLDisplay* display, gchar* textShader, GLhandleARB* handleShader)
{
    gst_gl_display_lock (display);
    display->gen_text_shader = textShader;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_GEN_SHADER, display);
    g_cond_wait (display->cond_gen_shader, display->mutex);
    *handleShader = display->gen_handle_shader;
    gst_gl_display_unlock (display);
}


/* Called by glfilter */
void
gst_gl_display_del_shader (GstGLDisplay* display, GLhandleARB shader)
{
    gst_gl_display_lock (display);
    display->del_handle_shader = shader;
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_DEL_SHADER, display);
    g_cond_wait (display->cond_del_shader, display->mutex);
    gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_window_id (GstGLDisplay* display, gulong winId)
{
    static gint y_pos = 0;

    gst_gl_display_lock (display);
    //display->winId = winId;
    //gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_CHANGE_CONTEXT, display);
    gst_gl_display_post_message (GST_GL_DISPLAY_ACTION_DESTROY_CONTEXT, display);
    //g_cond_wait (display->cond_change_context, display->mutex);
    g_cond_wait (display->cond_destroy_context, display->mutex);
    gst_gl_display_unlock (display);

    if (g_hash_table_size (gst_gl_display_map) == 0)
    {
        g_thread_join (gst_gl_display_glutThread);
        g_print ("gl thread joined when setting winId\n");
        gst_gl_display_glutThread = NULL;
        g_async_queue_unref (gst_gl_display_messageQueue);
        g_hash_table_unref  (gst_gl_display_map);
        gst_gl_display_map = NULL;
    }

    //init opengl context
    gst_gl_display_create_context (display,
        50, y_pos++ * (display->upload_height+50) + 50,
        display->upload_width, display->upload_height,
        winId,
        TRUE);

    //init colorspace conversion if needed
    gst_gl_display_init_upload (display, display->upload_video_format, 
        display->upload_width, display->upload_height);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_reshape_callback (GstGLDisplay * display, CRCB cb)
{
    gst_gl_display_lock (display);
    display->clientReshapeCallback = cb;
    gst_gl_display_unlock (display);
}


/* Called by the glimagesink */
void
gst_gl_display_set_client_draw_callback (GstGLDisplay * display, CDCB cb)
{
    gst_gl_display_lock (display);
    display->clientDrawCallback = cb;
    gst_gl_display_unlock (display);
}


//------------------------------------------------------------
//------------------------ END PUBLIC ------------------------
//------------------------------------------------------------


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void gst_gl_display_thread_do_upload_make (GstGLDisplay* display, GLuint* pTexture,
                                           GLuint* pTexture_u, GLuint* pTexture_v)
{
    gint width = display->upload_data_with;
    gint height = display->upload_data_height;

    gst_gl_display_glgen_texture (display, pTexture);

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture);
    switch (display->upload_video_format)
    {
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
			    width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		    break;

	    case GST_VIDEO_FORMAT_RGB:
	    case GST_VIDEO_FORMAT_BGR:
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGB,
			    width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
		    break;

	    case GST_VIDEO_FORMAT_YUY2:
            switch (display->colorspace_conversion)
            {
	            case GST_GL_DISPLAY_CONVERSION_GLSL:
                case GST_GL_DISPLAY_CONVERSION_MATRIX:
		            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
                        width, height,
                        0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);

                    gst_gl_display_glgen_texture (display, pTexture_u);

                    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_u);
                    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                        width, height,
                        0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		            break;
	            case GST_GL_DISPLAY_CONVERSION_MESA:
		            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA,
                        width, height,
                        0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
		            break;
	            default:
		            g_assert_not_reached ();
            }
		    break;
	    case GST_VIDEO_FORMAT_UYVY:
            switch (display->colorspace_conversion)
            {
	            case GST_GL_DISPLAY_CONVERSION_GLSL:
                case GST_GL_DISPLAY_CONVERSION_MATRIX:
		            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE_ALPHA,
                        width, height,
                        0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, NULL);

                    gst_gl_display_glgen_texture (display, pTexture_u);

                    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_u);
                    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
                        width, height,
                        0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);
		            break;
	            case GST_GL_DISPLAY_CONVERSION_MESA:
		            glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_YCBCR_MESA,
                        width, height,
                        0, GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, NULL);
		            break;
	            default:
		            g_assert_not_reached ();
            }
		    break;

	    case GST_VIDEO_FORMAT_I420:
	    case GST_VIDEO_FORMAT_YV12:
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
			    width, height,
			    0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

		    gst_gl_display_glgen_texture (display, pTexture_u);

		    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_u);
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
			    GST_ROUND_UP_2 (width) / 2,
			    GST_ROUND_UP_2 (height) / 2,
			    0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);

		    gst_gl_display_glgen_texture (display, pTexture_v);

		    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_v);
		    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_LUMINANCE,
			    GST_ROUND_UP_2 (width) / 2,
			    GST_ROUND_UP_2 (height) / 2,
			    0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
		    break;

	    default:
		    g_assert_not_reached ();
    }
}


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
gst_gl_display_thread_do_upload_fill (GstGLDisplay* display, GLuint* pTexture,
                                      GLuint* pTexture_u, GLuint* pTexture_v,
                                      GstVideoFormat* pVideo_format)
{
    gint width = display->upload_data_with;
    gint height = display->upload_data_height;
    GstVideoFormat video_format = display->upload_video_format;
    gpointer data = display->upload_data;
    *pVideo_format = video_format;

    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture);

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
            switch (display->colorspace_conversion)
            {
	            case GST_GL_DISPLAY_CONVERSION_GLSL:
                case GST_GL_DISPLAY_CONVERSION_MATRIX:
		            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, data);

                    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_u);
                    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                        GST_ROUND_UP_2 (width) / 2, height,
                        GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
		            break;
                case GST_GL_DISPLAY_CONVERSION_MESA:
	                glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                        GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_REV_MESA, data);
                    *pVideo_format = GST_VIDEO_FORMAT_RGBx;
		            break;
	            default:
		            g_assert_not_reached ();
            }
            break;
        case GST_VIDEO_FORMAT_UYVY:
            switch (display->colorspace_conversion)
            {
	            case GST_GL_DISPLAY_CONVERSION_GLSL:
                case GST_GL_DISPLAY_CONVERSION_MATRIX:
		            glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                        GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, data);

                    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_u);
                    glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                        GST_ROUND_UP_2 (width) / 2, height,
                        GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, data);
		            break;
                case GST_GL_DISPLAY_CONVERSION_MESA:
	                glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0, width, height,
                        GL_YCBCR_MESA, GL_UNSIGNED_SHORT_8_8_MESA, data);
                    *pVideo_format = GST_VIDEO_FORMAT_RGBx;
		            break;
	            default:
		            g_assert_not_reached ();
            }
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

                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_u);
                glTexSubImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, 0, 0,
                    GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    (guint8 *) data +
                    gst_video_format_get_component_offset (video_format, offsetU, width, height));

                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, *pTexture_v);
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
}


/* called by gst_gl_display_thread_do_upload (in the gl thread) */
void
gst_gl_display_thread_do_upload_draw (GstGLDisplay* display, GLuint texture, 
                                      GLuint texture_u, GLuint texture_v, 
                                      GstVideoFormat video_format)
{
    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->upload_fbo);

    glBindTexture(GL_TEXTURE_RECTANGLE_ARB, display->upload_texture);
    glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
        display->upload_width, display->upload_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    //attach the texture to the FBO to renderer to
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
        GL_TEXTURE_RECTANGLE_ARB, display->upload_texture, 0);

    glPushAttrib(GL_VIEWPORT_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, display->upload_width, 0.0, display->upload_height);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, display->upload_width, display->upload_height);

    glDrawBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	switch (video_format)
	{
		case GST_VIDEO_FORMAT_RGBx:
		case GST_VIDEO_FORMAT_BGRx:
		case GST_VIDEO_FORMAT_xRGB:
		case GST_VIDEO_FORMAT_xBGR:
			{
				glMatrixMode (GL_PROJECTION);
				glLoadIdentity ();

				glEnable(GL_TEXTURE_RECTANGLE_ARB);
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
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

			    switch (video_format)
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
			    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture_u);
			    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
			    glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glActiveTextureARB(GL_TEXTURE0_ARB);
			    i = glGetUniformLocationARB (GLSLProgram_YUY2_UYVY, "Ytex");
			    glUniform1iARB (i, 0);
			    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
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
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture_u);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

				glActiveTextureARB(GL_TEXTURE2_ARB);
				i = glGetUniformLocationARB (display->GLSLProgram_I420_YV12, "Vtex");
				glUniform1iARB (i, 2);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture_v);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
				glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                glActiveTextureARB(GL_TEXTURE0_ARB);
				i = glGetUniformLocationARB (display->GLSLProgram_I420_YV12, "Ytex");
				glUniform1iARB (i, 0);
				glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
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
			    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
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
        glTexCoord2i (display->upload_data_with, 0);
        glVertex2f (1.0f, -1.0f);
        glTexCoord2i (0, 0);
        glVertex2f (-1.0f, -1.0f);
        glTexCoord2i (0, display->upload_data_height);
        glVertex2f (-1.0f, 1.0f);
        glTexCoord2i (display->upload_data_with, display->upload_data_height);
        glVertex2f (1.0f, 1.0f);
    glEnd ();

    glDrawBuffer(GL_NONE);
    
    //we are done with the shader
    if (display->colorspace_conversion == GST_GL_DISPLAY_CONVERSION_GLSL)
        glUseProgramObjectARB (0);

    glDisable(GL_TEXTURE_RECTANGLE_ARB);

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopAttrib();

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);

    gst_gl_display_check_framebuffer_status();
}


/* called by gst_gl_display_thread_do_download (in the gl thread) */
void
gst_gl_display_thread_do_download_draw (GstGLDisplay* display)
{
    gint width = display->download_width;
    gint height = display->download_height;
    GstVideoFormat video_format = display->download_video_format;
    gpointer data = display->download_data;

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->download_fbo);

    glPushAttrib(GL_VIEWPORT_BIT);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    gluOrtho2D(0.0, width, 0.0, height);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glViewport(0, 0, width, height);

    switch (video_format)
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

                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);
                glEnable(GL_TEXTURE_RECTANGLE_ARB);
            }
            break;

        case GST_VIDEO_FORMAT_YUY2:
        case GST_VIDEO_FORMAT_UYVY:
            {
                gint i=0;
                GLhandleARB GLSLProgram_to_YUY2_UYVY = 0;

				switch (video_format)
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
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);
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
                glUniform1fARB (i, (gfloat)display->ouput_texture_width);
                i = glGetUniformLocationARB (display->GLSLProgram_to_I420_YV12, "h");
                glUniform1fARB (i, (gfloat)display->ouput_texture_height);
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);
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
                glBindTexture (GL_TEXTURE_RECTANGLE_ARB, display->ouput_texture);
            }
            break;

		default:
			g_assert_not_reached ();

	}//end switch display->currentVideo_format

    glBegin (GL_QUADS);
        glTexCoord2i (0, 0);
        glVertex2f (-1.0f, -1.0f);
        glTexCoord2i (width, 0);
        glVertex2f (1.0f, -1.0f);
        glTexCoord2i (width, height);
        glVertex2f (1.0f, 1.0f);
        glTexCoord2i (0, height);
        glVertex2f (-1.0f, 1.0f);
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

    gst_gl_display_check_framebuffer_status();

    glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, display->download_fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);

    switch (video_format) 
    {
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

                glReadPixels (0, 0, width, height, GL_LUMINANCE,
                    GL_UNSIGNED_BYTE, data);

                glReadBuffer(GL_COLOR_ATTACHMENT1_EXT);
                glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    (guint8 *) data +
                    gst_video_format_get_component_offset (video_format, offsetU, width, height));

                glReadBuffer(GL_COLOR_ATTACHMENT2_EXT);
                glReadPixels (0, 0, GST_ROUND_UP_2 (width) / 2, GST_ROUND_UP_2 (height) / 2,
                    GL_LUMINANCE, GL_UNSIGNED_BYTE,
                    (guint8 *) data +
                    gst_video_format_get_component_offset (video_format, offsetV, width, height));
            }
            break;
        default:
            g_assert_not_reached ();
    }

    glBindFramebufferEXT (GL_FRAMEBUFFER_EXT, 0);
    gst_gl_display_check_framebuffer_status();
}
