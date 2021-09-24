// Authors
//   Copyright (C) 2014 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using Gst;
using Gtk;
using System.Runtime.InteropServices;
using Gst.Video; 

namespace GstreamerSharp
{
	class Playback
	{
		static Element Playbin;
		static Gtk.Range Slider;
		static TextView StreamsList;
		static ulong silderUpdateSignalID;

		static State State;
		static long Duration = -1;
		static int ignoreCount = 0;

		static void HandleValueChanged (object sender, EventArgs e)
		{
			var range = (Gtk.Range)sender;
			var value = range.Value;
			Playbin.SeekSimple (Format.Time, SeekFlags.Flush | SeekFlags.KeyUnit, (long)(value * Gst.Constants.SECOND));
		}

		// This method is called when the STOP button is clicked
		static void HandleStop (object sender, EventArgs e)
		{
			Playbin.SetState (State.Ready);
		}

		// This method is called when the PAUSE button is clicked
		static void HandlePause (object sender, EventArgs e)
		{
			Playbin.SetState (State.Paused);
		}

		// This method is called when the PLAY button is clicked
		static void HandlePlay (object sender, EventArgs e)
		{
			Playbin.SetState (State.Playing);

		}

		static void HandleRealized (object sender, EventArgs e)
		{
			var widget = (Widget)sender;
			var window = widget.Window;
			IntPtr windowID = IntPtr.Zero;

			// Retrieve window handler from GDK
			switch (System.Environment.OSVersion.Platform) {
			case PlatformID.Unix:
				windowID = gdk_x11_window_get_xid (window.Handle);
				break;
			case PlatformID.Win32NT:
			case PlatformID.Win32S:
			case PlatformID.Win32Windows:
			case PlatformID.WinCE:
				windowID = gdk_win32_drawable_get_handle (window.Handle);
				break;
			}

			Element overlay = null;
			if(Playbin is Gst.Bin)
				overlay = ((Gst.Bin) Playbin).GetByInterface (VideoOverlayAdapter.GType);

			VideoOverlayAdapter adapter = new VideoOverlayAdapter (overlay.Handle);
			adapter.WindowHandle = windowID;
			adapter.HandleEvents (true);
		}

		// This function is called when the main window is closed
		static void HandleDelete (object o, DeleteEventArgs args)
		{
			HandleStop (null, null);
			Gtk.Application.Quit ();
		}

		//This function is called everytime the video window needs to be redrawn (due to damage/exposure, rescaling, etc). GStreamer takes care of this in the PAUSED and PLAYING states, otherwise, we simply draw a black rectangle to avoid garbage showing up. */
		static void HandleDamage (object o, DamageEventArgs args)
		{
			var widget = (Widget)o;

			if (State != State.Paused && State != State.Playing) {
				var window = widget.Window;
				var allocation = widget.Allocation;

				var cr = Gdk.CairoHelper.Create (window);
				cr.SetSourceRGB (0, 0, 0);
				cr.Rectangle (0, 0, allocation.Width, allocation.Height);
				cr.Fill ();
				cr.Dispose ();
			}

			args.RetVal = false;
		}

		static void CreateUI () {
			var mainWindow = new Window (WindowType.Toplevel);
			mainWindow.DeleteEvent += HandleDelete;

			var videoWindow = new DrawingArea ();
			videoWindow.DoubleBuffered = false;
			videoWindow.Realized += HandleRealized;
			videoWindow.DamageEvent += HandleDamage;

			var playButton = new Button (Stock.MediaPlay);
			playButton.Clicked += HandlePlay;

			var pauseButton = new Button (Stock.MediaPause);
			pauseButton.Clicked += HandlePause;

			var stopButton = new Button (Stock.MediaStop);
			stopButton.Clicked += HandleStop;

			Slider = new HScale (0, 100, 1);
			((Scale)Slider).DrawValue = false;
			Slider.ValueChanged += HandleValueChanged;

			StreamsList = new TextView ();
			StreamsList.Editable = false;

			var controls = new HBox (false, 0);
			controls.PackStart (playButton, false, false, 2);
			controls.PackStart (pauseButton, false, false, 2);
			controls.PackStart (stopButton, false, false, 2);
			controls.PackStart (Slider, true, true, 2);

			var mainHBox = new HBox (false, 0);
			mainHBox.PackStart (videoWindow, true, true, 0);
			mainHBox.PackStart (StreamsList, false, false, 2);

			var mainBox = new VBox (false, 0);
			mainBox.PackStart (mainHBox, true, true, 0);
			mainBox.PackStart (controls, false, false, 0);
			mainWindow.Add (mainBox);
			mainWindow.SetDefaultSize (640, 480);

			mainWindow.ShowAll ();
		}

