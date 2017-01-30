cairo_dep = dependency('cairo', required: false)
video = static_library(
    'gstvalidatevideo',
    'gstvalidatessim.c', 'gssim.c',
    include_directories : inc_dirs,
    dependencies : [gst_dep, gst_video_dep, glib_dep, cairo_dep, gio_dep,
        mathlib],
)
