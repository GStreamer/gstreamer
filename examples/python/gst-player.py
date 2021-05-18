#!/usr/bin/python3
import sys
import gi

gi.require_version('Gst', '1.0')
gi.require_version('GES', '1.0')
gi.require_version('GstPlayer', '1.0')
gi.require_version('GLib', '2.0')

from gi.repository import Gst, GES, GLib, GstPlayer


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("You must specify a file URI")
        sys.exit(-1)

    Gst.init(None)
    GES.init()

    timeline = GES.Timeline.new_audio_video()
    layer = timeline.append_layer()
    start = 0
    for uri in sys.argv[1:]:
        if not Gst.uri_is_valid(uri):
            uri = Gst.filename_to_uri(uri)

        clip = GES.UriClip.new(uri)
        clip.props.start = start
        layer.add_clip(clip)

        start += clip.props.duration

    player = GstPlayer
    player = GstPlayer.Player.new(None, GstPlayer.PlayerGMainContextSignalDispatcher.new(None))
    player.set_uri("ges://")
    player.get_pipeline().connect("source-setup",
        lambda playbin, source: source.set_property("timeline", timeline))

    loop = GLib.MainLoop()
    player.connect("end-of-stream", lambda x: loop.quit())

    def error(player, err):
        loop.quit()
        print("Got error: %s" % err)
        sys.exit(1)

    player.connect("error", error)
    player.play()
    loop.run()
