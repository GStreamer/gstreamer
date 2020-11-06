---
title: Compiling
...

# Compiling

This section talks about the different things you can do when building
and shipping your applications and plugins.

## Embedding static elements in your application

The [Plugin Writer's Guide](plugin-development/index.md)
describes in great detail how to write elements for the GStreamer
framework. In this section, we will solely discuss how to embed such
elements statically in your application. This can be useful for
application-specific elements that have no use elsewhere in GStreamer.

Dynamically loaded plugins contain a structure that's defined using
`GST_PLUGIN_DEFINE ()`. This structure is loaded when the plugin is
loaded by the GStreamer core. The structure contains an initialization
function (usually called `plugin_init`) that will be called right after
that. It's purpose is to register the elements provided by the plugin
with the GStreamer framework. If you want to embed elements directly in
your application, the only thing you need to do is to replace
`GST_PLUGIN_DEFINE ()` with a call to `gst_plugin_register_static ()`.
As soon as you call `gst_plugin_register_static ()`, the elements will
from then on be available like any other element, without them having to
be dynamically loadable libraries. In the example below, you would be
able to call `gst_element_factory_make
("my-element-name", "some-name")` to create an instance of the element.

``` c

/*
 * Here, you would write the actual plugin code.
 */

[..]

static gboolean
register_elements (GstPlugin *plugin)
{
  return GST_ELEMENT_REGISTER (my_element_name, plugin);
}

static
my_code_init (void)
{
  ...

  gst_plugin_register_static (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "my-private-plugins",
    "Private elements of my application",
    register_elements,
    VERSION,
    "LGPL",
    "my-application-source",
    "my-application",
    "http://www.my-application.net/")

  ...
}


```
