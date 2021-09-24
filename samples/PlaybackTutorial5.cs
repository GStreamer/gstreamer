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
		static Element Pipeline;
		static GLib.MainLoop MainLoop;

		// Process a color balance command
		static void UpdateColorChannel (string channelName, bool increase, Gst.Video.IColorBalance cb) {

			// Retrieve the list of channels and locate the requested one
			var channels = cb.ListChannels ();
			Gst.Video.ColorBalanceChannel channel = null;
			foreach (var ch in channels) {
				var label = ch.Label;

				if (label.Contains (channelName)) {
					channel = ch;
					break;
				}
			}
			if (channel == null)
				return;

			// Change the channel's value
			var step = 0.1 * (channel.MaxValue - channel.MinValue);
			var value = cb.GetValue (channel);
			if (increase) {
				value = (int)(value + step);
				if (value > channel.MaxValue)
					value = channel.MaxValue;
			} else {
				value = (int)(value - step);
				if (value < channel.MinValue)
					value = channel.MinValue;
			}
			cb.SetValue (channel, value);
		}

		// Process keyboard input
		static bool HandleKeyboard () {
			if (Console.KeyAvailable) {
				var key = Console.ReadKey (false);
				var cb = new Gst.Video.ColorBalanceAdapter (Pipeline.Handle);

				switch (key.Key) {
				case ConsoleKey.C:
					UpdateColorChannel ("CONTRAST", key.Modifiers == ConsoleModifiers.Shift, cb);
					break;
				case ConsoleKey.B:
					UpdateColorChannel ("BRIGHTNESS", key.Modifiers == ConsoleModifiers.Shift, cb);
					break;
				case ConsoleKey.H:
					UpdateColorChannel ("HUE", key.Modifiers == ConsoleModifiers.Shift, cb);
					break;
				case ConsoleKey.S:
					UpdateColorChannel ("SATURATION", key.Modifiers == ConsoleModifiers.Shift, cb);
					break;
				case ConsoleKey.Q:
					MainLoop.Quit ();
					break;
				}
				PrintCurrentValues ();
			}
			return true;
		}


		// Output the current values of all Color Balance channels
		static void PrintCurrentValues () {
			// Output Color Balance value
			var cb = new Gst.Video.ColorBalanceAdapter (Pipeline.Handle);
			var channels = cb.ListChannels ();

			foreach (var ch in channels) {
				var value = cb.GetValue (ch);
				Console.WriteLine ("{0}: {1}", ch.Label, 100 * (value - ch.MinValue) / (ch.MaxValue - ch.MinValue));
			}
			Console.WriteLine ();
		}

		public static void Main (string[] args)
		{
			// Initialize GStreamer
			Application.Init (ref args);
			GtkSharp.GstreamerSharp.ObjectManager.Initialize ();

			// Print usage map 
			Console.WriteLine ("USAGE: Choose one of the following options, then press enter:");
			Console.WriteLine (" 'C' to increase contrast, 'c' to decrease contrast");
			Console.WriteLine (" 'B' to increase brightness, 'b' to decrease brightness");
			Console.WriteLine (" 'H' to increase hue, 'h' to decrease hue");
			Console.WriteLine (" 'S' to increase saturation, 's' to decrease saturation");
			Console.WriteLine (" 'Q' to quit");

			// Build the pipeline 
			Pipeline = Parse.Launch ("playbin uri=http://freedesktop.org/software/gstreamer-sdk/data/media/sintel_trailer-480p.webm");

			// Add a keyboard watch so we get notified of keystrokes 

			// Start playing 
			var ret = Pipeline.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}
			PrintCurrentValues ();

			// Create a GLib Main Loop and set it to run 
			MainLoop = new GLib.MainLoop ();
			GLib.Timeout.Add (50, HandleKeyboard);
			MainLoop.Run ();

			// Free resources 
			Pipeline.SetState (State.Null);
		}
	}
}