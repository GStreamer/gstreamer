project('gstreamer-sharp', ['cs', 'c'], version: '1.23.2.1',
    meson_version : '>= 1.1', license: 'LGPL')

fs = import ('fs')

if host_machine.system() == 'osx'
  lib_prefix=''
  lib_suffix='.dylib'
else
  lib_prefix='.so'
  lib_suffix=''
endif

gtk_sharp_required_version = '>=3.22.6'

csc = meson.get_compiler('cs')
if csc.get_id() == 'mono'
    add_project_arguments('-keyfile:' + join_paths(meson.current_source_dir(), meson.project_name() + '.snk'),
        language: ['cs'])
endif


subproject('gtk-sharp', default_options: ['install=false'])
gtk_sharp = subproject('gtk-sharp')

glib_sharp_dep = gtk_sharp.get_variable('glib_sharp_dep')
glib_sharp = gtk_sharp.get_variable('glib_sharp')

gio_sharp_dep = gtk_sharp.get_variable('gio_sharp_dep')
gio_sharp = gtk_sharp.get_variable('gio_sharp')

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

mono_path = [gtk_sharp.get_variable('mono_path'), join_paths(meson.current_build_dir(), 'sources')]

codegen_dependencies = [gapi_codegen, gapi_fixup, glib_sharp, gio_sharp]
gapi_fixup = gapi_fixup.full_path()
gapi_codegen = gapi_codegen.full_path()

gacutil = find_program('gacutil')
generate_api = find_program('generate_code.py')
nuget = find_program('nuget.py')
diff = find_program('Tests/test_abi.py')

# TODO Handle monodoc

apiversion = '1.0'
gst_required_version = '>=@0@'.format(meson.project_version())
gst_deps = []

# dependency name, [subproject-name, internal-dep-name, library-name']
gst_deps_defs = [
  ['gstreamer', ['gstreamer', 'gst_dep'], 'libgst'],
  ['gstreamer-app', ['gst-plugins-base', 'app_dep'], 'gstapp'],
  ['gstreamer-audio', ['gst-plugins-base', 'audio_dep'], 'gstaudio'],
  ['gstreamer-base', ['gstreamer', 'gst_base_dep'], 'gst_base'],
  ['gstreamer-controller', ['gstreamer', 'gst_controller_dep'], 'gst_controller'],
  ['gstreamer-fft', ['gst-plugins-base', 'fft_dep'], 'gstfft'],
  ['gstreamer-net', ['gstreamer', 'gst_net_dep'], 'gst_net'],
  ['gstreamer-pbutils', ['gst-plugins-base', 'pbutils_dep'], 'pbutils'],
  ['gstreamer-riff', ['gst-plugins-base', 'riff_dep'], 'gstriff'],
  ['gstreamer-rtp', ['gst-plugins-base', 'rtp_dep'], 'gst_rtp'],
  ['gstreamer-rtsp', ['gst-plugins-base', 'rtsp_dep'], 'gst_rtsp'],
  ['gstreamer-sdp', ['gst-plugins-base', 'sdp_dep'], 'gstsdp'],
  ['gstreamer-tag', ['gst-plugins-base', 'tag_dep'], 'gsttag'],
  ['gstreamer-video', ['gst-plugins-base', 'video_dep'], 'gstvideo'],
  ['gstreamer-webrtc', ['gst-plugins-bad', 'gstwebrtc_dep'], 'gstwebrtc'],
]

foreach dep: gst_deps_defs
  gst_deps += [dependency(dep.get(0) + '-' + apiversion, version: gst_required_version,
    fallback: dep.get(1))]
endforeach

ges_dep = dependency('gst-editing-services-' + apiversion, version: gst_required_version,
    fallback: ['gst-editing-services', 'ges_dep'], required: false)

if ges_dep.found()
    gst_deps_defs +=  [
        ['gst-editing-services', ['gst-editing-services', 'ges_dep'], 'libges'],
    ]
    mono_path += [join_paths(meson.current_build_dir(), 'ges')]
endif

