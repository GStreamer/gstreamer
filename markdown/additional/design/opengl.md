# OpenGL

OpenGL is a venerable, cross-platform 3D graphics API available for use
on Linux, Windows, MacOS, iOS and Android and is usually backed by
specialized hardware (a GPU) to accelarate rendering.  There are however,
also CPU-based software implementations available for ensuring correctness and
OpenGL use without a GPU.

## Limits imposed by OpenGL

### OpenGL and Threads

A major design decision of OpenGL has an OpenGL context only being available
(or current) for use in a single thread.  This is directly at odds with
GStreamer's multi-threaded design and requires guidance and/or restrictions on
how GStreamer and the application will interact with OpenGL.  There are two
main models for using OpenGL from multiple threads.

The first involves creating an OpenGL context for each thread that will use
OpenGL in such a way that all the OpenGL contexts can share OpenGL resources.
While this does ensure that each thread can execute and use OpenGL resources
concurrently, it has a high resources cost for two main reasons.

 1. OpenGL contexts are expensive to create and maintain.  e.g. a single OpenGL
    context can use multiple MB of memory for storing all the required state.
 2. Transfering between different OpenGL contexts requires synchronisation.
    This incurs a penalty at runtime performing the required synchronisation
    as well as an increase in code complexity.

The second involves making the OpenGL context current whenever it is
needed. On some platforms this is possible to perform lazily as the context
will be uncurrented in the previous thread automatically.  Regrettably this
behaviour is not guaranteed for all platforms and means that transfering an
OpenGL context between threads requires explicitly uncurrenting from the
previous thread.  The other issue with this is performance related where on
some platforms, making an OpenGL current could could consume a large amount of
CPU time.

GStreamer takes a different approach to this by marshalling all the OpenGL
functionality to a dedicated OpenGL thread that is created and destroyed with
the `GstGLContext`.  This removes the synchronisation requirements between
multiple GStreamer OpenGL contexts, removes the need for multiple OpenGL
contexts inside GStreamer reducing the memory requirements, and avoids the
possibly expensive currenting and uncurrenting of the OpenGL context multiple
times per frame.  The marshalling is performed in the `GstGLWindow`
implementation by `gst_gl_window_send_message()` /
`gst_gl_window_send_message_async()` and is required to be re-entrant by
ensuring that marshalling from the OpenGL thread must execute the marshalled
operation immediately. The only downside of marshalling everything to a
dedicated thread has is that OpenGL in GStreamer requires two
thread switches for calling a block of OpenGL commands from outside the
OpenGL thread.  Given the possibly high cost of the other approaches
this is an acceptable trade-off.

### OpenGL Function Pointers

Another design choice of OpenGL is that (at least on the windows platform)
OpenGL functions can be OpenGL context specific.  This means that retrieving a
function pointer from one OpenGL context has no guarentee of doing the correct
thing with any other OpenGL context.  Each `GstGLContext` has a `GstGLFuncs`
structure that contains a list of OpenGL functions known to libgstgl that is
populated by a call to `gst_gl_context_fill_info()`.

## libgstgl Library

This library provides the necessary infrastructure for integrating OpenGL usage
in GStreamer between elements in the pipeline and an application that may use
OpenGL.  The library can be split into 3 main parts:

 - Platform-specific wrappers (`GstGLDisplay`, `GstGLWindow`, `GstGLContext`)
 - GStreamer subclasses
   - Element base clases (`GstGLBaseFilter`, `GstGLFilter`, `GstGLBaseMixer`, etc)
   - Data/Memory (`GstGLBuffer`, `GstGLMemory`, `GstGLBufferPool`, etc)
   - Synchronisation (`GstGLSyncMeta`)
 - OpenGL helpers (`GstGLShader`, `GstGLFramebuffer`, etc)

### Platform Specifics

Due to the nature of OpenGL, the use of OpenGL requires a connection to a
windowing system (X11, Wayland, Cocoa, GDI+, etc).  These windowing systems
are entirely platform specific and are wrapped in the `GstGLDisplay` generic
interface.  Specific functionality exposed by a windowing system may be exposed
by platform-specific `GstGLDisplay` objects.  e.g. `GstGLDisplayWayland` or
`GstGLDisplayX11`.

Another platform specific part of the puzzle is the `GstGLWindow` object which
encapsulates the surface that is being rendered into by OpenGL.  There are
specific windowing system implementations for each platform that are all exposed
through the generic `GstGLWindow` interface.  An application generally does not
need to interact with the window used by an OpenGL context with GStreamer.

The last platform specific piece is the `GstGLContext` which represents a
OpenGL context.  A `GstGLContext` has two main forms, wrapped (created with
`gst_gl_context_new_wrapped()`) vs not.  Some `GstGLContext` API behaves
differently with a wrapped `GstGLContext`.

#### Wrapped `GstGLContext`s

