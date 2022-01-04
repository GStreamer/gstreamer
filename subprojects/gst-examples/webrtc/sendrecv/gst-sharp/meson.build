project('gstreamer-sharp', ['cs'], meson_version: '>=0.47.0', license: 'LGPL')
gstreamer_version = '1.14.0'


mono_path = ''
nuget = find_program('nuget.py')

dependencies = []
foreach dependency, version: { 'Newtonsoft.Json': '11.0.2', 'WebSocketSharp': '1.0.3-rc11'}
    message('Getting @0@:@1@'.format(dependency, version))
    get_dep = run_command(nuget, 'get',
        '--builddir',  dependency,
        '--nuget-name', dependency,
        '--nuget-version', version,
        '--csharp-version=net45',
        '--current-builddir', meson.current_build_dir(),
        '--builddir', meson.global_build_root(),  # FIXME: --builddir specified twice?!
        check: true,
    )

    link_args = get_dep.stdout().split()
    dependencies += [declare_dependency(link_args: link_args, version: version)]
    foreach path: get_dep.stdout().split()
        mono_path += ':@0@'.format(join_paths(meson.build_root(), path.strip('-r:'), '..'))
    endforeach
endforeach

# Use nugget once 1.16 is released.
dependencies += [dependency('gstreamer-sharp-1.0', fallback: ['gstreamer-sharp', 'gst_sharp_dep'])]

message('Execute with MONO_PATH=@0@:$MONO_PATH @1@/WebRTCSendRecv.exe'.format(mono_path, meson.current_build_dir()))
executable('WebRTCSendRecv', 'WebRTCSendRecv.cs',
    cs_args: ['-unsafe'], dependencies: dependencies)
