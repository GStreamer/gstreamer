// Authors
//   Copyright (C) 2008 Paul Burton <paulburton89@gmail.com>
//   Copyright (C) 2010 Andoni Morales <ylatuya@gmail.com>
//   Copyright (C) 2013 Stephan Sundermann <stephansundermann@gmail.com>

using System;
using System;
using System.Runtime.InteropServices;

using Gtk;
using Gst;
using Gst.Video;
using Gst.Base;

namespace Gstreameroverlay
{
	public class MainWindow : Gtk.Window {
		DrawingArea _da;
		IntPtr _xWindowId;
		Element _playbin;
		HScale _scale;
		Label _lbl;
		bool _updatingScale;
		bool _pipelineOK = false;

		public static void Main (string[] args) {
			if (System.Environment.OSVersion.Platform == PlatformID.Unix)
				XInitThreads ();

			Gtk.Application.Init ();
			Gst.Application.Init ();
			MainWindow window = new MainWindow ();
			window.ShowAll ();

			switch (System.Environment.OSVersion.Platform) {
				case PlatformID.Unix:
				window._xWindowId = gdk_x11_window_get_xid (window._da.GdkWindow.Handle);
				break;
				case PlatformID.Win32NT:
				case PlatformID.Win32S:
				case PlatformID.Win32Windows:
				case PlatformID.WinCE:
				window._xWindowId = gdk_win32_drawable_get_handle (window._da.GdkWindow.Handle);
				break;
			}

			Gtk.Application.Run ();
		}

		public MainWindow ()
			: base ("Overlaytest") {
			VBox vBox = new VBox ();

			_da = new DrawingArea ();
			_da.ModifyBg (Gtk.StateType.Normal, new Gdk.Color (0, 0, 0));
			_da.SetSizeRequest (400, 300);
			_da.DoubleBuffered = false;
			vBox.PackStart (_da, false, false, 0);

			_scale = new HScale (0, 1, 0.01);
			_scale.DrawValue = false;
			_scale.ValueChanged += ScaleValueChanged;
			vBox.PackStart (_scale, false, false, 0);

			HBox hBox = new HBox ();

			Button btnOpen = new Button ();
			btnOpen.Label = "Open";
			btnOpen.Clicked += ButtonOpenClicked;

			hBox.PackStart (btnOpen, false, false, 0);

			Button btnPlay = new Button ();
			btnPlay.Label = "Play";
			btnPlay.Clicked += ButtonPlayClicked;

			hBox.PackStart (btnPlay, false, false, 0);

			Button btnPause = new Button ();
			btnPause.Label = "Pause";
			btnPause.Clicked += ButtonPauseClicked;

			hBox.PackStart (btnPause, false, false, 0);

			_lbl = new Label ();
			_lbl.Text = "00:00 / 00:00";

			hBox.PackEnd (_lbl, false, false, 0);

			vBox.PackStart (hBox, false, false, 3);

			Add (vBox);

			WindowPosition = Gtk.WindowPosition.Center;
			DeleteEvent += OnDeleteEvent;

			GLib.Timeout.Add (1000, new GLib.TimeoutHandler (UpdatePos));
		}

		void OnDeleteEvent (object sender, DeleteEventArgs args) {
			Gtk.Application.Quit ();


			if (_playbin != null) {
				_playbin.SetState (Gst.State.Null);
				_playbin.Dispose ();
				_playbin = null;
			}

			args.RetVal = true;
		}

