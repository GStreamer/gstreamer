validate_video_dep = dependency('', required: false)
cairo_dep = dependency('cairo-png', required: get_option('cairo'), fallback: 'cairo')

if cairo_dep.found()
    video = static_library(
        'gstvalidatevideo',
        'gstvalidatessim.c', 'gssim.c',
        include_directories : inc_dirs,
        dependencies : [gst_dep, gst_video_dep, gst_pbutils_dep, cairo_dep, gio_dep,
            mathlib],
    )
    validate_video_dep = declare_dependency(
        link_with : video,
        include_directories : inc_dirs,
        dependencies : [gst_dep, gst_video_dep, gst_pbutils_dep, cairo_dep, gio_dep,
            mathlib],
    )
endif
