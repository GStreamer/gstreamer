// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;

namespace GstreamerSharp
{
	class Playback
	{
		static bool Playing; // Playing or Paused
		static double Rate; //Current playback rate (can be negative)
		static Element Pipeline, VideoSink;

		// Send seek event to change rate
		static void SendSeekEvent () {
			var format = Format.Time;
			long position;

			// Obtain the current position, needed for the seek event
			if (!Pipeline.QueryPosition (format, out position)) {
				Console.WriteLine ("Unable to retrieve current position.");
				return;
			}

			Event seekEvent;
			// Create the seek event
			if (Rate > 0) {
				seekEvent = Event.NewSeek (Rate, Format.Time, SeekFlags.Flush | SeekFlags.Accurate, SeekType.Set, position, SeekType.None, 0);
			} else {
				seekEvent = Event.NewSeek (Rate, Format.Time, SeekFlags.Flush | SeekFlags.Accurate, SeekType.Set, 0, SeekType.Set, position);
			}

			if (VideoSink == null) {
				// If we have not done so, obtain the sink through which we will send the seek events
				VideoSink = (Element)Pipeline ["video-sink"];
			}

			// Send the event
			VideoSink.SendEvent (seekEvent);

			Console.WriteLine ("Current rate: {0}", Rate);
		}

		// Process keyboard input
		static void HandleKeyboard () {
			ConsoleKeyInfo x;
			bool terminate = false;
			while (!terminate) {
				x = Console.ReadKey ();
				switch (x.Key) {
				case ConsoleKey.P :
					Playing = !Playing;
					Pipeline.SetState (Playing ? State.Playing : State.Paused);
					Console.WriteLine ("Setting state to {0}", Playing ? "PLAYING" : "PAUSE");
					break;
				case ConsoleKey.S:
					if (x.Modifiers == ConsoleModifiers.Shift)
						Rate *= 2.0;
					else
						Rate /= 2.0;
					SendSeekEvent ();
					break;
				case ConsoleKey.D:
					Rate *= -1.0;
					SendSeekEvent ();
					break;
				case ConsoleKey.N:
					if (VideoSink == null) {
						// If we have not done so, obtain the sink through which we will send the step events
						VideoSink = (Element)Pipeline ["video-sink"];
					}
					var evnt = Event.NewStep (Format.Buffers, 1, Rate, true, false);
					VideoSink.SendEvent (evnt);

					Console.WriteLine ("Stepping one frame");
					break;
				case ConsoleKey.Q:
					terminate = true;
					break;
				default:
					break;
				}
			}
		}

		public static void Main (string[] args)
		{
			// Initialize GStreamer
			Application.Init (ref args);

			// Print usage map
			Console.WriteLine ("USAGE: Choose one of the following options, then press enter:");
			Console.WriteLine (" 'P' to toggle between PAUSE and PLAY");
			Console.WriteLine (" 'S' to increase playback speed, 's' to decrease playback speed");
			Console.WriteLine (" 'D' to toggle playback direction");
			Console.WriteLine (" 'N' to move to next frame (in the current direction, better in PAUSE)");
			Console.WriteLine (" 'Q' to quit");

			// Build the pipeline
			//Pipeline = Parse.Launch ("playbin uri=http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4");
			Pipeline = Parse.Launch ("playbin uri=file:///home/stephan/Downloads/sintel_trailer-1080p.mp4");

			// Start playing
			var ret = Pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}
			Playing = true;
			Rate = 1.0;

			// Process input
			HandleKeyboard ();

			// Free resources
			Pipeline.SetState (State.Null);
		}
	}
}