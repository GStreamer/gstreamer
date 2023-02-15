---
title: SSIM plugin
short_description: GstValidate plugin to detect frame corruptions
...

# SSIM plugin

GstValidate plugin to run the ssim algorithm on the buffers flowing in the
pipeline to find regressions and detect frame corruptions.
It allows you to generate image files from the buffers flowing in the pipeline
(either as raw in the many formats supported by GStreamer or as png) and then
check them against pre generated, reference images.

The ssim algorithm will set a value of 1.0 when images are perfectly identical,
and -1.0 if they have nothing in common. By default we consider images as similar
if they have at least a ssim value of 0.95 but you can override it defining the value
under which the test will be considered as failed.

Errors are reported on the GstValidate reporting system. You can also ask
the plugin to  generate grey scale output images. Those will be named in a way
that should lets you precisely see where and how the test failed.

# Configuration

The configuration of the plugin is done through a validate configuration file,
specified with the %GST_VALIDATE_CONFIG environment variable. Each line starting
with 'ssim,' will configure the ssim plugin. In practice each configuration statement
will lead to the creation of a #GstValidateOverride object which will then dump
image files and if wanted compare those with a set of reference images.

The following parameters can be passed in the configuration file:
 - element-classification: The target element classification as define in
   gst_element_class_set_metadata
 - output-dir: The directory in which the image files will be saved
 - min-avg-priority: (default 0.95): The minimum average similarity
   under which we consider the test as failing
 - min-lowest-priority: (default 1): The minimum 'lowest' similarity
   under which we consider the test as failing
 - reference-images-dir: Define the directory in which the files to be
   compared can be found
 - result-output-dir: The folder in which to store resulting grey scale
   images when the test failed. In that folder you will find images
   with the structural difference between the expected result and the actual
   result.
 - output-video-format: The format in which you want the images to be saved
 - reference-video-format: The format in which the reference images are stored
 - check-recurrence: The recurrence in seconds (as float) the frames should
   be dumped and checked.By default it is GST_CLOCK_TIME_NONE, meaning each
   and every frame is checked. Not that in any case, after a discontinuity
   in the stream (after a seek or a change in the video format for example)
   a check is done. And if recurrence == 0, images will be checked only after
   such discontinuity
 - is-config: Property letting the plugin know that the config line is exclusively
   used to configure the following configuration expressions. In practice this
   means that it will change the default values for the other configuration
   expressions.
 - framerate: (GstFraction): The framerate to use to compute frame number from
   timestamp, allowing to compare frames by 'frame number' instead of trying to
   match timestamp between reference images and output images.

# Example #

Let's take a special configuration where we want to compare frames that are
outputted by a video decoder with the ones after a agingtv element we would
call my_agingtv. We force to check one frame every 5.0 seconds only (with
check-recurrence=5.0) so the test is fast.

The configuration file:

``` shell
 core, action=set-property, target-element-klass=Sink, property-name=sync, property-value=false

 validatessim, is-config=true, output-video-format="I420", reference-video-format="I420"
 validatessim, element-classification="Video/Decoder", output-dir=/tmp/test/before-agingtv/
 validatessim, element-name=my_agingtv, output-dir=/tmp/test/after-agingtv/, \
       reference-images-dir=/tmp/test/before-agingtv/, \
       result-output-dir=/tmp/test/failures, check-recurrence=5.0
```

Save that content in a file called check_agingtv_ssim.config


## Launch the pipeline

``` shell
   GST_VALIDATE_CONFIG=check_agingtv_ssim.config gst-validate-1.0 uridecodebin uri=file://a/file ! videoconvert ! agingtv name=my_agingtv ! videoconvert ! autovideosink
```
