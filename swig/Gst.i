 /* File : Gst.i */
%module Gst

%include "typemap.i"
%include "GstPipeline.i"
%include "GstElement.i"

%{
#include <gst/gst.h>
%}

%include "gstswig.c"
