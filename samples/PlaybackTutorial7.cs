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
		public static void Main (string[] args)
		{
			// Initialize GStreamer
			Application.Init (ref args);

			// Build the pipeline
			var pipeline = Parse.Launch ("playbin uri=http://freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm");

			// Create the elements inside the sink bin
			var equalizer = ElementFactory.Make ("equalizer-3bands", "equalizer");
			var convert = ElementFactory.Make ("audioconvert", "convert");
			var sink = ElementFactory.Make ("autoaudiosink", "audio_sink");
			if (equalizer == null || convert == null || sink == null) {
				Console.WriteLine ("Not all elements could be created.");
				return;
			}

			// Create the sink bin, add the elements and link them
			var bin = new Bin ("audio_sink_bin");
			bin.Add (equalizer, convert, sink);
			Element.Link (equalizer, convert, sink);
			var pad = equalizer.GetStaticPad ("sink");
			var ghostPad = new GhostPad ("sink", pad);
			ghostPad.SetActive (true);
			bin.AddPad (ghostPad);

			// Configure the equalizer
			equalizer["band1"] = (double)-24.0;
			equalizer["band2"] = (double)-24.0;

			// Set playbin's audio sink to be our sink bin
			pipeline["audio-sink"] = bin;

			// Start playing
			var ret = pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}

			// Wait until error or EOS
			var bus = pipeline.Bus;
			var msg = bus.TimedPopFiltered (Constants.CLOCK_TIME_NONE, MessageType.Error | MessageType.Eos);

			// Free resources
			pipeline.SetState (State.Null);
		}
	}
}