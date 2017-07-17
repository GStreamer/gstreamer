gl_sources = [
  'gstglapi.c',
  'gstglbasefilter.c',
  'gstglbasememory.c',
  'gstglcolorconvert.c',
  'gstglbuffer.c',
  'gstglbufferpool.c',
  'gstglcontext.c',
  'gstgldebug.c',
  'gstgldisplay.c',
  'gstglfeature.c',
  'gstglfilter.c',
  'gstglformat.c',
  'gstglframebuffer.c',
  'gstglmemory.c',
  'gstglmemorypbo.c',
  'gstgloverlaycompositor.c',
  'gstglquery.c',
  'gstglrenderbuffer.c',
  'gstglshader.c',
  'gstglshaderstrings.c',
  'gstglsl.c',
  'gstglslstage.c',
  'gstglsyncmeta.c',
  'gstglupload.c',
  'gstglutils.c',
  'gstglviewconvert.c',
  'gstglwindow.c',
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
  'gstglrenderbuffer.h',
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

gl_prototype_headers = [
  'glprototypes/all_functions.h',
  'glprototypes/base.h',
  'glprototypes/blending.h',
  'glprototypes/buffers.h',
  'glprototypes/debug.h',
  'glprototypes/eglimage.h',
  'glprototypes/fbo.h',
  'glprototypes/fixedfunction.h',
  'glprototypes/gles.h',
  'glprototypes/gstgl_compat.h',
  'glprototypes/gstgl_gles2compat.h',
  'glprototypes/Makefile.am',
  'glprototypes/opengl.h',
  'glprototypes/query.h',
  'glprototypes/README',
  'glprototypes/shaders.h',
  'glprototypes/sync.h',
  'glprototypes/vao.h',
]

gl_x11_headers = []
gl_wayland_headers = []
gl_win32_headers = []
gl_cocoa_headers = []
gl_egl_headers = []

glconf = configuration_data()
glconf_options = [
    'GST_GL_HAVE_OPENGL',
    'GST_GL_HAVE_GLES2',
    'GST_GL_HAVE_GLES3',

    'GST_GL_HAVE_WINDOW_X11',
    'GST_GL_HAVE_WINDOW_COCOA',
    'GST_GL_HAVE_WINDOW_WIN32',
    'GST_GL_HAVE_WINDOW_WAYLAND',
    'GST_GL_HAVE_WINDOW_ANDROID',
    'GST_GL_HAVE_WINDOW_DISPMANX',
    'GST_GL_HAVE_WINDOW_EAGL',
    'GST_GL_HAVE_WINDOW_VIV_FB',

    'GST_GL_HAVE_PLATFORM_EGL',
    'GST_GL_HAVE_PLATFORM_GLX',
    'GST_GL_HAVE_PLATFORM_WGL',
    'GST_GL_HAVE_PLATFORM_CGL',
    'GST_GL_HAVE_PLATFORM_EAGL',

    'GST_GL_HAVE_DMABUF',
    'GST_GL_HAVE_VIV_DIRECTVIV',

    'GST_GL_HAVE_GLEGLIMAGEOES',
    'GST_GL_HAVE_GLCHAR',
    'GST_GL_HAVE_GLSIZEIPTR',
    'GST_GL_HAVE_GLINTPTR',
    'GST_GL_HAVE_GLSYNC',
    'GST_GL_HAVE_GLUINT64',
    'GST_GL_HAVE_GLINT64',
    'GST_GL_HAVE_EGLATTRIB',
]

foreach option : glconf_options
  glconf.set10(option, false)
endforeach

gmodule_dep = dependency('gmodule-no-export-2.0')
unneeded_dep = dependency('', required : false)
if unneeded_dep.found()
  error ('Found unfindable dependency')
endif

# OpenGL/GLES2 libraries
gl_lib_deps = []
# GL platform - EGL, GLX, CGL, WGL, etc
gl_platform_deps = []
# GL winsys - wayland, X11, Cocoa, win32, etc
gl_winsys_deps = []
# other things we need.
gl_misc_deps = []
# Other preprocessor arguments
gl_cpp_args = ['-DGST_USE_UNSTABLE_API', '-DGST_EXPORTS']

enabled_gl_apis = []
enabled_gl_platforms = []
enabled_gl_winsys = []

# parse provided options
libegl_module_name = get_option('with_egl_module_name')
if libegl_module_name != ''
  gl_cpp_args += ['-DGST_GL_LIBEGL_MODULE_NAME="@0@"'.format(libegl_module_name)]
endif
libgles2_module_name = get_option('with_gles2_module_name')
if libgles2_module_name != ''
  gl_cpp_args += ['-DGST_GL_LIBGLESV2_MODULE_NAME="@0@"'.format(libgles2_module_name)]
endif
libgl_module_name = get_option('with_opengl_module_name')
if libgl_module_name != ''
  gl_cpp_args += ['-DGST_GL_LIBGL_MODULE_NAME="@0@"'.format(libgl_module_name)]
endif

gl_apis_s = get_option ('with_gl_api')
if gl_apis_s == 'auto'
  need_api_opengl = 'auto'
  need_api_gles2 = 'auto'
else
  need_api_opengl = 'no'
  need_api_gles2 = 'no'
  gl_apis = gl_apis_s.split(',')
  foreach api : gl_apis
    if api == 'opengl'
      need_api_opengl = 'yes'
    elif api == 'gles2'
      need_api_gles2 = 'yes'
    else
      error('Unsupported GL api provided ' + api)
    endif
  endforeach
endif

gl_platforms_s = get_option ('with_gl_platform')
if gl_platforms_s == 'auto'
  need_platform_egl = 'auto'
  need_platform_glx = 'auto'
  need_platform_cgl = 'auto'
  need_platform_wgl = 'auto'
  need_platform_eagl = 'auto'
else
  need_platform_egl = 'no'
  need_platform_glx = 'no'
  need_platform_cgl = 'no'
  need_platform_wgl = 'no'
  need_platform_eagl = 'no'
  gl_platforms = gl_platforms_s.split(',')
  foreach platform : gl_platforms
    if platform == 'egl'
      need_platform_egl = 'yes'
    elif platform == 'glx'
      need_platform_glx = 'yes'
#    elif platform == 'cgl'
#      need_platform_cgl = 'yes'
    elif platform == 'wgl'
      need_platform_wgl = 'yes'
#    elif platform == 'eagl'
#      need_platform_eagl = 'yes'
    else
      error('Unsupported GL platform provided ' + platform)
    endif
  endforeach
endif

gl_winsys_s = get_option ('with_gl_winsys')
if gl_winsys_s == 'auto'
  need_win_x11 = 'auto'
  need_win_wayland = 'auto'
  need_win_win32 = 'auto'
  need_win_cocoa = 'auto'
  need_win_eagl = 'auto'
  need_win_dispmanx = 'auto'
else
  need_win_x11 = 'no'
  need_win_wayland = 'no'
  need_win_win32 = 'no'
  need_win_cocoa = 'no'
  need_win_eagl = 'no'
  need_win_dispmanx = 'no'
  gl_winsys = gl_winsys_s.split(',')
  foreach winsys : gl_winsys
    if winsys == 'x11'
      need_win_x11 = 'yes'
    elif winsys == 'wayland'
      need_win_wayland = 'yes'
    elif winsys == 'win32'
      need_win_win32 = 'yes'
#    elif winsys == 'cocoa'
#      need_win_cocoa = 'yes'
#    elif winsys == 'eagl'
#      need_win_eagl = 'yes'
    elif winsys == 'dispmanx'
      need_win_dispmanx = 'yes'
    else
      error('Unsupported GL winsys provided ' + winsys)
    endif
  endforeach
endif

gl_include_header = '''
#ifdef __GNUC__
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wredundant-decls"
#endif
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif
'''

# Desktop OpenGL checks
gl_dep = unneeded_dep
glx_dep = unneeded_dep
if need_api_opengl != 'no' or need_platform_glx != 'no'
  gl_dep = dependency('gl', required : false)
  if not gl_dep.found()
    if host_machine.system() == 'windows'
      gl_dep = cc.find_library('opengl32', required : false)
#    elif host_machine.system() == 'darwin'
#      gl_dep = cc.find_library('OpenGL', required : false)
    else
      gl_dep = cc.find_library('GL', required : false)
    endif

    if not gl_dep.found() and need_api_opengl == 'yes'
      error ('Could not find requested OpenGL library')
    endif
  endif

  glx_dep = gl_dep
  if need_api_opengl == 'no'
    gl_dep = unneeded_dep
  endif
  if need_platform_glx == 'no'
    glx_dep = unneeded_dep
  endif

  opengl_includes = '''
#ifdef __APPLE__
# include <OpenGL/OpenGL.h>
# include <OpenGL/gl.h>
# if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
#  define GL_DO_NOT_WARN_IF_MULTI_GL_VERSION_HEADERS_INCLUDED
#  include <OpenGL/gl3.h>
# endif
#else
# if _MSC_VER
#  include <windows.h>
# endif
# include <GL/gl.h>
# if __WIN32__ || _WIN32
#  include <GL/glext.h>
# endif
#endif
'''
endif

# GLES2 checks
gles2_dep = unneeded_dep
if need_api_gles2 != 'no'
  gles2_dep = dependency('glesv2', required : false)
  if not gles2_dep.found()
#    if host_machine.system() == 'windows'
#    elif host_machine.system() == 'darwin'
#      gles2_dep = cc.find_library('GLESv2', required : false)
#    else
      gles2_dep = cc.find_library('GLESv2', required : false)
#    endif

     if not cc.has_header('GLES2/gl2.h', required : false)
       gles2_dep = unneeded_dep
     endif

    if not gles2_dep.found() and need_api_gles2 == 'yes'
      error ('Could not find requested OpenGL ES library')
    endif
  endif

  gles3_h = gles2_dep.found() and cc.has_header('GLES3/gl3.h', required : false, dependencies : gles2_dep)

  gles_includes = '''
#ifdef HAVE_IOS /* FIXME */
# include <OpenGLES/ES2/gl.h>
# include <OpenGLES/ES2/glext.h>
#else'''
  if gles3_h
    gles_includes += '''
# include <GLES3/gl3.h>
# include <GLES3/gl3ext.h>'''
  else
    gles_includes += '''
# include <GLES2/gl2.h>
# include <GLES2/gl2ext.h>'''
  endif
  gles_includes += '''
#endif
'''
endif

# can we include both gles2 and opengl headers?
if gles2_dep.found() and gl_dep.found()
  gl_include_block = gl_include_header + gles_includes + opengl_includes
  if not cc.compiles('void f (void) {}', prefix : gl_include_block, dependencies : [gles2_dep, gl_dep] )
    message ('Cannot include both OpenGL and OpenGL ES headers')
    if need_gles2 != 'yes'
      gles2_dep = unneeded_dep
    elif need_opengl != 'yes'
      gl_dep = unneeded_dep
    else
      error('Both OpenGL and OpenGL ES were requested but cannot be included together')
    endif
  endif
endif
gl_include_block = gl_include_header
if gles2_dep.found()
  gl_include_block += gles_includes
endif
if gl_dep.found()
  gl_include_block += opengl_includes
endif

if gles2_dep.found()
  gl_lib_deps += gles2_dep
  glconf.set10('GST_GL_HAVE_GLES2', 1)
  if gles3_h
    glconf.set10('GST_GL_HAVE_GLES3', 1)
  endif
  enabled_gl_apis += 'gles2'
endif

if gl_dep.found()
  gl_lib_deps += gl_dep
  glconf.set10('GST_GL_HAVE_OPENGL', 1)
  enabled_gl_apis += 'opengl'
endif

# EGL checks
egl_dep = unneeded_dep
if need_platform_egl != 'no'
  egl_dep = dependency('egl', required : false)
  if not egl_dep.found()
    egl_dep = cc.find_library('EGL', required : false)

    if not egl_dep.found() and need_platform_egl == 'yes'
      error ('Could not find requested EGL library')
    endif
  endif

  if egl_dep.found()
    gl_sources += [
      'egl/gstegl.c',
      'egl/gsteglimage.c',
      'egl/gstglcontext_egl.c',
      'egl/gstgldisplay_egl.c',
      'egl/gstglmemoryegl.c',
    ]
    gl_egl_headers += [
      'egl/gstegl.h',
      'egl/gsteglimage.h',
      'egl/gstgldisplay_egl.h',
      'egl/gstglmemoryegl.h',
    ]
    gl_platform_deps += egl_dep
    glconf.set10('GST_GL_HAVE_PLATFORM_EGL', 1)

    if cc.has_header('libdrm/drm_fourcc.h', required : false)
      gl_misc_deps += gstallocators_dep
      glconf.set10('GST_GL_HAVE_DMABUF', 1)
    endif

    egl_includes = '''
#include <EGL/egl.h>
#include <EGL/eglext.h>
'''
    enabled_gl_platforms += 'egl'
  endif

endif

# wayland checks
wayland_client_dep = unneeded_dep
wayland_cursor_dep = unneeded_dep
wayland_egl_dep = unneeded_dep
if need_win_wayland != 'no'
  if need_win_wayland == 'yes'
    if need_platform_egl == 'no'
      error('Impossible situation requested: Cannot use Wayland without EGL support')
    endif
  endif
  if not egl_dep.found()
    if need_win_wayland == 'yes'
      error ('Could not find EGL libraries for wayland')
    else
      message ('Could not find EGL libraries for wayland')
    endif
  else
    wayland_client_dep = dependency('wayland-client', version : '>= 1.0', required : false)
    wayland_cursor_dep = dependency('wayland-cursor', version : '>= 1.0', required : false)
    wayland_egl_dep = dependency('wayland-egl', version : '>= 1.0', required : false)

    if wayland_client_dep.found() and wayland_cursor_dep.found() and wayland_egl_dep.found()
      gl_sources += [
        'wayland/gstgldisplay_wayland.c',
        'wayland/gstglwindow_wayland_egl.c',
        'wayland/wayland_event_source.c',
      ]
      gl_wayland_headers += [
        'wayland/gstgldisplay_wayland.h'
      ]
      glconf.set('GST_GL_HAVE_WINDOW_WAYLAND', 1)
      gl_winsys_deps += [wayland_client_dep, wayland_cursor_dep, wayland_egl_dep]
      enabled_gl_winsys += 'wayland'
    else
      if need_win_wayland == 'yes'
        error ('Could not find requested Wayland libraries')
      endif
      wayland_client_dep = unneeded_dep
      wayland_cursor_dep = unneeded_dep
      wayland_egl_dep = unneeded_dep
    endif
  endif
endif

# X11 checks
if need_platform_glx == 'yes'
  if need_win_x11 == 'no'
    error('Impossible situation requested: Cannot use GLX without X11 support')
  elif need_api_opengl == 'no'
    error('Impossible situation requested: Cannot use GLX without the OpenGL library')
  endif
endif

if need_win_x11 != 'no'
  xcb_dep = dependency('x11-xcb', required : false)
  if x11_dep.found() and xcb_dep.found()
    gl_sources += [
      'x11/gstgldisplay_x11.c',
      'x11/gstglwindow_x11.c',
      'x11/xcb_event_source.c',
    ]
    gl_x11_headers += [
      'x11/gstgldisplay_x11.h',
    ]
    glconf.set('GST_GL_HAVE_WINDOW_X11', 1)
    gl_winsys_deps += [x11_dep, xcb_dep]
    enabled_gl_winsys += 'x11'

    if need_platform_glx != 'no' and glx_dep.found()
      glconf.set('GST_GL_HAVE_PLATFORM_GLX', 1)
      gl_sources += [
        'x11/gstglcontext_glx.c',
      ]
      # GLX is in the opengl library on linux
      gl_platform_deps += glx_dep
      enabled_gl_platforms += 'glx'
    endif
  elif need_win_x11 == 'yes'
    error ('Could not find requested X11 libraries')
  endif
endif

bcm_host_dep = unneeded_dep
if need_win_dispmanx != 'no'
  bcm_host_dep = dependency('bcm_host', required : false)
  if bcm_host_dep.found()
    if not egl_dep.found()
      error('dispmanx requires the use of egl')
    endif

    gl_sources += [
      'dispmanx/gstglwindow_dispmanx_egl.c'
    ]

    glconf.set('GST_GL_HAVE_WINDOW_DISPMANX', 1)
    gl_winsys_deps += bcm_host_dep
    enabled_gl_winsys += 'dispmanx'
    gl_cpp_args += ['-DUSE_EGL_RPI']
  elif need_win_dispmanx == 'yes'
    error('Could not find dispmanx libraries')
  endif
endif

# win32 checks
if need_platform_wgl == 'yes'
  if need_win_win32 == 'no'
    error('Impossible situation requested: Cannot use WGL without the win32 window system')
  endif
endif

# XXX: untested
if need_platform_wgl != 'no' and need_win_win32 != 'no'
  gdi_dep = cc.find_library('gdi32', required : false)
  # FIXME: Revert back to has_header once it gains prefix support
  wglext_h = cc.has_header_symbol('GL/wglext.h', 'WGL_WGLEXT_VERSION',
                                  prefix : '#include <windows.h>
                                            #include <GL/gl.h>')

  if wglext_h and gdi_dep.found() and gl_dep.found()
    gl_platform_deps += gdi_dep
    gl_sources += [
      'win32/win32_message_source.c',
      'win32/gstglwindow_win32.c',
      'win32/gstglwindow_win32.c',
    ]
    enabled_gl_winsys += 'win32'
    gl_winsys_deps += gdi_dep
    enabled_gl_platforms += 'wgl'
  endif
endif

if host_machine.system() == 'darwin'
  # FIXME: how to know if we're on iOS or OS X?
#  gl_cocoa_headers += [
#    'gstglcontext_cocoa.h',
#    'gstglcaopengllayer.h',
#  ]
endif

# TODO: Add rest of gl config here.
# iOS, OS X, win32 specific support

build_gstgl = true
if gl_lib_deps.length() == 0
  message('No OpenGL API libraries found or requested')
  build_gstgl = false
endif
if gl_platform_deps.length() == 0
  message('No OpenGL Platforms found or requested')
  build_gstgl = false
endif
if gl_winsys_deps.length() == 0
  message('No OpenGL Window systems found or requested')
  build_gstgl = false
endif

if build_gstgl
  # find some types that may or may not be defined
  if cc.has_type('GLeglImageOES', prefix : gl_include_block, dependencies : gl_lib_deps)
    glconf.set('GST_GL_HAVE_GLEGLIMAGEOES', 1)
  endif
  if cc.has_type('GLchar', prefix : gl_include_block, dependencies : gl_lib_deps)
    glconf.set('GST_GL_HAVE_GLCHAR', 1)
  endif
  if cc.has_type('GLsizeiptr', prefix : gl_include_block, dependencies : gl_lib_deps)
    glconf.set('GST_GL_HAVE_GLSIZEIPTR', 1)
  endif
  if cc.has_type('GLintptr', prefix : gl_include_block, dependencies : gl_lib_deps)
    glconf.set('GST_GL_HAVE_GLINTPTR', 1)
  endif
  if cc.has_type('GLsync', prefix : gl_include_block, dependencies : gl_lib_deps)
    glconf.set('GST_GL_HAVE_GLSYNC', 1)
  endif
  if cc.has_type('GLuint64', prefix : gl_include_block, dependencies : gl_lib_deps)
    glconf.set('GST_GL_HAVE_GLUINT64', 1)
  endif
  if cc.has_type('GLint64', prefix : gl_include_block, dependencies : gl_lib_deps)
    glconf.set('GST_GL_HAVE_GLINT64', 1)
  endif
  if egl_dep.found() and cc.has_type('EGLAttrib', prefix : gl_include_block + egl_includes, dependencies : gl_lib_deps + [egl_dep])
    glconf.set('GST_GL_HAVE_EGLATTRIB', 1)
  endif

  message('Building libgstgl with GL api:      ' + ' '.join(enabled_gl_apis))
  message('Building libgstgl with GL platform: ' + ' '.join(enabled_gl_platforms))
  message('Building libgstgl with GL winsys:   ' + ' '.join(enabled_gl_winsys))

  install_headers(gl_headers, subdir : 'gstreamer-1.0/gst/gl')
  install_headers(gl_cocoa_headers, subdir : 'gstreamer-1.0/gst/gl/cocoa')
  install_headers(gl_egl_headers, subdir : 'gstreamer-1.0/gst/gl/egl')
  install_headers(gl_prototype_headers, subdir : 'gstreamer-1.0/gst/gl/glprototypes')
  install_headers(gl_x11_headers, subdir : 'gstreamer-1.0/gst/gl/x11')
  install_headers(gl_wayland_headers, subdir : 'gstreamer-1.0/gst/gl/wayland')

  configure_file(input : 'gstglconfig.h.meson',
    output : 'gstglconfig.h',
    install_dir : get_option('libdir') + '/gstreamer-1.0/include/gst/gl',
    configuration : glconf)

  gstgl = library('gstgl-' + api_version,
    gl_sources,
    c_args : gst_plugins_bad_args + gl_cpp_args,
    include_directories : [configinc, libsinc],
    version : libversion,
    soversion : soversion,
    install : true,
    dependencies : [gstbase_dep, gstvideo_dep, gmodule_dep,
                    gl_lib_deps, gl_platform_deps, gl_winsys_deps, gl_misc_deps],
    # Don't need vs_module_defs for gstgl because we do symbol exporting in the
    # function definitions via __declspec(dllexport/import)
  )
  gen_sources = []
  if build_gir
    gl_gir = gnome.generate_gir(gstgl,
      sources : gl_sources + gl_headers,
      namespace : 'GstGL',
      nsversion : api_version,
      identifier_prefix : 'Gst',
      symbol_prefix : 'gst',
      export_packages : 'gstreamer-gl-1.0',
      includes : ['Gst-1.0', 'GstBase-1.0', 'GstVideo-1.0'],
      install : true,
      extra_args : gir_init_section + ['-DGST_USE_UNSTABLE_API',
        '--c-include=gst/gl/gl.h'],
      dependencies : [gst_dep, gstbase_dep, gstvideo_dep]
    )
    gen_sources += gl_gir
  endif


  gstgl_dep = declare_dependency(link_with : gstgl,
    include_directories : [libsinc],
    sources: gen_sources,
    dependencies : [gstbase_dep, gstvideo_dep] + gl_winsys_deps)
endif
