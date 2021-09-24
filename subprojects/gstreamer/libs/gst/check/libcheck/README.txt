This is a copy of libcheck, a unit testing framework for C:

https://github.com/libcheck/check/

The last update was on 9th December, with the following commit: ba42e7de3d62ea9d3699bf0709554b3e47a8f09e

The check*.c files in this directory are the same as those in the src/
directory in upstream. The files in the libcompat/ directory are the same as
those in the lib/ directory upstream.

lib/snprintf.c was omitted since we don't run on any platforms that don't
provide snprintf and the upstream implementation is ~2000 lines.

lib/malloc.c and lib/realloc.c were omitted since we were doing fine without
them and it does a #define malloc rpl_malloc on Android because the malloc
shipped with Bionic is not GNU-compliant. rpl_malloc is provided by libcheck,
but not everything in gstreamer links against libcheck. We also don't care
about this.

Steps to sync with upstream:

1. Clone libcheck from the above git repository
2. Copy files into this directory
3. Run GNU indent on all the code
4. Fix internal #includes
5. Manually inspect the diff
6. Update configure.ac, m4/check-checks.m4, meson.build files, etc
6. Run make check, then commit and push

Any changes made to files in this directory must be submitted upstream via
a pull request: https://github.com/libcheck/check/compare

This involves creating an account on GitHub, forking libcheck/check there,
pushing the changes into a branch, and then submitting it as a pull request.
