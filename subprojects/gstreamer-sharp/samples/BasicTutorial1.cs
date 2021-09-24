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
			var pipeline = Parse.Launch("playbin uri=http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4");

			// Start playing
			pipeline.SetState(State.Playing);

			// Wait until error or EOS
			var bus = pipeline.Bus;
			var msg = bus.TimedPopFiltered (Constants.CLOCK_TIME_NONE, MessageType.Eos | MessageType.Error);

			// Free resources
			pipeline.SetState (State.Null);
		}
	}
}