		// This function is called periodically to refresh the GUI
		static bool RefreshUI () {
			var fmt = Format.Time;
			long current = 0;

			// We do not want to update anything nless we are in the PAUSED or PLAYING states
			if (State != State.Playing && State != State.Paused)
				return true;

			// If we didn't know it yet, query the stream duration
			if (Duration < 0) {
				if (!Playbin.QueryDuration (fmt, out Duration))
					Console.WriteLine ("Could not query the current duration.");
				else {
					// Set the range of the silder to the clip duration, in SECONDS
					Slider.SetRange (0, Duration / (double)Gst.Constants.SECOND);
				}
			}

			if (Playbin.QueryPosition (fmt, out current)) {
				// Block the "value-changed" signal, so the HandleSlider function is not called (which would trigger a seek the user has not requested)
				ignoreCount++;
				Slider.ValueChanged -= HandleValueChanged;
				// Set the position of the slider to the current pipeline position, in SECONDS
				Slider.Value = current / (double)Gst.Constants.SECOND;
				Slider.ValueChanged += HandleValueChanged;

			}
			return true;
		}



		// This function is called when an error message is posted on the bus
		static void HandleTags (object sender, GLib.SignalArgs args) {
			// We are possibly in the Gstreamer working thread, so we notify the main thread of this event through a message in the bus
			var s = new Structure ("tags-changed");
			Playbin.PostMessage (Message.NewApplication (Playbin, s));
		}

		// This function is called when an error message is posted on the bus
		static void HandleError (object sender, GLib.SignalArgs args) {
			var msg = (Message)args.Args [0];
			string debug;
			GLib.GException exc;
			msg.ParseError (out exc, out debug);
			Console.WriteLine (string.Format ("Error received from element {0}: {1}", msg.Src.Name, exc.Message));
			Console.WriteLine ("Debugging information: {0}", debug);
			// Set the pipeline to READY (which stops playback)
			Playbin.SetState (State.Ready);
		}

		// This function is called when an End-Of-Stream message is posted on the bus. We just set the pipelien to READY (which stops playback)
		static void HandleEos (object sender, GLib.SignalArgs args) {
			Console.WriteLine ("End-Of-Stream reached.");
			Playbin.SetState (State.Ready);
		}

		// This function is called when the pipeline changes states. We use it to keep track of the current state.
		static void HandleStateChanged (object sender, GLib.SignalArgs args) {
			var msg = (Message) args.Args [0];
			State oldState, newState, pendingState;
			msg.ParseStateChanged (out oldState, out newState, out pendingState);
			if (msg.Src == Playbin) {
				State = newState;
				Console.WriteLine ("State set to {0}", Element.StateGetName (newState));
				if (oldState == State.Ready && newState == State.Paused) {
					// For extra responsiveness, we refresh the GUI as soon as we reach the PAUSED state
					RefreshUI ();
				}
			}

		}

