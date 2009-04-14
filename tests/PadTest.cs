//
// PadTest.cs: NUnit Test Suite for gstreamer-sharp
//
// Authors:
//   Michael Dominic K. (michaldominik@gmail.com)
//   Khaled Mohammed (khaled.mohammed@gmail.com)
//   
// (C) 2006 Novell, Inc.
//

using System;
using NUnit.Framework;

using Gst;

[TestFixture]
public class PadTest
{
    [TestFixtureSetUp]
    public void Init()
    {
        Application.Init();
    }

    [TestFixtureTearDown]
    public void Deinit()
    {
        Application.Deinit();
    }

    [Test]
    public void TestPlainCreation()
    {
        Pad src = new Pad("src", PadDirection.Src);
        Pad sink = new Pad("sink", PadDirection.Sink);
        
        Assert.IsNotNull(src);
        Assert.IsNotNull(sink);

        Assert.IsFalse(src.Handle == IntPtr.Zero, "Ooops, src pad has null handle");
        Assert.IsFalse(sink.Handle == IntPtr.Zero, "Ooops, sink pad has null handle");

        Assert.AreEqual(PadDirection.Src, src.Direction);
        Assert.AreEqual(PadDirection.Sink, sink.Direction);

        src.Dispose();
        sink.Dispose();
    }

    public static Caps PadGetCapsStub(Pad pad)
    {
        return Caps.NewFromString("video/x-raw-yuv");
    }

    [Test]
    public void TestFuncAssigning()
    {
        Pad src = new Pad("src", PadDirection.Src);
        src.GetcapsFunction = new PadGetCapsFunction(PadGetCapsStub);

        Caps caps = src.Caps;

        Assert.IsNotNull(caps, "Ooops, returned caps is null");
        Assert.IsFalse(caps.IsEmpty == true, "Ooops, returned caps are empty");
        Assert.AreEqual("video/x-raw-yuv", caps.ToString ());

        caps.Dispose();
        src.Dispose();
    }

    [Test]
    public void TestElementPadAccessByName()
    {
        Element element = ElementFactory.Make("identity", null);
        Assert.IsNotNull(element);
        Assert.IsFalse(element.Handle == IntPtr.Zero, "Ooops, identity element has null handle");

        Pad src = element.GetPad("src");
        Pad sink = element.GetPad("sink");

        Assert.IsNotNull(src, "Ooops, src pad is null");
        Assert.IsNotNull(sink, "Ooops, sink pad is null");

        Assert.IsFalse(src.Handle == IntPtr.Zero, "Ooops, src pad has null handle");
        Assert.IsFalse(sink.Handle == IntPtr.Zero, "Ooops, sink pad has null handle");

        Caps srccaps = src.Caps;
        Assert.IsTrue(srccaps.IsAny, "How come src pad caps is not ANY?");

        Caps sinkcaps = sink.Caps;
        Assert.IsTrue(sinkcaps.IsAny, "How come sink pad caps is not ANY?");

        element.Dispose();
        src.Dispose();
        sink.Dispose();
        srccaps.Dispose();
        sinkcaps.Dispose();
    }

    [Test]
    public void TestElementPadAccessByList()
    {
        Element element = ElementFactory.Make("identity", null);
        Assert.IsNotNull(element);
        Assert.IsFalse(element.Handle == IntPtr.Zero, "Ooops, identity element has null handle");

        Assert.AreEqual(2, element.Numpads);
        Pad [] pads = new Pad [element.Numpads];
        element.Pads.CopyTo (pads, 0);

        bool hassink = false;
        bool hassrc = false;
     
        for(int i = 0; i < element.Numpads; i++) {
            if (pads [i].Name == "src") 
                hassrc = true;
            else if (pads [i].Name == "sink")
                hassink = true;
        }

        Assert.IsTrue(hassink, "Sink pad not found in the list");
        Assert.IsTrue(hassrc, "Src pad not found in the list");

        element.Dispose();
    }

	[Test]
	public void TestLink()
	{
		Pad src = new Pad("source", PadDirection.Src);
		Assert.IsNotNull(src, "Pad could not be created");

		string name = src.Name;
		Assert.AreEqual(name, "source");

		Pad sink = new Pad("sink", PadDirection.Sink);
		Assert.IsNotNull(sink, "Pad could not be created");

		Assert.AreEqual(src.Link(sink), PadLinkReturn.Noformat);

		sink.Dispose();
		src.Dispose();
	}
	
	[Test]
	public void TestGetAllowedCaps()
	{
/*
		Gst.Buffer buffer = new Gst.Buffer();

		try {
			Pad pbuffer = (Pad) buffer;
			Caps pcaps = pbuffer.AllowedCaps;
		}
		catch (Exception ex) {
			Assert.Fail("buffer.AllowedCaps failed");
		}

		buffer.Dispose();
*/
		Pad sink = new Pad("sink", PadDirection.Sink);
//		try { Caps tcaps = sink.AllowedCaps; }
//		catch (Exception) { Assert.Fail("sink.AllowedCaps failed"); }

		Pad src = new Pad("src", PadDirection.Src);
		Assert.IsNotNull(src);
		Caps caps = src.AllowedCaps;
		Assert.IsNull(caps);

		caps = Caps.NewFromString("foo/bar");

		src.SetCaps(caps);
		sink.SetCaps(caps);

		PadLinkReturn plr = src.Link(sink);
		Assert.AreEqual(plr, PadLinkReturn.Ok);

		Caps gotcaps = src.AllowedCaps;
		Assert.IsNotNull(gotcaps);
		Assert.IsTrue(gotcaps.IsEqual(caps));

		gotcaps.Dispose();

		src.Unlink(sink);

		src.Dispose();
		sink.Dispose();
		
		caps.Dispose();
	}

	bool ProbeHandler(Pad pad, Gst.Buffer buffer)
	{
		//Assert.Fail("event worked");
		return false;
	}
	
	[Test]
	public void TestPushUnlinked()
	{
		Pad src = new Pad("src", PadDirection.Src);
		Assert.IsNotNull(src, "Could not create src");
		Caps caps = src.AllowedCaps;
		Assert.IsNull(caps);

		caps = Caps.NewFromString("foo/bar");
		src.SetCaps(caps);

		Gst.Buffer buffer = new Gst.Buffer();
		Assert.AreEqual(src.Push(buffer), FlowReturn.NotLinked);
		buffer.Dispose();

		ulong handler_id = src.AddBufferProbe(new Pad.BufferProbeDelegate(ProbeHandler));
		buffer = new Gst.Buffer();
		Assert.AreEqual(src.Push(buffer), FlowReturn.Ok);
		buffer.Dispose();
		src.RemoveBufferProbe((uint)handler_id);

		src.Dispose();

		caps.Dispose();	
	}
}
