//
//  Authors
//    Khaled Mohammed (khaled.mohammed@gmail.com)
//
// (C) 2006
//

using System;
using NUnit.Framework;
using Gst;

[TestFixture]
public class BufferTest {
  [TestFixtureSetUp]
  public void Init() {
    Application.Init();
  }

  [Test]
  public void TestCaps() {
    Gst.Buffer buffer = new Gst.Buffer (4);
    Caps caps = Caps.FromString ("audio/x-raw-int");

    Assert.IsNull (buffer.Caps, "buffer.Caps should be null");
    buffer.Caps = caps;
    Assert.IsNotNull (buffer.Caps, "buffer.Caps is null");

    Caps caps2 = Caps.FromString ("audio/x-raw-float");
    buffer.Caps = caps2;
    Assert.AreNotEqual (buffer.Caps, caps);
    Assert.AreEqual (buffer.Caps, caps2);

    buffer.Caps = null;
    Assert.IsNull (buffer.Caps, "buffer.Caps should be null");
  }

  [Test]
  public void TestSubbuffer() {
    Gst.Buffer buffer = new Gst.Buffer (4);
    Gst.Buffer sub = buffer.CreateSub (1, 2);
    Assert.IsNotNull (sub);
    Assert.AreEqual (sub.Size, 2, "subbuffer has wrong size");
  }

  [Test]
  public void TestIsSpanFast() {
    Gst.Buffer buffer = new Gst.Buffer (4);

    Gst.Buffer sub1 = buffer.CreateSub (0, 2);
    Assert.IsNotNull (sub1, "CreateSub of buffer returned null");

    Gst.Buffer sub2 = buffer.CreateSub (2, 2);
    Assert.IsNotNull (sub2, "CreateSub of buffer returned null");

    Assert.IsFalse (buffer.IsSpanFast (sub2), "a parent buffer can not be SpanFasted");
    Assert.IsFalse (sub1.IsSpanFast (buffer), "a parent buffer can not be SpanFasted");
    Assert.IsTrue (sub1.IsSpanFast (sub2), "two subbuffers next to each other should be SpanFast");
  }

  private void ArrayIsEqual (byte[] a, byte[] b) {
    Assert.IsTrue (a.Length == b.Length);
    for (int i = 0; i < a.Length; i++)
      Assert.IsTrue (a[i] == b[i]);
  }

  [Test]
  public void TestBufferData() {
    byte[] data = new byte[] {0, 1, 2, 3, 4, 5};

    Gst.Buffer buffer = new Gst.Buffer (data);

    ArrayIsEqual (data, buffer.ToByteArray ());

    Gst.Base.ByteReader reader = new Gst.Base.ByteReader (buffer);
    byte b;
    uint u;
    Assert.IsTrue (reader.PeekUInt32Be (out u));
    Assert.IsTrue (u == 0x00010203);
    Assert.IsTrue (reader.GetUInt8 (out b));
    Assert.IsTrue (b == 0 && 0 == data[reader.Pos-1]);
    Assert.IsTrue (reader.GetUInt8 (out b));
    Assert.IsTrue (b == 1 && 1 == data[reader.Pos-1]);
    Assert.IsTrue (reader.GetUInt8 (out b));
    Assert.IsTrue (b == 2 && 2 == data[reader.Pos-1]);
    Assert.IsTrue (reader.GetUInt8 (out b));
    Assert.IsTrue (b == 3 && 3 == data[reader.Pos-1]);
    Assert.IsTrue (reader.GetUInt8 (out b));
    Assert.IsTrue (b == 4 && 4 == data[reader.Pos-1]);
    Assert.IsTrue (reader.GetUInt8 (out b));
    Assert.IsTrue (b == 5 && 5 == data[reader.Pos-1]);
    Assert.IsFalse (reader.GetUInt8 (out b));
    Assert.IsTrue (reader.Pos == buffer.Size);

    Gst.Base.ByteWriter writer = new Gst.Base.ByteWriter (buffer, true);
    Assert.IsTrue (writer.Remaining == buffer.Size);
    Assert.IsTrue (writer.RemainingReadable == buffer.Size);
    Assert.IsTrue (writer.PutUInt8 (5));
    Assert.IsTrue (writer.PutUInt16Be (0x0403));
    Assert.IsTrue (writer.PutUInt8 (2));
    Assert.IsTrue (writer.PutUInt16Le (0x0001));
    Assert.IsTrue (writer.Remaining == 0);
    Assert.IsTrue (writer.SetPos (0));
    Assert.IsTrue (writer.PeekUInt32Be (out u));
    Assert.IsTrue (u == 0x05040302);
    Assert.IsTrue (writer.GetUInt8 (out b));
    Assert.IsTrue (b == 5 && 0 == data[writer.Pos-1]);
    Assert.IsTrue (writer.GetUInt8 (out b));
    Assert.IsTrue (b == 4 && 1 == data[writer.Pos-1]);
    Assert.IsTrue (writer.GetUInt8 (out b));
    Assert.IsTrue (b == 3 && 2 == data[writer.Pos-1]);
    Assert.IsTrue (writer.GetUInt8 (out b));
    Assert.IsTrue (b == 2 && 3 == data[writer.Pos-1]);
    Assert.IsTrue (writer.GetUInt8 (out b));
    Assert.IsTrue (b == 1 && 4 == data[writer.Pos-1]);
    Assert.IsTrue (writer.GetUInt8 (out b));
    Assert.IsTrue (b == 0 && 5 == data[writer.Pos-1]);
    Assert.IsFalse (writer.GetUInt8 (out b));
    Assert.IsTrue (writer.Pos == buffer.Size);

    writer = new Gst.Base.ByteWriter (buffer, false);
    Assert.IsTrue (writer.Remaining == buffer.Size);
    Assert.IsTrue (writer.RemainingReadable == 0);
  }
}
