examples = [
    ['playback', 'Playback.cs'],
    ['video-overlay' , 'VideoOverlay.cs', has_gtk, gtk_sharp_dep],
    ['basic-tutorial-1' , 'BasicTutorial1.cs',],
    ['basic-tutorial-2' , 'BasicTutorial2.cs',],
    ['basic-tutorial-3' , 'BasicTutorial3.cs',],
    ['basic-tutorial-4' , 'BasicTutorial4.cs',],
    ['basic-tutorial-5' , 'BasicTutorial5.cs', has_gtk, gtk_sharp_dep],
    ['basic-tutorial-6' , 'BasicTutorial6.cs',],
    ['basic-tutorial-7' , 'BasicTutorial7.cs',],
    ['basic-tutorial-8' , 'BasicTutorial8.cs',],
    ['basic-tutorial-9' , 'BasicTutorial9.cs',],
    ['basic-tutorial-12' , 'BasicTutorial12.cs',],
    ['basic-tutorial-13' , 'BasicTutorial13.cs',],
    ['example-volume' , 'ExampleVolume.cs',],
    ['playback-tutorial-1' , 'PlaybackTutorial1.cs',],
    ['playback-tutorial-2' , 'PlaybackTutorial2.cs',],
    ['playback-tutorial-3' , 'PlaybackTutorial3.cs',],
    ['playback-tutorial-4' , 'PlaybackTutorial4.cs',],
    ['playback-tutorial-5' , 'PlaybackTutorial5.cs',],
    ['playback-tutorial-6' , 'PlaybackTutorial6.cs',],
    ['playback-tutorial-7' , 'PlaybackTutorial7.cs',],
    ['rtsp-server-example', 'RTSPServerExample.cs',],
]

foreach example: examples
    deps = [gst_sharp_dep]
    if example.length() == 2 or example.get(2)
        if example.length() > 2
            deps += example.get(3)
        endif

        executable(example.get(0), example.get(1),
                cs_args: ['-unsafe'], dependencies: deps)
    endif
endforeach

if ges_dep.found()
    executable('ges-example', 'GESExample.cs',
            cs_args: ['-unsafe'],
            dependencies: [gst_sharp_dep],
            link_with: ges_sharp)
endif
