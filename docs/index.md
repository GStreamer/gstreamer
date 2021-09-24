# GStreamer Validate

GstValidate is a tool that allows GStreamer developers to check that the
GstElements they write behave the way they are supposed to. It was first
started to provide plug-ins developers with a tool to check that they
use the framework the proper way.

GstValidate implements a monitoring logic that allows the system to
check that the elements of a GstPipeline respect some rules GStreamer
components have to follow to make them properly interact together. For
example, a GstValidatePadMonitor will make sure that if we receive a
GstSegment from upstream, an equivalent segment is sent downstream
before any buffer gets out.

Then GstValidate implements a reporting system that allows users to get
detailed informations about what was not properly handled by the
elements. The generated reports are ordered by level of importance from
"issue" to "critical".

Some tools have been implemented to help developers validate and test
their GstElement, see [gst-validate](gst-validate.md) for example.

On top of that, the notion of a [validation scenario](gst-validate-scenarios.md)
has been implemented so that developers can easily execute a set of actions on
pipelines to test real world interactive cases and reproduce existing
issues in a convenient way.
