/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef WIN32
#include <windows.h>
#endif

#include <GL/gl.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

#ifndef WIN32
#include <GL/glx.h>
#include <SDL2/SDL_syswm.h>
#include <gst/gl/x11/gstgldisplay_x11.h>
#endif

#include <gst/gst.h>
#include <gst/gl/gl.h>

static GstGLContext *sdl_context;
static GstGLDisplay *sdl_gl_display;

static guint32 sdl_message_event = -1;
static SDL_Window *sdl_window;
static SDL_GLContext sdl_gl_context;

static GAsyncQueue *queue_input_buf;
static GAsyncQueue *queue_output_buf;

/* rotation angle for the triangle. */
float rtri = 0.0f;

/* rotation angle for the quadrilateral. */
float rquad = 0.0f;

/* A general OpenGL initialization function.  Sets all of the initial parameters. */
static void
InitGL (int Width, int Height)  // We call this right after our OpenGL window is created.
{
  glViewport (0, 0, Width, Height);
  glClearColor (0.0f, 0.0f, 0.0f, 0.0f);        // This Will Clear The Background Color To Black
  glClearDepth (1.0);           // Enables Clearing Of The Depth Buffer
  glDepthFunc (GL_LESS);        // The Type Of Depth Test To Do
  glEnable (GL_DEPTH_TEST);     // Enables Depth Testing
  glShadeModel (GL_SMOOTH);     // Enables Smooth Color Shading

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();            // Reset The Projection Matrix

  glMatrixMode (GL_MODELVIEW);
}

/* The main drawing function. */
static void
DrawGLScene (GstBuffer * buf)
{
  GstVideoFrame v_frame;
  GstVideoInfo v_info;
  guint texture;

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 320, 240);

  if (!gst_video_frame_map (&v_frame, &v_info, buf, GST_MAP_READ | GST_MAP_GL)) {
    g_warning ("Failed to map the video buffer");
    return;
  }

  texture = *(guint *) v_frame.data[0];

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);  // Clear The Screen And The Depth Buffer
  glLoadIdentity ();            // Reset The View

  glTranslatef (-0.4f, 0.0f, 0.0f);     // Move Left 1.5 Units And Into The Screen 6.0

  glRotatef (rtri, 0.0f, 1.0f, 0.0f);   // Rotate The Triangle On The Y axis 
  // draw a triangle (in smooth coloring mode)
  glBegin (GL_POLYGON);         // start drawing a polygon
  glColor3f (1.0f, 0.0f, 0.0f); // Set The Color To Red
  glVertex3f (0.0f, 0.4f, 0.0f);        // Top
  glColor3f (0.0f, 1.0f, 0.0f); // Set The Color To Green
  glVertex3f (0.4f, -0.4f, 0.0f);       // Bottom Right
  glColor3f (0.0f, 0.0f, 1.0f); // Set The Color To Blue
  glVertex3f (-0.4f, -0.4f, 0.0f);      // Bottom Left  
  glEnd ();                     // we're done with the polygon (smooth color interpolation)

  glEnable (GL_TEXTURE_2D);
  glBindTexture (GL_TEXTURE_2D, texture);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glLoadIdentity ();            // make sure we're no longer rotated.
  glTranslatef (0.5f, 0.0f, 0.0f);      // Move Right 3 Units, and back into the screen 6.0

  glRotatef (rquad, 1.0f, 0.0f, 0.0f);  // Rotate The Quad On The X axis 
  // draw a square (quadrilateral)
  glColor3f (0.4f, 0.4f, 1.0f); // set color to a blue shade.
  glBegin (GL_QUADS);           // start drawing a polygon (4 sided)
  glTexCoord3f (0.0f, 1.0f, 0.0f);
  glVertex3f (-0.4f, 0.4f, 0.0f);       // Top Left
  glTexCoord3f (1.0f, 1.0f, 0.0f);
  glVertex3f (0.4f, 0.4f, 0.0f);        // Top Right
  glTexCoord3f (1.0f, 0.0f, 0.0f);
  glVertex3f (0.4f, -0.4f, 0.0f);       // Bottom Right
  glTexCoord3f (0.0f, 0.0f, 0.0f);
  glVertex3f (-0.4f, -0.4f, 0.0f);      // Bottom Left  
  glEnd ();                     // done with the polygon

  glBindTexture (GL_TEXTURE_2D, 0);

  rtri += 1.0f;                 // Increase The Rotation Variable For The Triangle
  rquad -= 1.0f;                // Decrease The Rotation Variable For The Quad 

  // swap buffers to display, since we're double buffered.
  SDL_GL_SwapWindow (sdl_window);

  gst_video_frame_unmap (&v_frame);
}

