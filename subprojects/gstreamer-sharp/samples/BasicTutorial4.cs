// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst; 

namespace GstreamerSharp
{
	class Playback
	{
		static Element playbin;
		static bool playing;
		static bool terminate;
		static bool seekEnabled;
		static bool seekDone;
		static long duration;

		public static void Main (string[] args)
		{
			// Initialize Gstreamer
			Application.Init(ref args);

			// Create the elements
			playbin = ElementFactory.Make ("playbin", "playbin");

			if (playbin == null) {
				Console.WriteLine ("Not all elements could be created");
				return;
			}

			// Set the URI to play.
			playbin ["uri"] = "http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4";

			// Start playing
			var ret = playbin.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}

			// Listen to the bus
			var bus = playbin.Bus;
			do {
				var msg = bus.TimedPopFiltered (100 * Constants.MSECOND, MessageType.StateChanged | MessageType.Error |  MessageType.DurationChanged);

				// Parse message
				if (msg != null) {
					HandleMessage (msg);
				}
				else {
					// We got no message, this means the timeout expired
					if (playing) {
						var fmt = Format.Time;
						var current = -1L;

						// Query the current position of the stream
						if (!playbin.QueryPosition (fmt, out current)) {
							Console.WriteLine ("Could not query current position.");
						}

						// if we didn't know it yet, query the stream position
						if (duration <= 0) {
							if (!playbin.QueryDuration (fmt, out duration)) {
								Console.WriteLine ("Could not query current duration.");
							}
						}

						// Print current position and total duration
						Console.Write("Position {0} / {1}\r", new TimeSpan (current), new TimeSpan (duration));

						if (seekEnabled && !seekDone && current > 10L * Constants.SECOND) {
							Console.WriteLine ("\nRead 10s, performing seek...");
							playbin.SeekSimple (fmt, SeekFlags.KeyUnit | SeekFlags.Flush, 30L * Constants.SECOND);
							seekDone = true;
						}
					}
				}
			} while (!terminate);
		}

		private static void HandleMessage (Message msg) {
			switch (msg.Type) {
			case MessageType.Error:
				string debug;
				GLib.GException exc;
				msg.ParseError (out exc, out debug);
				Console.WriteLine (string.Format ("Error received from element {0}: {1}", msg.Src.Name, exc.Message));
				Console.WriteLine ("Debugging information: {0}", debug);
				terminate = true;
				break;
			case MessageType.Eos:
				Console.WriteLine("End-Of-Stream reached.");
				terminate = true;
				break;
			case MessageType.DurationChanged:
				// The duration has changed, mark the current one as invalid
				duration = -1;
				break;
			case MessageType.StateChanged:
				// We are only interested in state-changed messages from the pipeline
				if (msg.Src == playbin) {
					State oldState, newState, pendingState;
					msg.ParseStateChanged(out oldState, out newState, out pendingState);
					Console.WriteLine ("Pipeline state changed from {0} to {1}:", Element.StateGetName(oldState), Element.StateGetName(newState));

					// Remember wheather we are in the PLAYING state
					playing = newState == State.Playing;

					if (playing) {
						// We have just moved to PLAYING. Check if seeking is possible
						var query = Query.NewSeeking (Format.Time);
						long start, end;
						Format fmt = Format.Time;

						if (playbin.Query (query)) {
							query.ParseSeeking (out fmt, out seekEnabled, out start, out end);

							if (seekEnabled) {
								Console.WriteLine ("Seeking is ENABLED from {0} to {1}", new TimeSpan(start), new TimeSpan(end));
							} else {
								Console.WriteLine ("Seeking DISABLED for this stream.");
							}
						} else {
							Console.WriteLine ("Seeking query failed.");
						}
					}
				}
				break;
			default:
				// We should not reach here
				Console.WriteLine ("Unexpected message received.");
				break;
			} 
		}
	}
}