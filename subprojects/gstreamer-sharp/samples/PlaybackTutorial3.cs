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

		static Gst.App.AppSrc AppSource;
		static Element Pipeline;

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

		// This function is called when playbin has created the appsrc element, so we have a chance to configure it.
		static void SourceSetup (object sender, GLib.SignalArgs args) {
			var info = new Gst.Audio.AudioInfo ();
			var source = new Gst.App.AppSrc(((Element)args.Args [0]).Handle);
			Console.WriteLine ("Source has been created. Configuring.");
			AppSource = source;

			// Configure appsrc
			Gst.Audio.AudioChannelPosition[] position = {};
			info.SetFormat (Gst.Audio.AudioFormat.S16, SampleRate, 1, position);
			var audioCaps = info.ToCaps ();
			source ["caps"] = audioCaps;
			source ["format"] = Format.Time;
			source.NeedData += StartFeed;
			source.EnoughData += StopFeed;
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

			// Initialize Gstreamer
			Gst.Application.Init(ref args);

			// Create the playbin element
			Pipeline = Parse.Launch ("playbin uri=appsrc://");
			Pipeline.Connect ("source-setup", SourceSetup);

			// Instruct the bus to emit signals for each received message, and connect to the interesting signals
			var bus = Pipeline.Bus;
			bus.AddSignalWatch ();
			bus.Connect ("message::error", HandleError);

			// Start playing the pipeline
			Pipeline.SetState (State.Playing);

			// Create a GLib Main Loop and set it to run
			MainLoop = new GLib.MainLoop ();
			MainLoop.Run ();

			// Free resources
			Pipeline.SetState (State.Null);
		}
	}
}