/* appsink new-sample callback */
static GstFlowReturn
on_new_sample (GstElement * appsink, gpointer data)
{
  GstSample *sample = NULL;
  GstBuffer *buf;

  g_signal_emit_by_name (appsink, "pull-sample", &sample, NULL);
  if (!sample)
    return GST_FLOW_FLUSHING;

  buf = gst_sample_get_buffer (sample);
  /* ref then push buffer to use it in sdl */
  gst_buffer_ref (buf);

  gst_sample_unref (sample);

  g_async_queue_push (queue_input_buf, buf);

  /* pop then unref buffer we have finished to use in sdl */
  if (g_async_queue_length (queue_output_buf) > 3) {
    GstBuffer *buf_old = (GstBuffer *) g_async_queue_pop (queue_output_buf);
    gst_buffer_unref (buf_old);
  }

  return GST_FLOW_OK;
}

static void
sync_bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_NEED_CONTEXT:
    {
      const gchar *context_type;

      gst_message_parse_context_type (msg, &context_type);
      g_print ("got need context %s\n", context_type);

      if (g_strcmp0 (context_type, GST_GL_DISPLAY_CONTEXT_TYPE) == 0) {
        GstContext *display_context =
            gst_context_new (GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
        gst_context_set_gl_display (display_context, sdl_gl_display);
        gst_element_set_context (GST_ELEMENT (msg->src), display_context);
        gst_context_unref (display_context);
      } else if (g_strcmp0 (context_type, "gst.gl.app_context") == 0) {
        GstContext *app_context = gst_context_new ("gst.gl.app_context", TRUE);
        GstStructure *s = gst_context_writable_structure (app_context);
        gst_structure_set (s, "context", GST_TYPE_GL_CONTEXT, sdl_context,
            NULL);
        gst_element_set_context (GST_ELEMENT (msg->src), app_context);
        gst_context_unref (app_context);
      }
      break;
    }
    default:
    {
      SDL_Event event = { 0, };

      event.type = sdl_message_event;
      SDL_PushEvent (&event);

      break;
    }
  }
}

static void
sdl_event_loop (GstBus * bus)
{
  GstBuffer *buf = NULL;
  gboolean quit = FALSE;

  SDL_GL_MakeCurrent (sdl_window, sdl_gl_context);
  SDL_GL_SetSwapInterval (1);

  while (!quit) {
    SDL_Event event;

    while (SDL_PollEvent (&event)) {
      if (event.type == SDL_QUIT) {
        quit = TRUE;
      }
      if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          quit = TRUE;
        }
      }

      if (event.type == sdl_message_event) {
        GstMessage *msg;

        while ((msg = gst_bus_pop (bus))) {
          switch (GST_MESSAGE_TYPE (msg)) {

            case GST_MESSAGE_EOS:
              g_print ("End-of-stream\n");
              g_print
                  ("For more information, try to run: GST_DEBUG=gl*:3 ./sdlshare\n");
              quit = TRUE;
              break;

            case GST_MESSAGE_ERROR:
            {
              gchar *debug = NULL;
              GError *err = NULL;

              gst_message_parse_error (msg, &err, &debug);

              g_print ("Error: %s\n", err->message);
              g_error_free (err);

              if (debug) {
                g_print ("Debug deails: %s\n", debug);
                g_free (debug);
              }

              quit = TRUE;
              break;
            }

            default:
              break;
          }

          gst_message_unref (msg);
        }
      }
    }

    while (g_async_queue_length (queue_input_buf) > 3) {
      if (buf)
        g_async_queue_push (queue_output_buf, buf);
      buf = (GstBuffer *) g_async_queue_pop (queue_input_buf);
    }

    if (buf)
      DrawGLScene (buf);
  }

  SDL_GL_MakeCurrent (sdl_window, NULL);

  if (buf)
    g_async_queue_push (queue_output_buf, buf);
}

