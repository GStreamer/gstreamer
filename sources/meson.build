raw_api_fname = join_paths(meson.current_source_dir(), meson.project_name() + '-api.raw')
metadata_fname = join_paths(meson.current_source_dir(), meson.project_name() + '.metadata')
snk = join_paths(meson.current_source_dir(), '../', meson.project_name() + '.snk')

glueincludes = 'gst/gst.h,gst/app/app.h,gst/audio/audio.h,gst/base/base.h,gst/controller/controller.h,gst/fft/fft.h,gst/net/net.h,gst/pbutils/gstaudiovisualizer.h,gst/pbutils/pbutils.h,gst/rtp/rtp.h,gst/rtsp/rtsp.h,gst/sdp/sdp.h,gst/tag/tag.h,gst/video/video.h'
gluefile = join_paths(meson.current_build_dir(), 'generate.c')

sources = [
    'custom/Adapter.cs',
    'custom/Application.cs',
    'custom/AppSink.cs',
    'custom/AppSrc.cs',
    'custom/AudioFilter.cs',
    'custom/Bin.cs',
    'custom/Buffer.cs',
    'custom/Bus.cs',
    'custom/Caps.cs',
    'custom/DeviceProvider.cs',
    'custom/DynamicSignal.cs',
    'custom/Element.cs',
    'custom/FFTF32.cs',
    'custom/Iterator.cs',
    'custom/MapInfo.cs',
    'custom/Message.cs',
    'custom/MiniObject.cs',
    'custom/NavigationAdapter.cs',
    'custom/Object.cs',
    'custom/Pad.cs',
    'custom/Pipeline.cs',
    'custom/TagList.cs',
    'custom/Value.cs',
    'custom/Version.cs',
    'custom/VideoGLUploadMeta.cs'
]

subdir('generated')

# Install with gacutil_install.py from gtk-sharp.
gst_sharp = library('gstreamer-sharp', gst_source_gen, sources,
        cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114', '-unsafe',
            '-keyfile:' + snk],
        dependencies: [glib_sharp_dep, gio_sharp_dep])

gluegen = custom_target('gluegen',
        input: raw_api_fname,
        output: 'generate.c',
        command: [generate_api, '--fakeglue'],
        depends: [gst_source_gen])

library('gstreamersharpglue-1.0.0', gluegen, 'glue/gobject.c', dependencies: gst_deps)

gst_sharp_dep = declare_dependency(dependencies: [glib_sharp_dep, gio_sharp_dep],
        link_with: gst_sharp)
meson.add_install_script(gacutil_install, meson.project_name(), gst_sharp.full_path())

configure_file(
    input: '../out/gstreamer-sharp.dll.config',
    output: 'gstreamer-sharp.dll.config',
    configuration: configuration_data())
