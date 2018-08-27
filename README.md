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

# Build a web portal from the sources

## Install dependencies

* Follow [hotdoc's installation guide](https://people.collabora.com/~meh/hotdoc_hotdoc/html/installing.html),
  preferably in a virtualenv.

* We *experimentally* use the hotdoc C extension to include functions by
  name, follow the steps outlined [here](https://github.com/hotdoc/hotdoc_c_extension)

* Build the portal:

```
make
```

* And browse it:

```
gio open built_doc/html/index.html
```

## Licensing

The content of this module comes from a number of different sources and is
licensed in different ways:

### Tutorial source code

All tutorial code is licensed under any of the following licenses (your choice):

 - 2-clause BSD license ("simplified BSD license") (`LICENSE.BSD`)
 - MIT license (`LICENSE.MIT`)
 - LGPL v2.1 (`LICENSE.LGPL-2.1`)

This means developers have maximum flexibility and can pick the right license
for any derivative work.

### Application Developer Manual and Plugin Writer's Guide

These are licensed under the [Open Publication License v1.0][op-license]
(`LICENSE.OPL`), for historical reasons.

[op-license]: http://www.opencontent.org/openpub/

### Documentation

Mostly licensed under the [Creative Commons CC-BY-SA-4.0 license][cc-by-sa-4.0],
but some parts of the documentation may still be licensed differently
(e.g. LGPLv2.1) for historical reasons.

[cc-by-sa-4.0]: https://creativecommons.org/licenses/by-sa/4.0/
