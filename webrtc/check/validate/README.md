# What is this?

The entire contents of this folder perform testing of GStreamer's webrtc
implementation against browser implementations using the selenium webdriver
testing framework.

# Dependencies:

- gst-validate: https://gitlab.freedesktop.org/gstreamer/gst-devtools/tree/master/validate
- gst-python: https://gitlab.freedesktop.org/gstreamer/gst-python/
- selenium: https://www.seleniumhq.org/projects/webdriver/
- selenium python bindings
- chrome and firefox with webdriver support

# Run the tests

`GST_VALIDATE_APPS_DIR=/path/to/gst-examples/webrtc/check/validate/apps/ GST_VALIDATE_SCENARIOS_PATH=/path/to/gst-examples/webrtc/check/validate/scenarios/ gst-validate-launcher --testsuites-dir /path/to/gst-examples/webrtc/check/validate/testsuites/ webrtc`
