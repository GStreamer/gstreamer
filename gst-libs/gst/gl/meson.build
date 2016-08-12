gl_sources = [
  'gstgldisplay.c',
  'gstglcontext.c',
  'gstglmemory.c',
  'gstglbufferpool.c',
  'gstglfilter.c',
  'gstglshader.c',
  'gstglshadervariables.c',
  'gstglcolorconvert.c',
  'gstgldownload.c',
  'gstglupload.c',
  'gstgluploadmeta.c',
  'gstglwindow.c',
  'gstglapi.c',
  'gstglfeature.c',
  'gstglutils.c',
  'gstglframebuffer.c',
]

gl_headers = [
  'gl.h',
  'gstgl_enums.h',
  'gstgl_fwd.h',
  'gstglapi.h',
  'gstglbasefilter.h',
  'gstglbasememory.h',
  'gstglbuffer.h',
  'gstglbufferpool.h',
  'gstglcolorconvert.h',
  'gstglcontext.h',
  'gstglcontrolbindingproxy.h',
  'gstgldebug.h',
  'gstgldisplay.h',
  'gstglfeature.h',
  'gstglfilter.h',
  'gstglformat.h',
  'gstglframebuffer.h',
  'gstglmemory.h',
  'gstglmemorypbo.h',
  'gstgloverlaycompositor.h',
  'gstglquery.h',
  'gstglshader.h',
  'gstglshaderstrings.h',
  'gstglsl.h',
  'gstglslstage.h',
  'gstglsyncmeta.h',
  'gstglupload.h',
  'gstglutils.h',
  'gstglviewconvert.h',
  'gstglwindow.h',
]
# FIXME: Install glprototypes
install_headers(gl_headers, subdir : 'gstreamer-1.0/gst/gl')

glconf = configuration_data()

glconf.set10('GST_GL_HAVE_OPENGL', gl_dep.found())

if x11_dep.found()
  gl_sources += [
    'x11/gstgldisplay_x11.c',
    'x11/gstglwindow_x11.c',
    'x11/x11_event_source.c',
    'x11/gstglcontext_glx.c',
]
  glconf.set('GST_GL_HAVE_WINDOW_X11', 1)
  glconf.set('GST_GL_HAVE_PLATFORM_GLX', 1)
endif

# Add rest of gl config here.

configure_file(input : 'gstglconfig.h.meson',
  output : 'gstglconfig.h',
  install_dir : get_option('libdir') + '/gstreamer-1.0/include/gst/gl',
  configuration : glconf)

gstgl = library('gstgl-' + api_version,
  gl_sources,
  c_args : gst_plugins_bad_args + ['-DGST_USE_UNSTABLE_API'],
  include_directories : [configinc, libsinc],
  version : libversion,
  soversion : soversion,
  install : true,
  dependencies : [gstbase_dep, gstvideo_dep],
  # FIXME: This symbol list is generated on Linux, so this is wrong for Windows
  vs_module_defs: vs_module_defs_dir + 'libgstgl.def',
)
# TODO: generate gir

gstgl_dep = declare_dependency(link_with : gstgl,
  include_directories : [libsinc],
  dependencies : [gstbase_dep, gstvideo_dep])
