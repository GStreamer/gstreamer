 /* File : Gst.i */
%module Gst

%include "typemap.i"
%include "GstPipeline.i"
%include "GstElement.i"
%include "GstBin.i"
%include "GstPad.i"

%{
#include <gst/gst.h>
%}

%include "gstswig.c"
