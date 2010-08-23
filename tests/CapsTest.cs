//
// CapsTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors:
//   Michael Dominic K. (michaldominik@gmail.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using NUnit.Framework;
using Gst;
using Gst.Video;

[TestFixture]
public class CapsTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  [Test]
  public void TestPlainCreation() {
    Caps caps = new Caps();
    Assert.IsNotNull (caps);
    Assert.IsFalse (caps.Handle == IntPtr.Zero, "Ooops, null handle");
  }

  [Test]
  public void TestFromString() {
    Caps caps = Caps.FromString ("video/x-raw-yuv, " +
                                 "format=(fourcc)I420, " +
                                 "width=(int)384, " +
                                 "height=(int)288, " +
                                 "framerate=(fraction)25/1");
    Assert.IsNotNull (caps);

    Assert.IsFalse (caps.Handle == IntPtr.Zero, "Ooops, null handle");
    Assert.IsTrue (caps.IsFixed, "Caps should be FIXED!");
    Assert.IsFalse (caps.IsEmpty, "Caps shouldn't be EMPTY!");
    Assert.IsFalse (caps.IsAny, "Caps shouldn't be ANY!");
  }

  [Test]
  public void TestIntersecting() {
    Caps caps1 = Caps.FromString ("video/x-raw-yuv, " +
                                  "format=(fourcc)I420, " +
                                  "width=(int)[ 1,1000 ], " +
                                  "height=(int)[ 1, 1000 ], " +
                                  "framerate=(fraction)[ 0/1, 100/1 ]");
    Caps caps2 = Caps.FromString ("video/x-raw-yuv, " +
                                  "format=(fourcc)I420, " +
                                  "width=(int)640, " +
                                  "height=(int)480");
    Assert.IsNotNull (caps1);
    Assert.IsNotNull (caps2);

    Assert.IsFalse (caps1.Handle == IntPtr.Zero, "Ooops, null handle in caps1");
    Assert.IsFalse (caps1.Handle == IntPtr.Zero, "Ooops, null handle in caps2");

    Caps caps3 = caps1.Intersect (caps2);

    Assert.IsFalse (caps3.IsFixed, "How come caps are FIXED?!");
    Assert.IsFalse (caps3.IsEmpty, "How come caps are EMPTY?!");

    Assert.AreEqual (caps2.ToString() + ", framerate=(fraction)[ 0/1, 100/1 ]", caps3.ToString());
  }

  [Test]
  public void TestUnion() {
    Caps caps1 = Caps.FromString ("video/x-raw-yuv, " +
                                  "format=(fourcc)I420, " +
                                  "width=(int)640");
    Caps caps2 = Caps.FromString ("video/x-raw-yuv, " +
                                  "format=(fourcc)I420, " +
                                  "height=(int)480");
    Assert.IsNotNull (caps1);
    Assert.IsNotNull (caps2);

    Assert.IsFalse (caps1.Handle == IntPtr.Zero, "Ooops, null handle in caps1");
    Assert.IsFalse (caps1.Handle == IntPtr.Zero, "Ooops, null handle in caps2");

    Caps caps3 = caps1.Union (caps2);

    Assert.IsFalse (caps3.IsEmpty, "How come caps are EMPTY?!");

    Caps caps4 = Caps.FromString ("video/x-raw-yuv, " +
                                  "format=(fourcc)I420, " +
                                  "width=(int)640; " +
                                  "video/x-raw-yuv, " +
                                  "format=(fourcc)I420, " +
                                  "height=(int)480");
    Assert.IsTrue (caps3.IsEqual (caps4));
  }

  [Test]
  public void TestManagedReferences() {
    Caps tmp = VideoUtil.FormatToTemplateCaps(Gst.Video.VideoFormat.RGBX);
    Caps caps = tmp.Copy();
    caps[0]["width"] = 640;
    caps[0]["height"] = 480;
    
    caps.Append(tmp);
    Caps any = Caps.NewAny();
    caps.Merge(any);
  }
}
