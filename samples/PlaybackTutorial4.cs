// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;
using System.Text;

namespace GstreamerSharp
{
	class Playback
	{
		const int GraphLength = 78;
		static uint PlayFlagDownload = (1 << 7); // Enable progressive download (on selected formats)

		static bool IsLive;
		static Element Pipeline;
		static GLib.MainLoop MainLoop;
		static int BufferingLevel;

		static void GotLocation (object sender, GLib.SignalArgs args) {
			var propObject = (Gst.Object)args.Args [0];
			var location = (string) propObject["temp-location"];
			Console.WriteLine ("Temporary file: {0}", location);
			// Uncomment this line to keep the temporary file after the program exits
			// g_object_set (G_OBJECT (prop_object), "temp-remove", FALSE, NULL);
		}

		public static void Main (string[] args)
		{
			// Initialize GStreamer
			Application.Init (ref args);
			BufferingLevel = 100;

			// Build the pipeline
			Pipeline = Parse.Launch ("playbin uri=http://freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm");
			var bus = Pipeline.Bus;

			// Set the download flag
			var flags = (uint)Pipeline ["flags"];
			flags |= PlayFlagDownload;
			Pipeline ["flags"] = flags;

			// Uncomment this line to limit the amount of downloaded data
			// g_object_set (pipeline, "ring-buffer-max-size", (guint64)4000000, NULL);

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
			Pipeline.Connect ("deep-notify::temp-location", GotLocation);

			// Register a function that GLib will call every second
			GLib.Timeout.AddSeconds (1, RefreshUI);

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

		static bool RefreshUI () {
			var query = new Query (Format.Percent);
			if (Pipeline.Query (query)) {
				var graph = new StringBuilder (GraphLength);
				long position = 0, duration = 0;

				var nRanges = query.NBufferingRanges;
				for (uint range = 0; range < nRanges; range++) {
					long start, stop;
					query.ParseNthBufferingRange (range, out start, out stop);
					start = start * GraphLength / (stop - start);
					stop = stop * GraphLength / (stop - start);
					for (int i = (int)start; i < stop; i++)
						graph.Insert (i, '-');
				}
				if (Pipeline.QueryPosition (Format.Time, out position) && position >= 0
					&& Pipeline.QueryDuration (Format.Time, out duration) && duration >= 0) {
					var i = (int)(GraphLength * (double)position / (double)(duration + 1));
					graph [i] = BufferingLevel < 100 ? 'X' : '>';
				}
				Console.WriteLine ("[{0}]", graph);
				if (BufferingLevel < 100) {
					Console.WriteLine (" Buffering: {0}", BufferingLevel);
				} else {
					Console.WriteLine ("                ");
				}
				Console.WriteLine ();
			}
			return true;
		}
	}
}