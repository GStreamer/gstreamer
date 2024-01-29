# Test Harnesses

This directory contains a collection of library harnesses for use with Mayhem or other dynamic analysis applications.

## Steps to build harnesses

In order to include these harnesses in the build, simply add the flag during configuration:
```
meson -Dharnesses=enabled [options...] builddir
meson compile -C builddir
```

## To add a new harness

Dependencies are largely handled already by the `meson.build` file located in this directory. To add additional targets, it should be as easy as adding:

```
harnesses = [
  ['test_jpegdec.c',
   '<your_harness_here.c>,
   ...
  ]
]
```
to the list of harnesses in the `meson.build`. If you need additional dependencies, examples of adding those are also in the `meson.build` file.
