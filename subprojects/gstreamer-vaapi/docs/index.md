---
short-description: GStreamer plugins from gstreamer-vaapi
...

# VAAPI Plugin

## Environment variables

GStreamer-VAAPI inspects a few of environment variables to define it
usage.

**GST_VAAPI_ALL_DRIVERS.**

This environment variable can be set, independently of its value, to
disable the drivers white list. By default only intel and mesa va
drivers are loaded if they are available. The rest are ignored. With
this environment variable defined, all the available va drivers are
loaded, even if they are deprecated.

**LIBVA_DRIVER_NAME.**

This environment variable can be set with the drivers name to load. For
example, intel's driver is `i965`, meanwhile mesa is `gallium`.

**LIBVA_DRIVERS_PATH.**

This environment variable can be set to a colon-separated list of paths
(or a semicolon-separated list on Windows). libva will scan these paths
for va drivers.

**GST_VAAPI_DRM_DEVICE.**
This environment variable can be set to a specified DRM device when DRM
display is used, it is ignored when other types of displays are used.
By default /dev/dri/renderD128 is used for DRM display.
