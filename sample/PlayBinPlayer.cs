
using System;

using GLib;

using Gst;


public class PlayBinPlayer
{
    private static MainLoop loop;
    private static string[] songs;
    private static int song_idx = 0;
    private static PlayBin play;

    public static void Main (string[] args)
    {
        if (args.Length < 1) {
            Console.WriteLine ("usage: mono playbin-player.exe audio_file_uri");
            return;
        }

        songs = args;

        Gst.Application.Init ();
        loop = new MainLoop ();

        play = ElementFactory.Make ("playbin", "play") as PlayBin;

        if (play == null) {
            Console.WriteLine ("error creating a playbin gstreamer object");
            return;
        }

        play.Uri = songs[song_idx++];
        play.Bus.AddWatch (new BusFunc (BusCb));
        play.SetState (State.Playing);
        
        loop.Run ();
    }

    private static bool BusCb (Bus bus, Message message)
    {
        switch (message.Type) {
            case MessageType.Error:
                string err = String.Empty;
                message.ParseError (out err);
                Console.WriteLine ("Gstreamer error: {0}", err);
                loop.Quit ();
                break;
            case MessageType.Eos:
                if (song_idx >= songs.Length) {
                    Console.WriteLine ("Thank you, come again");
                    loop.Quit ();
                } else {
                    play.SetState (State.Null);
                    play.Uri = songs[song_idx++];
                    play.SetState (State.Playing);
                }
                break;
        }

        return true;
    }
}
        
