// Authors
//   Copyright (C) 2008 Paul Burton <paulburton89@gmail.com>
using System;
using System.Runtime.InteropServices;

using Gtk;
using Gst;
using Gst.Interfaces;
using Gst.BasePlugins;

public class MainWindow : Gtk.Window {
  DrawingArea _da;
  Pipeline _pipeline;
  HScale _scale;
  Label _lbl;
  bool _updatingScale;
  bool _pipelineOK;

  public static void Main (string[] args) {
    Gtk.Application.Init ();
    Gst.Application.Init ();
    MainWindow window = new MainWindow ();
    window.ShowAll ();
    Gtk.Application.Run ();
  }

  public MainWindow ()
  : base (WindowType.Toplevel) {
    VBox vBox = new VBox ();

    _da = new DrawingArea ();
    _da.ModifyBg (Gtk.StateType.Normal, new Gdk.Color (0, 0, 0));
    _da.SetSizeRequest (400, 300);
    vBox.PackStart (_da);

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
    args.RetVal = true;
  }

  void ButtonOpenClicked (object sender, EventArgs args) {
    FileChooserDialog dialog = new FileChooserDialog ("Open", this, FileChooserAction.Open, new object[] { "Cancel", ResponseType.Cancel, "Open", ResponseType.Accept });
    dialog.SetCurrentFolder (Environment.GetFolderPath (Environment.SpecialFolder.Personal));

    if (dialog.Run () == (int) ResponseType.Accept) {
      _pipelineOK = false;

      if (_pipeline != null) {
        _pipeline.SetState (Gst.State.Null);
        _pipeline.Dispose ();
      }

      _scale.Value = 0;

      _pipeline = new Pipeline (string.Empty);

      Element playbin = ElementFactory.Make ("playbin", "playbin");
      XvImageSink sink = XvImageSink.Make ("sink");

      if (_pipeline == null)
        Console.WriteLine ("Unable to create pipeline");
      if (playbin == null)
        Console.WriteLine ("Unable to create element 'playbin'");
      if (sink == null)
        Console.WriteLine ("Unable to create element 'sink'");

      _pipeline.Add (playbin);

      sink.XwindowId = gdk_x11_drawable_get_xid (_da.GdkWindow.Handle);

      playbin["video-sink"] = sink;
      playbin["uri"] = "file://" + dialog.Filename;

      StateChangeReturn sret = _pipeline.SetState (Gst.State.Playing);

      if (sret == StateChangeReturn.Async) {
        State state, pending;
        sret = _pipeline.GetState (out state, out pending, Clock.Second * 5);
      }

      if (sret == StateChangeReturn.Success)
        _pipelineOK = true;
      else
        Console.WriteLine ("State change failed for {0} ({1})\n", dialog.Filename, sret);
    }

    dialog.Destroy ();
  }

  void ButtonPlayClicked (object sender, EventArgs args) {
    if ( (_pipeline != null) && _pipelineOK)
      _pipeline.SetState (Gst.State.Playing);
  }

  void ButtonPauseClicked (object sender, EventArgs args) {
    if ( (_pipeline != null) && _pipelineOK)
      _pipeline.SetState (Gst.State.Paused);
  }

  void ScaleValueChanged (object sender, EventArgs args) {
    if (_updatingScale)
      return;

    long duration;
    Gst.Format fmt = Gst.Format.Time;

    if ( (_pipeline != null) && _pipelineOK && _pipeline.QueryDuration (ref fmt, out duration)) {
      long pos = (long) (duration * _scale.Value);
      //Console.WriteLine ("Seek to {0}/{1} ({2}%)", pos, duration, _scale.Value);

      _pipeline.Seek (Format.Time, SeekFlags.Flush, pos);
    }
  }

  bool UpdatePos () {
    Gst.Format fmt = Gst.Format.Time;
    long duration, pos;
    if ( (_pipeline != null) && _pipelineOK &&
         _pipeline.QueryDuration (ref fmt, out duration) &&
         _pipeline.QueryPosition (ref fmt, out pos)) {
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

  [DllImport ("libgdk-x11-2.0") ]
  static extern uint gdk_x11_drawable_get_xid (IntPtr handle);
}
