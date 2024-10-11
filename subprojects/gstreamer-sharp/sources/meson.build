raw_api_fname = join_paths(meson.current_source_dir(), meson.project_name() + '-api.raw')
metadata = files(meson.project_name() + '.metadata')

abi_includes = 'glib.h,gst/gst.h,gst/video/video.h,gst/audio/audio.h,gst/rtsp/rtsp.h,gst/app/app.h,gst/audio/audio.h,gst/base/base.h,gst/controller/controller.h,gst/fft/fft.h,gst/net/net.h,gst/pbutils/gstaudiovisualizer.h,gst/pbutils/pbutils.h,gst/rtp/rtp.h,gst/rtsp/rtsp.h,gst/sdp/sdp.h,gst/tag/tag.h,gst/video/video.h,gst/video/gstvideoaffinetransformationmeta.h,gst/net/gstnetcontrolmessagemeta.h,gst/webrtc/webrtc.h,gst/rtsp-server/rtsp-server.h'

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
    'custom/ElementFactory.cs',
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
    'custom/VideoGLUploadMeta.cs',
    'custom/Global.cs',
]

subdir('generated')

gst_sharp = shared_library('gstreamer-sharp', gst_generate_files, sources,
        cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114', '-unsafe'],
        dependencies: [glib_sharp_dep, gio_sharp_dep])

gst_sharp_dep = declare_dependency(dependencies: [glib_sharp_dep, gio_sharp_dep],
        link_with: gst_sharp)

if add_languages('c', required: get_option('tests')) and csc.get_id() == 'mono'
    c_abi_exe = executable('gst_sharp_c_abi', c_abi,
            c_args: ['-DGST_USE_UNSTABLE_API'],
            dependencies: [gst_deps])

    cs_abi_exe = executable('gst_sharp_cs_abi', cs_abi,
            cs_args: ['-nowarn:169', '-nowarn:108', '-nowarn:114', '-unsafe'],
            dependencies: [gst_sharp_dep])

    test('gstreamer_sharp_abi', diff, args: [c_abi_exe.full_path(), cs_abi_exe.full_path()],
            env: testsenv)
else
    message('Not running tests ' + csc.get_id())
endif

configure_file(
    input: 'gstreamer-sharp.dll.config',
    output: 'gstreamer-sharp.dll.config',
    copy: true)