int
main (int argc, char **argv)
{

#ifdef WIN32
  HGLRC gl_context = 0;
  HDC sdl_dc = 0;
#else
  SDL_SysWMinfo info;
  Display *sdl_display = NULL;
  GLXContext gl_context = NULL;
#endif

  GstPipeline *pipeline = NULL;
  GstBus *bus = NULL;
  GstElement *appsink = NULL;
  GstState state;
  const gchar *platform;

  /* Initialize SDL for video output */
  if (SDL_Init (SDL_INIT_VIDEO) < 0) {
    fprintf (stderr, "Unable to initialize SDL: %s\n", SDL_GetError ());
    return -1;
  }

  sdl_message_event = SDL_RegisterEvents (1);
  g_assert (sdl_message_event != -1);

  /* Create a 640x480 OpenGL window */
  sdl_window =
      SDL_CreateWindow ("SDL and gst-plugins-gl", SDL_WINDOWPOS_UNDEFINED,
      SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_OPENGL);
  if (sdl_window == NULL) {
    fprintf (stderr, "Unable to create OpenGL screen: %s\n", SDL_GetError ());
    SDL_Quit ();
    return -1;
  }

  sdl_gl_context = SDL_GL_CreateContext (sdl_window);

  gst_init (&argc, &argv);

  SDL_GL_MakeCurrent (sdl_window, sdl_gl_context);

  /* Loop, drawing and checking events */
  InitGL (640, 480);

#ifdef WIN32
  gl_context = wglGetCurrentContext ();
  sdl_dc = wglGetCurrentDC ();
  platform = "wgl";
  sdl_gl_display = gst_gl_display_new ();
#else
  SDL_VERSION (&info.version);
  SDL_GetWindowWMInfo (sdl_window, &info);
  sdl_display = info.info.x11.display;
  gl_context = glXGetCurrentContext ();
  platform = "glx";
  sdl_gl_display =
      (GstGLDisplay *) gst_gl_display_x11_new_with_display (sdl_display);
#endif
  SDL_GL_MakeCurrent (sdl_window, NULL);

  sdl_context =
      gst_gl_context_new_wrapped (sdl_gl_display, (guintptr) gl_context,
      gst_gl_platform_from_string (platform), GST_GL_API_OPENGL);

  pipeline =
      GST_PIPELINE (gst_parse_launch
      ("videotestsrc ! glupload ! gleffects effect=5 ! "
          "appsink name=sink sync=true "
          "caps=video/x-raw(memory:GLMemory),format=RGBA,width=320,height=240,framerate=(fraction)30/1,texture-target=2D",
          NULL));

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (bus, "sync-message", G_CALLBACK (sync_bus_call), NULL);

  /* NULL to PAUSED state pipeline to make sure the gst opengl context is created and
   * shared with the sdl one */
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PAUSED);
  state = GST_STATE_PAUSED;
  if (gst_element_get_state (GST_ELEMENT (pipeline), &state, NULL,
          GST_CLOCK_TIME_NONE) != GST_STATE_CHANGE_SUCCESS) {
    g_debug ("failed to pause pipeline\n");
    return -1;
  }

  queue_input_buf = g_async_queue_new ();
  queue_output_buf = g_async_queue_new ();

  /* append a gst-gl texture to this queue when you do not need it no more */
  appsink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  g_object_set (appsink, "emit-signals", TRUE, NULL);
  g_signal_connect (appsink, "new-sample", G_CALLBACK (on_new_sample), NULL);
  gst_object_unref (appsink);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);

  sdl_event_loop (bus);

  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  gst_object_unref (pipeline);

  gst_object_unref (bus);

  gst_object_unref (sdl_context);
  gst_object_unref (sdl_gl_display);

  /* make sure there is no pending gst gl buffer in the communication queues 
   * between sdl and gst-gl
   */
  while (g_async_queue_length (queue_input_buf) > 0) {
    GstBuffer *buf = (GstBuffer *) g_async_queue_pop (queue_input_buf);
    gst_buffer_unref (buf);
  }

  while (g_async_queue_length (queue_output_buf) > 0) {
    GstBuffer *buf = (GstBuffer *) g_async_queue_pop (queue_output_buf);
    gst_buffer_unref (buf);
  }

  SDL_GL_DeleteContext (gl_context);

  SDL_DestroyWindow (sdl_window);

  SDL_Quit ();

  return 0;
}