		// Extract metadata from all the streams and write it to the text widget in the GUI
		static void AnalyzeStreams () {
			TagList tags;
			String str, totalStr;
			uint rate;

			// Clean current contents of the widget
			var text = StreamsList.Buffer;
			text.Text = String.Empty;

			// Read some properties
			var nVideo = (int) Playbin ["n-video"];
			var nAudio = (int) Playbin ["n-audio"];
			var nText = (int) Playbin ["n-text"];

			for (int i = 0; i < nVideo; i++) {
				// Retrieve the stream's video tags
				tags = (TagList)Playbin.Emit ("get-video-tags", i);

				if (tags != null) {
					totalStr = string.Format ("video stream {0}:\n", i);
					text.InsertAtCursor (totalStr);
					tags.GetString (Gst.Constants.TAG_VIDEO_CODEC, out str);
					totalStr = string.Format ("  codec: {0}\n", str != null ? str : "unknown");
					text.InsertAtCursor (totalStr);
				}
			}

			for (int i = 0; i < nAudio; i++) {
				// Retrieve the stream's audio tags
				tags = (TagList)Playbin.Emit ("get-audio-tags", i);

				if (tags != null) {
					totalStr = string.Format ("audio stream {0}:\n", i);
					text.InsertAtCursor (totalStr);

					str = String.Empty;
					if (tags.GetString (Gst.Constants.TAG_AUDIO_CODEC, out str)) {
						totalStr = string.Format ("  codec: {0}\n", str);
						text.InsertAtCursor (totalStr);
					}
					str = String.Empty;

					if (tags.GetString (Gst.Constants.TAG_LANGUAGE_CODE+"dr", out str)) {
						totalStr = string.Format ("  language: {0}\n", str);
						text.InsertAtCursor (totalStr);
					}
					str = String.Empty;

					if (tags.GetUint (Gst.Constants.TAG_BITRATE, out rate)) {
						totalStr = string.Format ("  bitrate: {0}\n", rate);
						text.InsertAtCursor (totalStr);
					}
				}
			}

			for (int i = 0; i < nText; i++) {
				// Retrieve the stream's text tags
				tags = (TagList)Playbin.Emit ("get-text-tags", i);

				if (tags != null) {
					totalStr = string.Format ("subtitle stream {0}:\n", i);
					text.InsertAtCursor (totalStr);

					if (tags.GetString (Gst.Constants.TAG_LANGUAGE_CODE, out str)) {
						totalStr = string.Format ("  language: {0}\n", str);
						text.InsertAtCursor (totalStr);
					}
				}
			}
		}

		// This function is called when an "application" message is posted on the bus. Here we retrieve the message posted by the HandleTags callback
		static void HandleApplication (object sender, GLib.SignalArgs args) {
			var msg = (Message)args.Args [0];

			if (msg.Structure.Name.Equals ("tags-changed")) {
				// If the message is the "tags-changed" (only one we are currently issuing), update the stream info GUI
				AnalyzeStreams ();
			}
		}

		public static void Main (string[] args)
		{
			// Initialize GTK
			Gtk.Application.Init ();

			// Initialize Gstreamer
			Gst.Application.Init(ref args);

			// Create the elements
			Playbin = ElementFactory.Make ("playbin", "playbin");

			if (Playbin == null) {
				Console.WriteLine ("Not all elements could be created");
				return;
			}

			// Set the URI to play.
			Playbin ["uri"] = "http://download.blender.org/durian/trailer/sintel_trailer-1080p.mp4";

			// Connect to interesting signals in playbin
			Playbin.Connect ("video-tags-changed", HandleTags);
			Playbin.Connect ("audio-tags-changed", HandleTags);
			Playbin.Connect ("text-tags-changed", HandleTags);


			// Create the GUI
			CreateUI ();

			// Instruct the bus to emit signals for each received message, and connect to the interesting signals
			var bus = Playbin.Bus;
			bus.AddSignalWatch ();
			bus.Connect ("message::error", HandleError);
			bus.Connect ("message::eos", HandleEos);
			bus.Connect ("message::state-changed", HandleStateChanged);
			bus.Connect ("message::application", HandleApplication);


			// Start playing
			var ret = Playbin.SetState (State.Playing);
			if (ret == StateChangeReturn.Failure) {
				Console.WriteLine ("Unable to set the pipeline to the playing state.");
				return;
			}

			// Register a function that GLib will call every second
			GLib.Timeout.Add (1, RefreshUI);

			// Start the GTK main loop- We will not regain control until gtk_main_quit is called
			Gtk.Application.Run ();

			// Free resources
			Playbin.SetState (State.Null);

		}

		[DllImport ("libgdk-3.so.0") ]
		static extern IntPtr gdk_x11_window_get_xid (IntPtr handle);

		[DllImport ("gdk-win32-3.0-0.dll") ]
		static extern IntPtr gdk_win32_drawable_get_handle (IntPtr handle);

		[DllImport ("libX11.so.6")]
		static extern int XInitThreads ();
	}
}
