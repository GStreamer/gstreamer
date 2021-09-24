// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;

namespace GstreamerSharp
{
	class Playback
	{
		const int ChunkSize = 1024;
		const int SampleRate = 44100;

		static Gst.App.AppSink AppSink;
		static Gst.App.AppSrc AppSource;
		static Element Pipeline, Tee, AudioQueue, AudioConvert1, AudioResample, AudioSink;
		static Element VideoQueue, AudioConvert2, Visual, VideoConvert, VideoSink;
		static Element AppQueue;

		static long NumSamples;   // Number of samples generated so far (for timestamp generation)
		static float a, b, c, d;     // For waveform generation

		static uint Sourceid;        // To control the GSource

		static GLib.MainLoop MainLoop;  // GLib's Main Loop

		// This method is called by the idle GSource in the mainloop, to feed CHUNK_SIZE bytes into appsrc.
		// The idle handler is added to the mainloop when appsrc requests us to start sending data (need-data signal)
		// and is removed when appsrc has enough data (enough-data signal).

		static bool PushData () {
			var numSamples = ChunkSize / 2; // Because each sample is 16 bits
			MapInfo map;

			// Create a new empty buffer
			var buffer = new Gst.Buffer (null, ChunkSize, AllocationParams.Zero);

			// Set its timestamp and duration
			buffer.Pts = Util.Uint64Scale ((ulong)NumSamples, (ulong)Constants.SECOND, (ulong)SampleRate);
			buffer.Dts = Util.Uint64Scale ((ulong)NumSamples, (ulong)Constants.SECOND, (ulong)SampleRate);
			buffer.Duration = Util.Uint64Scale ((ulong)NumSamples, (ulong)Constants.SECOND, (ulong)SampleRate);

			// Generate some psychodelic waveforms
			buffer.Map (out map, MapFlags.Write);
			c += d;
			d -= c / 1000f;
			var freq = 1100f + 1000f * d;
			short[] data = new short[numSamples];
			for (int i = 0; i < numSamples; i++) {
				a += b;
				b -= a / freq;
				data[i] = (short)(500f * a);
			}
			// convert the short[] to a byte[] by marshalling
			var native = Marshal.AllocHGlobal (data.Length * sizeof(short));
			Marshal.Copy (data, 0, native, data.Length);
			byte[] bytedata = new byte[2 * data.Length];
			Marshal.Copy (native, bytedata, 0, data.Length * sizeof(short));

			map.Data = bytedata;
			buffer.Unmap (map);
			NumSamples += numSamples;

			// Push the buffer into the appsrc
			var ret = AppSource.PushBuffer (buffer);

			// Free the buffer now that we are done with it
			buffer.Dispose ();

			if (ret != FlowReturn.Ok) {
				// We got some error, stop sending data
				return false;
			}
			return true;
		}

		// This signal callback triggers when appsrc needs  Here, we add an idle handler
		// to the mainloop to start pushing data into the appsrc
		static void StartFeed (object sender, Gst.App.NeedDataArgs args) {
			if (Sourceid == 0) {
				Console.WriteLine ("Start feeding");
				Sourceid = GLib.Idle.Add (PushData);
			}
		}

		// This callback triggers when appsrc has enough data and we can stop sending.
		// We remove the idle handler from the mainloop
		static void StopFeed (object sender, EventArgs args) {
			if (Sourceid != 0) {
				Console.WriteLine ("Stop feeding");
				GLib.Source.Remove (Sourceid);
				Sourceid = 0;
			}
		}

		// The appsink has received a buffer
		static void NewSample (object sender, GLib.SignalArgs args) {
			var sink = (Gst.App.AppSink)sender;

			// Retrieve the buffer
			var sample = sink.PullSample ();
			if (sample != null) {
				// The only thing we do in this example is print a * to indicate a received buffer
				Console.Write ("*");
				sample.Dispose ();
			}
		}

		// This function is called when an error message is posted on the bus
		static void HandleError (object sender, GLib.SignalArgs args) {
			GLib.GException err;
			string debug;
			var msg = (Message) args.Args[0];

			// Print error details on the screen
			msg.ParseError (out err, out debug);
			Console.WriteLine ("Error received from element {0}: {1}", msg.Src.Name, err.Message);
			Console.WriteLine ("Debugging information: {0}", debug != null ? debug : "none");

			MainLoop.Quit ();
		}

