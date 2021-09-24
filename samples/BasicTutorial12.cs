// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;

namespace GstreamerSharp
{
	class Playback
	{
		static bool IsLive;
		static Element Pipeline;
		static GLib.MainLoop MainLoop;

		public static void Main (string[] args)
		{
			// Initialize GStreamer
			Application.Init (ref args);

			// Build the pipeline
			Pipeline = Parse.Launch ("playbin uri=http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4");
			var bus = Pipeline.Bus;

			// Start playing
			var ret = Pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			} else if (ret == StateChangeReturn.NoPreroll) {
				IsLive = true;
			}

			MainLoop = new GLib.MainLoop ();

			bus.AddSignalWatch ();
			bus.Message += HandleMessage;

			MainLoop.Run ();

			// Free resources
			Pipeline.SetState (State.Null);
		}

		static void HandleMessage (object o, MessageArgs args)
		{
			var msg = args.Message;
			switch (msg.Type) {
			case MessageType.Error: {
					GLib.GException err;
					string debug;

					msg.ParseError (out err, out debug);
					Console.WriteLine ("Error: {0}", err.Message);

					Pipeline.SetState (State.Ready);
					MainLoop.Quit ();
					break;
				}
			case MessageType.Eos:
				// end-of-stream
				Pipeline.SetState (State.Ready);
				MainLoop.Quit ();
				break;
			case MessageType.Buffering: {
					int percent = 0;

					// If the stream is live, we do not care about buffering.
					if (IsLive) break;

					percent = msg.ParseBuffering ();
					Console.WriteLine ("Buffering ({0})", percent);
					// Wait until buffering is complete before start/resume playing
					if (percent < 100)
						Pipeline.SetState (State.Paused);
					else
						Pipeline.SetState (State.Playing);
					break;
				}
			case MessageType.ClockLost:
				// Get a new clock
				Pipeline.SetState (State.Paused);
				Pipeline.SetState (State.Playing);
				break;
			default:
				// Unhandled message
				break;
			}
		}
	}
}