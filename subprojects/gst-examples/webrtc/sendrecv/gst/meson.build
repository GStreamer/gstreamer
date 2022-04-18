executable('webrtc-sendrecv',
           'webrtc-sendrecv.c',
           'custom_agent.h',
           'custom_agent.c',
            c_args : ['-DGST_USE_UNSTABLE_API'],
            dependencies : [gst_dep, gstsdp_dep, gstwebrtc_dep, gstrtp_dep,
            libsoup_dep, json_glib_dep, libgstwebrtcnice_dep])

webrtc_py = files('webrtc_sendrecv.py')