testsenv = environment()
testsenv.prepend('MONO_PATH', mono_path)
i = 0
testsenv_ld_library_path = []
foreach dep: gst_deps + [ges_dep]
    if dep.type_name() == 'pkgconfig'
        testsenv_ld_library_path += [dep.get_variable('libdir')]
    else
        depdef = gst_deps_defs[i][1]
        libname = gst_deps_defs[i].get(2, '')
        if libname != ''
            proj = subproject(depdef[0])
            libpath = proj.get_variable(libname).full_path().split('/')
            dirname = ''
            j = 1
            foreach comp: libpath
                if j < libpath.length()
                    dirname += '/' + comp
                endif
                j += 1
            endforeach
            testsenv_ld_library_path += [dirname]
        endif
    endif
    i += 1
endforeach
# FIXME: port this to macOS and Windows
testsenv.prepend('LD_LIBRARY_PATH', testsenv_ld_library_path)

subdir('sources')
if ges_dep.found()
  subdir('ges')
  custom_target('GESSharp-nuget',
      command: [nuget, '--package-name', 'GESSharp',
          '--assembly', ges_sharp.full_path(),
          '--dependency=GstSharp:' + meson.project_version(),
          '--project-url', 'https://gstreamer.freedesktop.org/modules/gstreamer-sharp.html',
          '--icon-url', 'https://gstreamer.freedesktop.org/images/header-logo-top.png',
          '--license-url', 'https://cgit.freedesktop.org/gstreamer/gstreamer-sharp/plain/COPYING',
          '--version', meson.project_version(),
          '--tags', 'GStreamer GES GstSharp bindings',
          '--builddir', meson.current_build_dir(),
          '--dependency', 'GstSharp:' + meson.project_version(),
      ],
      output: 'GESSharp.' + meson.project_version() + '.nupkg'
  )
endif

custom_target('GstSharp-nuget',
    command: [nuget, '--package-name', 'GstSharp',
        '--assembly', gst_sharp.full_path(),
        '--assembly', gio_sharp.full_path(),
        '--assembly', glib_sharp.full_path(),
        '--project-url', 'https://gstreamer.freedesktop.org/modules/gstreamer-sharp.html',
        '--icon-url', 'https://gstreamer.freedesktop.org/images/header-logo-top.png',
        '--license-url', 'https://cgit.freedesktop.org/gstreamer/gstreamer-sharp/plain/COPYING',
        '--version', meson.project_version(),
        '--tags', 'GStreamer GstSharp bindings',
        '--builddir', meson.current_build_dir(),
    ],
    output: 'GstSharp.' + meson.project_version() + '.nupkg',
)

subdir('samples')
subdir('Tests')

bindinate = find_program('bindinate', required: false)
girs_dir = join_paths(meson.global_source_root(), 'girs')

codegen_enabled = false
if not bindinate.found()
    warning('Bindinator not usable as some required dependencies are not avalaible.')
elif not fs.is_dir(girs_dir)
    message('Code generation disabled, gstreamer-sharp must be a GStreamer subproject.')
else
    codegen_enabled = true
endif

if codegen_enabled
    env = environment()
    env.set('GI_TYPELIB_PATH', girs_dir)
    merge_with = '--merge-with=GstApp-1.0,GstAudio-1.0,GstBase-1.0,GstController-1.0,GstNet-1.0,GstPbutils-1.0,GstRtp-1.0,GstRtsp-1.0,GstSdp-1.0,GstTag-1.0,GstVideo-1.0,GstWebRTC-1.0'
    run_target('bindinate_gstreamer',
        command: ['sh', '-c',
            '''@0@ --name=gstreamer --regenerate=true @3@ --gir=Gst-1.0 --out=@1@ && mv @1@/sources/gstreamer-sharp-api.raw @2@/sources/gstreamer-sharp-api.raw '''.format(
            bindinate.full_path(),
            meson.current_build_dir(),
            meson.current_source_dir(),
            merge_with
            ),
        ],
        depends: [],
        env:env,
    )
    run_target('bindinate_ges',
        command: ['sh', '-c',
            '''@0@ --name=gst-editing-services --regenerate=true --gir=GES-1.0 --out=@1@ && mv @1@/sources/gst-editing-services-sharp-api.raw @2@/ges/gst-editing-services-api.raw '''.format(
            bindinate.full_path(),
            meson.current_build_dir(),
            meson.current_source_dir()
            ),
        ],
        depends: [],
        env:env,
    )
    python3 = import('python3').find_python()
    run_target('update-code', command: [find_program('update_sources.py')])
    run_target('update-all', command: [find_program('update_sources.py'), 'bindinate'])
endif

meson.add_dist_script('scripts/gen-changelog.py', meson.project_name(), '1.22.0', meson.project_version())
