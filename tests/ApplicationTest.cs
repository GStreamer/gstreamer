//
// ApplicationTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using NUnit.Framework;

[TestFixture]
public class ApplicationTest {
  [Test]
  public void Init() {
    Gst.Application.Init();
  }

  [Test]
  public void InitArgs() {
    string [] args = { "arg_a", "arg_b" };
    Gst.Application.Init ("gstreamer-sharp-test", ref args);
  }

  [Test]
  public void InitArgsCheck() {
    string [] args = { "arg_a", "arg_b" };
    Gst.Application.InitCheck ("gstreamer-sharp-test", ref args);
  }
}
