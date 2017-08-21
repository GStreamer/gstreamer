project('gstreamer-sharp', ['cs', 'c'], version: '1.12.0',
    meson_version: '>0.42')


if host_machine.system() == 'osx'
  lib_prefix=''
  lib_suffix='.dylib'
else
  lib_prefix='.so'
  lib_suffix=''
endif

gtk_sharp_required_version = '>=3.22.6'

glib_sharp_dep = dependency('glib-sharp-3.0',
    version: gtk_sharp_required_version,
    fallback: ['gtk-sharp', 'glib_sharp_dep'])

gio_sharp_dep = dependency('gio-sharp-3.0',
    version: gtk_sharp_required_version,
    fallback: ['gtk-sharp', 'gio_sharp_dep'])

if glib_sharp_dep.type_name() == 'internal'
  gtk_sharp = subproject('gtk-sharp')
  gapi_fixup = gtk_sharp.get_variable('gapi_fixup')
  gapi_codegen = gtk_sharp.get_variable('gapi_codegen')
  glib_api_includes = gtk_sharp.get_variable('glib_api_includes')
  gio_api_includes = gtk_sharp.get_variable('gio_api_includes')
  has_gtk = gtk_sharp.get_variable('has_gtk')
  if has_gtk
    gtk_sharp_dep = [gtk_sharp.get_variable('gtk_sharp_dep'),
      gtk_sharp.get_variable('atk_sharp_dep')]
  else
    gtk_sharp_dep = dependency('gtk-sharp-3.0', version: gtk_sharp_required_version,
        required: false)
  endif
  codegen_dependencies = [gapi_codegen, gapi_fixup]
  gapi_fixup = gapi_fixup.full_path()
  gapi_codegen = gapi_codegen.full_path()
else
  gtk_sharp_dep = dependency('gtk-sharp-3.0', version: gtk_sharp_required_version,
      required: false)
  has_gtk = gtk_sharp_dep.found()

  gapi_fixup = find_program('gapi3-fixup')
  gapi_codegen = find_program('gapi3-codegen')
  pkgconfig = find_program('pkg-config')
  glib_api_includes = run_command(pkgconfig, '--cflags', 'glib-sharp-3.0').stdout()
  gio_api_includes = run_command(pkgconfig, '--cflags', 'gio-sharp-3.0').stdout()
  codegen_dependencies = []
endif

gacutil = find_program('gacutil')
generate_api = find_program('generate_code.py')
gacutil_install = join_paths(meson.current_source_dir(), 'gacutil_install.py')

# TODO Handle monodoc

apiversion = '1.0'
gst_required_version = '>=@0@'.format(meson.project_version())
gst_deps = []

foreach dep: [
  ['gstreamer', ['gstreamer', 'gst_dep']],
  ['gstreamer-app', ['gst-plugins-base', 'app_dep']],
  ['gstreamer-audio', ['gst-plugins-base', 'audio_dep']],
  ['gstreamer-base', ['gst-plugins-base', 'audio_dep']],
  ['gstreamer-controller', ['gstreamer', 'gst_controller_dep']],
  ['gstreamer-fft', ['gst-plugins-base', 'fft_dep']],
  ['gstreamer-net', ['gstreamer', 'gst_net_dep']],
  ['gstreamer-pbutils', ['gst-plugins-base', 'pbutils_dep']],
  ['gstreamer-riff', ['gst-plugins-base', 'riff_dep']],
  ['gstreamer-rtp', ['gst-plugins-base', 'rtp_dep']],
  ['gstreamer-rtsp', ['gst-plugins-base', 'rtsp_dep']],
  ['gstreamer-sdp', ['gst-plugins-base', 'sdp_dep']],
  ['gstreamer-tag', ['gst-plugins-base', 'tag_dep']],
  ['gstreamer-video', ['gst-plugins-base', 'video_dep']],]

  gst_deps += [dependency(dep.get(0) + '-' + apiversion, version: gst_required_version,
    fallback: dep.get(1))]
endforeach

subdir('sources')
subdir('samples')

bindinate = find_program('bindinate', required: false)

if bindinate.found()
  run_target('update-bindings',
    command: [bindinate,
      '--name=gstreamer', '--regenerate=true',
      '--merge-with=GstApp-1.0,GstAudio-1.0,GstBase-1.0,GstController-1.0,GstFft-1.0,GstNet-1.0,GstPbutils-1.0,GstRiff-1.0,GstRtp-1.0,GstRtsp-1.0,GstSdp-1.0,GstTag-1.0,GstVideo-1.0',
      '--gir=Gst-1.0']
    )
endif
