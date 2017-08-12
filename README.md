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

* We recompile and override hotdoc's default theme, which is a submodule of this project:

```
git submodule update --init
```

Follow the instructions outlined in the theme's README.md, you can dispense
with the last step (building the theme):

```
cd theme/hotdoc_bootstrap_theme
sudo dnf install nodejs # On Fedora
sudo apt-get install nodejs nodejs-legacy npm # debian
sudo pacman -S nodejs npm # arch
npm install && ./node_modules/bower/bin/bower install
cd ../..
```

* Build the portal:

```
make
```

* And browse it:

```
xdg-open built_doc/html/index.html
```