A wrapped `GstGLContext` is simply a container to an OpenGL context handle and
does not contain any separate thread to execute the OpenGL operations on.
This means that `gst_gl_context_thread_add()` with a wrapped `GstGLContext`
will attempt to execute the function immediately and may fail in some way
(crashes, ignored) if the actual OpenGL context is not active in the current
thread.  Even if the OpenGL context is active in the current thread, GStreamer
will not know that and needs to be told explicitly through the necessary calls
to `gst_gl_context_set_active()`.

### Application Integration

There are two types of resources required for integration between GStreamer
elements and the application, platform specific and generic OpenGL resources.

First, the platform specific resources are the `GstGLDisplay` for the
required connection to the windowing system and the `GstGLContext` for the
OpenGL context that the application is using.  With these two pieces of
information, it is possible for certain OpenGL resources to be 'shared'
between GStreamer and the application subject to lifetime and synchronisation
requirements.

#### Sharing X11 Display With GStreamer

```c
static gboolean
sync_bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_NEED_CONTEXT:
    {
      const gchar *context_type;
      GstContext *context = NULL;
     
      gst_message_parse_context_type (msg, &context_type);
      g_print("got need context %s\n", context_type);

      if (g_strcmp0 (context_type, GST_GL_DISPLAY_CONTEXT_TYPE) == 0) {
        Display *x11_display; /* get this from the application somehow */
        GstGLDisplay *gl_display = GST_GL_DISPLAY (gst_gl_display_x11_new_with_display (x11_display));

        context = gst_context_new (GST_GL_DISPLAY_CONTEXT_TYPE, TRUE);
        gst_context_set_gl_display (context, gl_display);

        gst_element_set_context (GST_ELEMENT (msg->src), context);
      }
      if (context)
        gst_context_unref (context);
      break;
    }
    default:
      break;
  }

  return FALSE;
}
```

#### Sharing OpenGL Context With GStreamer

```c
static gboolean
sync_bus_call (GstBus *bus, GstMessage *msg, gpointer    data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_NEED_CONTEXT:
    {
      const gchar *context_type;
      GstContext *context = NULL;
     
      gst_message_parse_context_type (msg, &context_type);
      g_print("got need context %s\n", context_type);

      if (g_strcmp0 (context_type, "gst.gl.app_context") == 0) {
        GstGLContext *gl_context; /* get this from the application somehow */
        GstStructure *s;

        context = gst_context_new ("gst.gl.app_context", TRUE);
        s = gst_context_writable_structure (context);
        gst_structure_set (s, "context", GST_GL_TYPE_CONTEXT, gl_context, NULL);

        gst_element_set_context (GST_ELEMENT (msg->src), context);
      }
      if (context)
        gst_context_unref (context);
      break;
    }
    default:
      break;
  }

  return FALSE;
}
```

Second, other OpenGL resources such as buffers or textures require wrapping
into a GStreamer compatible format.  For data-specific OpenGL resources, they
are wrapped up in some form of `GstMemory`.  The application can create these
resources for pushing into GStreamer or consume these resources from GStreamer.
See the resource specific sections for more details.

### `GstGLMemory`

`GstGLMemory` is a `GstMemory` subclass that holds a single plane of a video
frame in an OpenGL texture.

### `GstGLBuffer`

`GstGLBuffer` is a `GstMemory` subclass that holds data stored in an OpenGL
buffer object.

### Automatic Transfers To/From The GPU

Both `GstGLMemory` and `GstGLBuffer` implement automatic transfers to and from
OpenGL based entirely on the sequence of calls to `gst_memory_map()` /
`gst_memory_unmap()`.  Combined with a special `GstMapFlags`, `GST_MAP_GL`, the
OpenGL based `GstMemory` implementations can discern where the most recent
data was written and copy the data on-demand to where it is needed on read.

Internally this is implemented using two extra `GstMemoryFlags`,
`GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD` and
`GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD` that indicate that a transfer is
needed if that specific domain (system memory or OpenGL) is mapped.
The transfer flags are automatically updated and should not be modified by
elements or the application.

## Elements

There are a number of elements that make use of the OpenGL API for their
functionality.  A non-comprehensive list is provided below.

 - `glimagesinkelement` - Display a video using OpenGL
 - `glcolorconvert` - Convert between diferent color spaces
 - `glviewconvert` - Convert between different stereo view formats
 - `gltransformation` - Perfom transformations in 3D space of the 2D video plane
 - `gleffects` - Various OpenGL effects
 - `glvideomixerelement` - Mix video using OpenGL (roughly equivalent to compositor)
 - `glcolorbalance` - Color balance filtering
 - `gltestsrc` - OpenGL equivalent to videotestsrc
 - `glshader` - Execute an arbitrary OpenGL shader
 - `gloverlay` - Overlay an image onto a video stream
 - `glupload` - Upload data into OpenGL
 - `gldownload` - Download data from OpenGL