		void ButtonOpenClicked (object sender, EventArgs args) {
			FileChooserDialog dialog = new FileChooserDialog ("Open", this, FileChooserAction.Open, new object[] { "Cancel", ResponseType.Cancel, "Open", ResponseType.Accept });
			dialog.SetCurrentFolder (Environment.GetFolderPath (Environment.SpecialFolder.Personal));

			if (dialog.Run () == (int) ResponseType.Accept) {
				_pipelineOK = false;

				if (_playbin != null) {
					_playbin.SetState (Gst.State.Null);
				} else {
					_playbin = ElementFactory.Make  ("playbin", "playbin");
				}

				_scale.Value = 0;

				if (_playbin == null)
					Console.WriteLine ("Unable to create element 'playbin'");

				_playbin.Bus.EnableSyncMessageEmission ();
				_playbin.Bus.AddSignalWatch ();

				_playbin.Bus.SyncMessage += delegate (object bus, SyncMessageArgs sargs) {
					Gst.Message msg = sargs.Message;

					if (!Gst.Video.Global.IsVideoOverlayPrepareWindowHandleMessage (msg))
						return;

					Element src = msg.Src as Element;
					if (src == null)
						return;

					try {
						src["force-aspect-ratio"] = true;
					}
					catch (PropertyNotFoundException) {}
					Element overlay = null;
					if(src is Gst.Bin)
						overlay = ((Gst.Bin) src).GetByInterface (VideoOverlayAdapter.GType);

					VideoOverlayAdapter adapter = new VideoOverlayAdapter (overlay.Handle);
					adapter.WindowHandle = _xWindowId;
					adapter.HandleEvents (true);
				};

				_playbin.Bus.Message += delegate (object bus, MessageArgs margs) {
					Message message = margs.Message;

					switch (message.Type) {
						case Gst.MessageType.Error:
						GLib.GException err;
						string msg;

						message.ParseError (out err, out msg);
						Console.WriteLine (String.Format ("Error message: {0}", msg));
						_pipelineOK = false;
						break;
						case Gst.MessageType.Eos:
						Console.WriteLine ("EOS");
						break;
					}
				};

				switch (System.Environment.OSVersion.Platform) {
					case PlatformID.Unix:
					_playbin["uri"] = "file://" + dialog.Filename;
					break;
					case PlatformID.Win32NT:
					case PlatformID.Win32S:
					case PlatformID.Win32Windows:
					case PlatformID.WinCE:
					_playbin["uri"] = "file:///" + dialog.Filename.Replace("\\","/");
					break;
				}

				StateChangeReturn sret = _playbin.SetState (Gst.State.Playing);

				if (sret == StateChangeReturn.Async) {
					State state, pending;
					sret = _playbin.GetState (out state, out pending, Gst.Constants.SECOND * 5L);
				}

				if (sret == StateChangeReturn.Success) {
					Console.WriteLine ("State change successful");
					_pipelineOK = true;
				} else {
					Console.WriteLine ("State change failed for {0} ({1})\n", dialog.Filename, sret);
				}
			}

			dialog.Destroy ();
		}

		void ButtonPlayClicked (object sender, EventArgs args) {
			if ( (_playbin != null) && _pipelineOK)
				_playbin.SetState (Gst.State.Playing);
		}

		void ButtonPauseClicked (object sender, EventArgs args) {
			if ( (_playbin != null) && _pipelineOK)
				_playbin.SetState (Gst.State.Paused);
		}

		void ScaleValueChanged (object sender, EventArgs args) {
			if (_updatingScale)
				return;

			long duration;
			Gst.Format fmt = Gst.Format.Time;
			Console.WriteLine ("Trying to seek");

			if ( (_playbin != null) && _pipelineOK && _playbin.QueryDuration (fmt, out duration) && duration != -1) {
				long pos = (long) (duration * _scale.Value);
				Console.WriteLine ("Seek to {0}/{1} ({2}%)", pos, duration, _scale.Value);

				bool ret = _playbin.SeekSimple (Format.Time, SeekFlags.Flush | SeekFlags.KeyUnit, pos);

				Console.WriteLine ("Seeked {0}successfully", (ret ? "" : "not "));
			}
		}

		bool UpdatePos () {
			Gst.Format fmt = Gst.Format.Time;
			long duration, pos;
			pos = 0;
			if ( (_playbin != null) && _pipelineOK &&
			    _playbin.QueryDuration (fmt, out duration) &&
			    _playbin.QueryPosition (fmt, out pos)) {
				_lbl.Text = string.Format ("{0} / {1}", TimeString (pos), TimeString (duration));

				_updatingScale = true;
				_scale.Value = (double) pos / duration;
				_updatingScale = false;
			}

			return true;
		}

		string TimeString (long t) {
			long secs = t / 1000000000;
			int mins = (int) (secs / 60);
			secs = secs - (mins * 60);

			if (mins >= 60) {
				int hours = (int) (mins / 60);
				mins = mins - (hours * 60);

				return string.Format ("{0}:{1:d2}:{2:d2}", hours, mins, secs);
			}

			return string.Format ("{0}:{1:d2}", mins, secs);
		}

		[DllImport ("libgdk-3.so.0") ]
		static extern IntPtr gdk_x11_window_get_xid (IntPtr handle);

		[DllImport ("gdk-win32-3.0-0.dll") ]
		static extern IntPtr gdk_win32_drawable_get_handle (IntPtr handle);

		[DllImport ("libX11.so.6")]
		static extern int XInitThreads ();
	}
}
