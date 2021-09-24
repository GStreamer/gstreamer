// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst; 

namespace GstreamerSharp
{
	class Playback
	{
		public static void Main (string[] args)
		{
			// Initialize Gstreamer
			Application.Init(ref args);

			// Build the pipeline
			var source = ElementFactory.Make ("videotestsrc", "source");
			var sink = ElementFactory.Make ("autovideosink", "sink");

			// Create the empty pipeline
			var pipeline = new Pipeline ("test-pipeline");

			if (pipeline == null || source == null || sink == null) {
				Console.WriteLine ("Not all elements could be created");
				return;
			}

			// Build the pipeline
			pipeline.Add (source, sink);
			if (!source.Link (sink)) {
				Console.WriteLine ("Elements could not be linked");
				return;
			}

			// Modify the source's properties
			source ["pattern"] = 0;

			// Start playing
			var ret = pipeline.SetState(State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state");
				return;
			}

			// Wait until error or EOS
			var bus = pipeline.Bus;
			var msg = bus.TimedPopFiltered (Constants.CLOCK_TIME_NONE, MessageType.Eos | MessageType.Error);

			// Free resources
			if (msg != null) {
				switch (msg.Type) {
				case MessageType.Error:
					GLib.GException exc;
					string debug;
					msg.ParseError (out exc, out debug);
					Console.WriteLine (String.Format ("Error received from element {0}: {1}", msg.Src.Name, exc.Message));
					Console.WriteLine (String.Format ("Debugging information {0}", debug));
					break;
				case MessageType.Eos:
					Console.WriteLine ("End-Of-Stream reached");
					break;
				default:
					// We should not reach here because we only asked for ERRORs and EOS
					Console.WriteLine ("Unexpected messag received");
					break;
				}
			}

			pipeline.SetState (State.Null);
		}
	}
}