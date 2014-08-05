// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;

namespace GstreamerSharp
{
	class Playback
	{
		public static void Main (string[] args)
		{
			// Initialize Gstreamer
			Gst.Application.Init(ref args);

			// Create the elements
			var audioSource = ElementFactory.Make ("audiotestsrc", "audio_source");
			var tee = ElementFactory.Make ("tee", "tee");
			var audioQueue = ElementFactory.Make ("queue", "audio_queue");
			var audioConvert = ElementFactory.Make ("audioconvert", "audio_convert");
			var audioResample = ElementFactory.Make ("audioresample", "audio_resample");
			var audioSink = ElementFactory.Make ("autoaudiosink", "audio_sink");
			var videoQueue = ElementFactory.Make ("queue", "video_queue");
			var visual = ElementFactory.Make ("wavescope", "visual");
			var videoConvert = ElementFactory.Make ("videoconvert", "csp");
			var videoSink = ElementFactory.Make ("autovideosink", "video_sink");

			// Create the empty pipeline
			var pipeline = new Pipeline ("test-pipeline");

			if (audioSource == null || tee == null || audioQueue == null || audioConvert == null || audioResample == null || 
				audioSink == null || videoQueue == null || visual == null || videoConvert == null || videoSink == null || pipeline == null) {
				Console.WriteLine ("Not all elements could be created.");
				return;
			}


			// Link all elements that can be automatically linked because they have "Always" pads
			pipeline.Add (audioSource, tee, audioQueue, audioConvert, audioResample, audioSink,
				videoQueue, visual, videoConvert, videoSink);
			if (!audioSource.Link (tee) ||
				!Element.Link (audioQueue, audioConvert, audioResample, audioSink) ||
				!Element.Link (videoQueue, visual, videoConvert, videoSink)) {
				Console.WriteLine ("Elements could not be linked.");
				return;
			}

			// Manually link the Tee, which has "Request" pads
			var teeSrcPadTemplate = tee.GetPadTemplate ("src_%u");
			var teeAudioPad = tee.RequestPad (teeSrcPadTemplate, null, null);
			Console.WriteLine ("Obtained request pad {0} for audio branch.", teeAudioPad.Name);
			var queueAudioPad = audioQueue.GetStaticPad ("sink");
			var teeVideoPad = tee.RequestPad (teeSrcPadTemplate, null, null);
			Console.WriteLine ("Obtained request pad {0} for video branch.", teeVideoPad.Name);
			var queueVideoPad = videoQueue.GetStaticPad ("sink");
			if (teeAudioPad.Link (queueAudioPad) != PadLinkReturn.Ok ||
				teeVideoPad.Link(queueVideoPad) != PadLinkReturn.Ok) {
				Console.WriteLine ("Tee could not be linked.");
				return;
			}

			// Start playing
			var ret = pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state (check the bus for error messages).");
			}

			// Wait until error or EOS
			pipeline.Bus.TimedPopFiltered (Constants.CLOCK_TIME_NONE, MessageType.Error | MessageType.Eos);

			// Release the request pads from the Tee, and unref them
			tee.ReleaseRequestPad (teeAudioPad);
			tee.ReleaseRequestPad (teeVideoPad);

			// Free resources
			pipeline.SetState (State.Null);
		}
	}
}