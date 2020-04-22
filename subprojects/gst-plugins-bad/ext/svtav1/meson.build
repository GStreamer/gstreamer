# standalone plugin meson configuration
project('gst-svt-av1', 'c',
  version : '0.1',
  meson_version : '>= 0.29',
  default_options : [ 'buildtype=debugoptimized' ])

# standard gst-plugins-bad dependencies and configuration
gst_req = '>= 1.13.1'
gst_dep = dependency('gstreamer-1.0', version : gst_req,
  fallback : ['gstreamer', 'gst_dep'])
gstbase_dep = dependency('gstreamer-base-1.0', version : gst_req,
  fallback : ['gstreamer', 'gstbase_dep'])
gstvideo_dep = dependency('gstreamer-video-1.0', version : gst_req,
  fallback : ['gstreamer', 'gstvideo_dep'])
plugins_install_dir = '@0@/gstreamer-1.0'.format(get_option('libdir'))

# common meson configuration
svtav1enc_dep = dependency('SvtAv1Enc')

cc = meson.get_compiler('c')
cc_flags = [
    '-O2',
    '-D_FORTIFY_SOURCE=2',
    '-Wformat',
    '-Wformat-security',
    '-fPIE',
    '-fPIC',
    '-fstack-protector-strong',
]
foreach flag: cc_flags
    if cc.has_argument(flag)
        add_global_arguments(flag, language: 'c')
    endif
endforeach


ldflags = [
   '-Wl,-z,now',
   '-Wl,-z,relro',
   '-Wl,-z,noexecstack',
]
foreach lflag : ldflags
    add_global_link_arguments(lflag, language : 'c')
endforeach


if svtav1enc_dep.found()
    gstsvtav1enc = library('gstsvtav1enc',
        'gstsvtav1enc.c',
        dependencies: [gstbase_dep, gstvideo_dep, svtav1enc_dep],
        install: true,
        install_dir: plugins_install_dir,
    )
endif
