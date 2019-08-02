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

Elements do not need to be registered to be used in GStreamer, so one
can simply instantiate the elements with `g_object_new ()` and use them
in pipelines like any other.

If the statically linked elements should also be available through a
name for functions such as `gst_element_factory_make ()`, these elements
can be registered direclty without a plugin. For that you can use
`gst_element_register ()` with `NULL` as plugin parameter.

While these two methods are usually sufficient, it is also possible
to register a static plugin.

### Static plugins

Dynamically loaded plugins contain a structure that's defined using
`GST_PLUGIN_DEFINE ()`. This structure is loaded when the plugin is
loaded by the GStreamer core. The structure contains an initialization
function (usually called `plugin_init`) that will be called right after
that. It's purpose is to register the elements provided by the plugin
with the GStreamer framework. To register a static plugin, the only
thing you need to do is to replace `GST_PLUGIN_DEFINE ()` with a call
to `gst_plugin_register_static ()`. As soon as you call
`gst_plugin_register_static ()`, the elements will from then on be
available like any other element, without them having to be dynamically
loadable libraries. In the example below, you would be able to call
`gst_element_factory_make("my-element-name", "some-name")` to create
an instance of the element.

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
