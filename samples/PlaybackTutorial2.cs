// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using System.Runtime.InteropServices;

namespace GstreamerSharp
{
	class Playback
	{
		// playbin flags
		static uint Video = (1 << 0); // We want video output
		static uint Audio = (1 << 1); // We want audio output
		static uint Text = (1 << 2);  // We want subtitle output

		static Element Playbin;
		static int NAudio, NVideo, NText;
		static int CurrentVideo, CurrentAudio, CurrentText;
		static GLib.MainLoop MainLoop;

		public static void Main (string[] args)
		{
			// Initialize GStreamer
			Application.Init (ref args);

			// Create the elements
			Playbin = ElementFactory.Make ("playbin", "playbin");

			if (Playbin == null) {
				Console.WriteLine ("Not all elements could be created.");
				return;
			}

			// Set the URI to play
			Playbin ["uri"] = "http://freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.ogv";

			// Set the subtitle URI to play and some font description
			Playbin ["suburi"] = "http://freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer_gr.srt";
			Playbin ["subtitle-font-desc"] = "Sans, 18";

			// Set flags to show Audio and Video and Subtitles
			var flags = (uint)Playbin ["flags"];
			flags |= Audio | Video | Text;
			Playbin ["flags"] = flags;

			// Add a bus watch, so we get notified when a message arrives
			var bus = Playbin.Bus;
			bus.AddSignalWatch ();
			bus.Message += HandleMessage;

			// Start playing
			var ret = Playbin.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}

			// Add a keyboard watch so we get notified of keystrokes
			GLib.Idle.Add (HandleKeyboard);
			MainLoop = new GLib.MainLoop ();
			MainLoop.Run ();

			// Free resources
			Playbin.SetState (State.Null);
		}

		static void HandleMessage (object o, MessageArgs args)
		{
			var msg = args.Message;
			switch (msg.Type) {
			case MessageType.Error:
				GLib.GException err;
				string debug;
				msg.ParseError (out err, out debug);
				Console.WriteLine ("Error received from element {0}: {1}", msg.Src, err.Message);
				Console.WriteLine ("Debugging information: {0}", debug != null ? debug : "none");
				MainLoop.Quit ();
				break;
			case MessageType.Eos:
				Console.WriteLine ("End-Of-Stream reached.");
				MainLoop.Quit ();
				break;
			case MessageType.StateChanged: {
					State oldState, newState, pendingState;
					msg.ParseStateChanged (out oldState, out newState, out pendingState);
					if (msg.Src == Playbin) {
						if (newState == State.Playing) {
							// Once we are in the playing state, analyze the streams
							AnalyzeStreams ();
						}
					}
				} break;
			default:
				break;
			}

			// We want to keep receiving messages
			args.RetVal = true;
		}

		// Extract some metadata from the streams and print it on the screen
		static void AnalyzeStreams () {
			// Read some properties
			NVideo = (int)Playbin ["n-video"];
			NAudio = (int)Playbin ["n-audio"];
			NText = (int)Playbin ["n-text"];

			Console.WriteLine ("{0} video stream(s), {1} audio stream(s), {2} text stream(s)", NVideo, NAudio, NText);

			Console.WriteLine ();
			for (int i = 0; i < NVideo; i++) {
				// Retrieve the stream's video tags
				var tags = (TagList)Playbin.Emit ("get-video-tags", new object [] { i });
				if (tags != null) {
					Console.WriteLine ("video stream {0}", i);
					string str;
					tags.GetString (Constants.TAG_VIDEO_CODEC, out str);
					Console.WriteLine ("  codec: {0}", str != null ? str : "unknown");
				}
			}

			Console.WriteLine ();
			for (int i = 0; i < NAudio; i++) {
				// Retrieve the stream's audio tags
				var tags = (TagList)Playbin.Emit ("get-audio-tags", new object [] { i });
				if (tags != null) {
					Console.WriteLine ("audio stream {0}", i);
					string str;
					if (tags.GetString (Constants.TAG_AUDIO_CODEC, out str)) {
						Console.WriteLine ("  codec: {0}", str);
					}
					if (tags.GetString (Constants.TAG_LANGUAGE_CODE, out str)) {
						Console.WriteLine ("  language: {0}", str);
					}
					uint rate;
					if (tags.GetUint (Constants.TAG_BITRATE, out rate)) {
						Console.WriteLine ("  bitrate: {0}", rate);
					}
				}
			}

			Console.WriteLine ();
			for (int i = 0; i < NText; i++) {
				// Retrieve the stream's subtitle tags
				var tags = (TagList)Playbin.Emit ("get-text-tags", new object [] { i });
				if (tags != null) {
					Console.WriteLine ("subtitle stream {0}", i);
					string str;
					if (tags.GetString (Constants.TAG_LANGUAGE_CODE, out str)) {
						Console.WriteLine ("  language: {0}", str);
					}
				}
			}

			CurrentAudio = (int)Playbin ["current-audio"];
			CurrentVideo = (int)Playbin ["current-video"];
			CurrentText = (int)Playbin ["current-text"];

			Console.WriteLine ();
			Console.WriteLine ("Currently playing video stream {0}, audio stream {1} and text stream {2}", CurrentVideo, CurrentAudio, CurrentText);
			Console.WriteLine ("Type any number to select a different subtitle stream");
		}

		// Process keyboard input
		static bool HandleKeyboard () {
			if (Console.KeyAvailable) {
				var key = Console.ReadKey (false);

				if (char.IsDigit (key.KeyChar)) {
					var digit = int.Parse (key.KeyChar.ToString ());

					if (digit < 0 || digit > NText) {
						Console.WriteLine ("Index out of bounds");
					} else {
						// If the input was a valid subtitle stream index, set the current subtitle stream
						Console.WriteLine ("Setting current subtitle stream to {0}", digit);
						Playbin ["current-text"] = digit;
					}
				}
			}
			return true;
		}
	}
}