		public static void Main (string[] args)
		{
			b = 1;
			d = 1;
			Gst.Audio.AudioInfo info = new Gst.Audio.AudioInfo();

			// Initialize Gstreamer
			Gst.Application.Init(ref args);

			// Create the elements
			AppSource = new Gst.App.AppSrc ("app_src");
			Tee = ElementFactory.Make ("tee", "tee");
			AudioQueue = ElementFactory.Make ("queue", "audio_queue");
			AudioConvert1 = ElementFactory.Make ("audioconvert", "audio_convert1");
			AudioResample = ElementFactory.Make ("audioresample", "audio_resample");
			AudioSink = ElementFactory.Make ("autoaudiosink", "audio_sink");
			VideoQueue = ElementFactory.Make ("queue", "video_queue");
			AudioConvert2 = ElementFactory.Make ("audioconvert", "audio_convert2");
			Visual = ElementFactory.Make ("wavescope", "visual");
			VideoConvert = ElementFactory.Make ("videoconvert", "video_convert");
			VideoSink = ElementFactory.Make ("autovideosink", "video_sink");
			AppQueue = ElementFactory.Make ("queue", "app_queue");
			AppSink = new Gst.App.AppSink ("app_sink");

			// Create the empty pipeline
			var pipeline = new Pipeline ("test-pipeline");

			if (AppSource == null || Tee == null || AudioQueue == null || AudioConvert1 == null || AudioResample == null || 
				AudioSink == null || VideoQueue == null || AudioConvert2 == null || Visual == null || VideoConvert == null || 
				AppQueue == null || AppSink == null ||pipeline == null) {
				Console.WriteLine ("Not all elements could be created.");
				return;
			}

			// Configure wavescope
			Visual ["shader"] = 0;
			Visual ["style"] = 0;

			// Configure appsrc
			Gst.Audio.AudioChannelPosition[] position = {};
			info.SetFormat (Gst.Audio.AudioFormat.S16, SampleRate, 1, position);
			var audioCaps = info.ToCaps ();
			AppSource ["caps"] = audioCaps;
			AppSource ["format"] = Format.Time;

			AppSource.NeedData += StartFeed;
			AppSource.EnoughData += StopFeed;

			// Configure appsink
			AppSink ["emit-signals"] = true;
			AppSink ["caps"] = audioCaps;
			AppSink.NewSample += NewSample;

			// Link all elements that can be automatically linked because they have "Always" pads
			pipeline.Add (AppSource, Tee, AudioQueue, AudioConvert1, AudioResample, 
				AudioSink, VideoQueue, AudioConvert2, Visual, VideoConvert, VideoSink, AppQueue, AppSink);
			if (!Element.Link (AppSource, Tee) ||
				!Element.Link (AudioQueue, AudioConvert1, AudioResample, AudioSink) ||
				!Element.Link (VideoQueue, AudioConvert2, Visual, VideoConvert, VideoSink) ||
				!Element.Link (AppQueue, AppSink)) {
				Console.WriteLine ("Elements could not be linked.");
				return;
			}

			// Manually link the Tee, which has "Request" pads
			var teeSrcPadTemplate = Tee.GetPadTemplate ("src_%u");
			var teeAudioPad = Tee.RequestPad (teeSrcPadTemplate);
			Console.WriteLine ("Obtained request pad {0} for audio branch.", teeAudioPad.Name);
			var queueAudioPad = AudioQueue.GetStaticPad ("sink");
			var teeVideoPad = Tee.RequestPad (teeSrcPadTemplate);
			Console.WriteLine ("Obtained request pad {0} for video branch.", teeVideoPad.Name);
			var queueVideoPad = VideoQueue.GetStaticPad ("sink");
			var teeAppPad = Tee.RequestPad (teeSrcPadTemplate);
			Console.WriteLine ("Obtained request pad {0} for app branch.", teeAppPad.Name);
			var queueAppPad = AppQueue.GetStaticPad ("sink");
			if (teeAudioPad.Link (queueAudioPad) != PadLinkReturn.Ok ||
				teeVideoPad.Link (queueVideoPad) != PadLinkReturn.Ok ||
				teeAppPad.Link (queueAppPad) != PadLinkReturn.Ok) {
				Console.WriteLine ("Tee could not be linked");
				return;
			}

			// Instruct the bus to emit signals for each received message, and connect to the interesting signals
			var bus = pipeline.Bus;
			bus.AddSignalWatch ();
			bus.Connect ("message::error", HandleError);

			// Start playing the pipeline
			pipeline.SetState (State.Playing);

			// Create a GLib Main Loop and set it to run
			MainLoop = new GLib.MainLoop ();
			MainLoop.Run ();

			// Release the request pads from the Tee, and unref them
			Tee.ReleaseRequestPad(teeAudioPad);
			Tee.ReleaseRequestPad(teeVideoPad);
			Tee.ReleaseRequestPad(teeAppPad);

			// Free resources
			pipeline.SetState (State.Null);

			Gst.Global.Deinit();
		}
	}
}
