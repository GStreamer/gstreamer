# Context

`GstContext` is a container object, containing a type string and a generic
`GstStructure`. It is used to store and propagate context information in a
pipeline, like device handles, display server connections and other
information that should be shared between multiple elements in a
pipeline.

For sharing context objects and distributing them between application
and elements in a pipeline, there are downstream queries, upstream
queries, messages and functions to set a context on a complete pipeline.

## Context types

Context type names should be unique and be put in appropriate
namespaces, to prevent name conflicts, e.g. "gst.egl.EGLDisplay". Only
one specific type is allowed per context type name.

## Elements

Elements that need a specific context for their operation would do the
following steps until one succeeds:

1) Check if the element already has a context of the specific type,
   i.e. it was previously set via `gst_element_set_context()`.

2) Query downstream with `GST_QUERY_CONTEXT` for the context and check if
   downstream already has a context of the specific type

3) Query upstream with `GST_QUERY_CONTEXT` for the context and check if
   upstream already has a context of the specific type

4) Post a `GST_MESSAGE_NEED_CONTEXT` message on the bus with the required
   context types and afterwards check if a usable context was set now
   as in 1). The message could be handled by the parent bins of the
   element and the application.

4) Create a context by itself and post a `GST_MESSAGE_HAVE_CONTEXT` message
       on the bus.

Bins will propagate any context that is set on them to their child
elements via `gst_element_set_context()`. Even to elements added after
a given context has been set.

Bins can handle the `GST_MESSAGE_NEED_CONTEXT` message, can filter both
messages and can also set different contexts for different pipeline
parts.

## Applications

Applications can set a specific context on a pipeline or elements inside
a pipeline with `gst_element_set_context()`.

If an element inside the pipeline needs a specific context, it will post
a `GST_MESSAGE_NEED_CONTEXT` message on the bus. The application can
now create a context of the requested type or pass an already existing
context to the element (or to the complete pipeline).

Whenever an element creates a context internally it will post a
`GST_MESSAGE_HAVE_CONTEXT` message on the bus. Bins will cache these
contexts and pass them to any future element that requests them.
