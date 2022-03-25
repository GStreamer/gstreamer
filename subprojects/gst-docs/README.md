# Introduction

This is a collection of design documents, formerly maintained in various
different locations and formats, now grouped together and converted
to commonmark.

# Contributing

## Style

We will follow the commonmark specification.

We *should* try to follow this
[style guide](http://www.cirosantilli.com/markdown-style-guide/#about),
but are still [evaluating solutions](https://github.com/jgm/cmark/issues/131)
for *stable* automatic formatting.

80 columns line width is thus not yet enforced, but strongly suggested.

# Build the documentation

## Install dependencies

* Follow [hotdoc's installation guide](https://hotdoc.github.io/installing.html),
  preferably in a virtualenv.

* We *experimentally* use the hotdoc C extension to include functions by
  name, follow the steps outlined [here](https://github.com/hotdoc/hotdoc_c_extension)

## Build the portal without the API documentation

```
meson build
ninja -C build/ GStreamer-doc
```

And browse it:

```
gio open build/GStreamer-doc/html/index.html
```

## API documentation

Building the API documentation in the portal implies using the full multi-repo
[gstreamer](https://gitlab.freedesktop.org/gstreamer/gstreamer/) build which
allows us to aggregate the documentation from all GStreamer modules using the
hotdoc subproject feature.

From `gstreamer`:

```
meson build/
./gst-env ninja -C build subprojects/gst-docs/GStreamer-doc
```

And browse the doc:

```
gio open build/subprojects/gst-docs/GStreamer-doc/html/index.html
```

You can also generate a release tarball of the portal with:

```
ninja -C build gst-docs@@release
```

### Adding a newly written plugin to the documentation

To add a plugin to the documentation you need to add the given
meson target to the `plugins` list present in each GStreamer module for
example:

``` meson
gst_elements = library('gstcoreelements',
  gst_elements_sources,
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gst_dep, gst_base_dep],
  install : true,
  install_dir : plugins_install_dir,
)
plugins += [gst_elements]
```

Then you need to regenerate the `gst_plugins_cache.json` file by running
the target manually, if building from the module itself:

```
ninja -C <build-dir> docs/gst_plugins_cache.json
```

if you use the mono repo build there's also a target that will rebuild all
the cache files in the various GStreamer subprojects:

```
ninja -C <build-dir> plugins_doc_caches`
```

NOTE: the newly generated cache should be commited to the git repos.

The plugins documentation is generated based on that file to
avoid needing to have built all plugins to get the documentation generated.

NOTE: When moving plugins from one module to another, the `gst_plugins_cache.json`
from the module where the plugin has been removed should be manually edited
to reflect the removal.

## Licensing

The content of this module comes from a number of different sources and is
licensed in different ways:

### Tutorial source code

All tutorial code is licensed under any of the following licenses (your choice):

 - 2-clause BSD license ("simplified BSD license") (`LICENSE.BSD`)
 - MIT license (`LICENSE.MIT`)
 - LGPL v2.1 (`LICENSE.LGPL-2.1`), or (at your option) any later version

This means developers have maximum flexibility and can pick the right license
for any derivative work.

### Application Developer Manual and Plugin Writer's Guide

These are licensed under the [Open Publication License v1.0][op-license]
(`LICENSE.OPL`), for historical reasons.

[op-license]: http://www.opencontent.org/openpub/

### Documentation

#### Tutorials

The tutorials are licensed under the [Creative Commons CC-BY-SA-4.0 license][cc-by-sa-4.0]
(`LICENSE.CC-BY-SA-4.0`).

[cc-by-sa-4.0]: https://creativecommons.org/licenses/by-sa/4.0/

#### API Reference and Design Documentation

The remaining documentation, including the API reference and Design Documentation,
is licensed under the LGPL v2.1 (`LICENSE.LGPL-2.1`), or (at your option) any later
version.
