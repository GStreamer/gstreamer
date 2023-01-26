using System;
using static System.Diagnostics.Debug;
using Gst;
using WebSocketSharp;
using Gst.WebRTC;
using Newtonsoft.Json;
using System.Net.Security;
using System.Security.Cryptography.X509Certificates;
using Gst.Sdp;
using System.Text;
using GLib;

namespace GstWebRTCDemo
{
    class WebRtcClient : IDisposable
    {
        const string SERVER = "wss://127.0.0.1:8443";

        const string PIPELINE_DESC = @"webrtcbin name=sendrecv bundle-policy=max-bundle
 videotestsrc is-live=true pattern=ball ! videoconvert ! queue ! vp8enc deadline=1 ! rtpvp8pay !
 queue ! application/x-rtp,media=video,encoding-name=VP8,payload=97 ! sendrecv.
 audiotestsrc is-live=true wave=red-noise ! audioconvert ! audioresample ! queue ! opusenc ! rtpopuspay !
 queue ! application/x-rtp,media=audio,encoding-name=OPUS,payload=96 ! sendrecv.";

        readonly int _id;
        readonly int _peerId;
        readonly string _server;
        readonly WebSocket _conn;
        Pipeline pipe;
        Element webrtc;
        bool terminate;

        public WebRtcClient(int id, int peerId, string server = SERVER)
        {
            _id = id;
            _peerId = peerId;
            _server = server;

            _conn = new WebSocket(_server);
            _conn.SslConfiguration.ServerCertificateValidationCallback = validatCert;
            _conn.OnOpen += OnOpen;
            _conn.OnError += OnError;
            _conn.OnMessage += OnMessage;
            _conn.OnClose += OnClose;

            pipe = (Pipeline)Parse.Launch(PIPELINE_DESC);
        }

        bool validatCert(object sender, X509Certificate certificate, X509Chain chain, SslPolicyErrors sslPolicyErrors)
        {
            return true;
        }

        public void Connect()
        {
            _conn.ConnectAsync();
        }

        void SetupCall()
        {
            _conn.Send($"SESSION {_peerId}");
        }

        void OnClose(object sender, CloseEventArgs e)
        {
            Console.WriteLine("Closed: " + e.Reason);

            terminate = true;
        }

        void OnError(object sender, ErrorEventArgs e)
        {
            Console.WriteLine("Error " + e.Message);

            terminate = true;
        }

        void OnOpen(object sender, System.EventArgs e)
        {
            var ws = sender as WebSocket;
            ws.SendAsync($"HELLO {_id}", (b) => Console.WriteLine($"Opened {b}"));
        }

        void OnMessage(object sender, MessageEventArgs args)
        {
            var msg = args.Data;
            switch (msg)
            {
                case "HELLO":
                    SetupCall();
                    break;
                case "SESSION_OK":
                    StartPipeline();
                    break;
                default:
                    if (msg.StartsWith("ERROR")) {
                        Console.WriteLine(msg);
                        terminate = true;
                    } else {
                        HandleSdp(msg);
                    }
                    break;
            }
        }

        void StartPipeline()
        {
            webrtc = pipe.GetByName("sendrecv");
            Assert(webrtc != null);
            webrtc.Connect("on-negotiation-needed", OnNegotiationNeeded);
            webrtc.Connect("on-ice-candidate", OnIceCandidate);
            webrtc.Connect("pad-added", OnIncomingStream);
            pipe.SetState(State.Playing);
            Console.WriteLine("Playing");
        }

        #region Webrtc signal handlers
        #region Incoming stream
        void OnIncomingStream(object o, GLib.SignalArgs args)
        {
            var pad = args.Args[0] as Pad;
            if (pad.Direction != PadDirection.Src)
                return;
            var decodebin = ElementFactory.Make("decodebin");
            decodebin.Connect("pad-added", OnIncomingDecodebinStream);
            pipe.Add(decodebin);
            decodebin.SyncStateWithParent();
            webrtc.Link(decodebin);
        }

        void OnIncomingDecodebinStream(object o, SignalArgs args)
        {
            var pad = (Pad)args.Args[0];
            if (!pad.HasCurrentCaps)
            {
                Console.WriteLine($"{pad.Name} has no caps, ignoring");
                return;
            }

            var caps = pad.CurrentCaps;
            Assert(!caps.IsEmpty);
            Structure s = caps[0];
            var name = s.Name;
            if (name.StartsWith("video"))
            {
                var q = ElementFactory.Make("queue");
                var conv = ElementFactory.Make("videoconvert");
                var sink = ElementFactory.Make("autovideosink");
                pipe.Add(q, conv, sink);
                pipe.SyncChildrenStates();
                pad.Link(q.GetStaticPad("sink"));
                Element.Link(q, conv, sink);
            }
            else if (name.StartsWith("audio"))
            {
                var q = ElementFactory.Make("queue");
                var conv = ElementFactory.Make("audioconvert");
                var resample = ElementFactory.Make("audioresample");
                var sink = ElementFactory.Make("autoaudiosink");
                pipe.Add(q, conv, resample, sink);
                pipe.SyncChildrenStates();
                pad.Link(q.GetStaticPad("sink"));
                Element.Link(q, conv, resample, sink);
            }

        }
        #endregion

        void OnIceCandidate(object o, GLib.SignalArgs args)
        {
            var index = (uint)args.Args[0];
            var cand = (string)args.Args[1];
            var obj = new { ice = new { sdpMLineIndex = index, candidate = cand } };
            var iceMsg = JsonConvert.SerializeObject(obj);

            _conn.SendAsync(iceMsg, (b) => { } );
        }

        void OnNegotiationNeeded(object o, GLib.SignalArgs args)
        {
            var webRtc = o as Element;
            Assert(webRtc != null, "not a webrtc object");
            Promise promise = new Promise(OnOfferCreated, webrtc.Handle, null); // webRtc.Handle, null);
            Structure structure = new Structure("struct");
            webrtc.Emit("create-offer", structure, promise);
        }

        void OnOfferCreated(Promise promise)
        {
            promise.Wait();
            var reply = promise.RetrieveReply();
            var gval = reply.GetValue("offer");
            WebRTCSessionDescription offer = (WebRTCSessionDescription)gval.Val;
            promise = new Promise();
            webrtc.Emit("set-local-description", offer, promise);
            promise.Interrupt();
            SendSdpOffer(offer) ;
        }
        #endregion

        void SendSdpOffer(WebRTCSessionDescription offer)
        {
            var text = offer.Sdp.AsText();
            var obj = new { sdp = new { type = "offer", sdp = text } };
            var json = JsonConvert.SerializeObject(obj);
            Console.Write(json);

            _conn.SendAsync(json, (b) => Console.WriteLine($"Send offer completed {b}"));
        }

        void HandleSdp(string message)
        {
            var msg = JsonConvert.DeserializeObject<dynamic>(message);

            if (msg.sdp != null)
            {
                var sdp = msg.sdp;
                if (sdp.type != null && sdp.type != "answer")
                {
                    throw new Exception("Not an answer");
                }
                string sdpAns = sdp.sdp;
                Console.WriteLine($"received answer:\n{sdpAns}");
                SDPMessage.New(out SDPMessage sdpMsg);
                SDPMessage.ParseBuffer(ASCIIEncoding.Default.GetBytes(sdpAns), (uint)sdpAns.Length, sdpMsg);
                var answer = WebRTCSessionDescription.New(WebRTCSDPType.Answer, sdpMsg);
                var promise = new Promise();
                webrtc.Emit("set-remote-description", answer, promise);
            }
            else if (msg.ice != null)
            {
                var ice = msg.ice;
                string candidate = ice.candidate;
                uint sdpMLineIndex = ice.sdpMLineIndex;
                webrtc.Emit("add-ice-candidate", sdpMLineIndex, candidate);
            }
        }

        public void Run()
        {
            // Wait until error, EOS or State Change
            var bus = pipe.Bus;
            do {
                var msg = bus.TimedPopFiltered (Gst.Constants.SECOND, MessageType.Error | MessageType.Eos | MessageType.StateChanged);
                // Parse message
                if (msg != null) {
                    switch (msg.Type) {
                    case MessageType.Error:
                        string debug;
                        GLib.GException exc;
                        msg.ParseError (out exc, out debug);
                        Console.WriteLine ("Error received from element {0}: {1}", msg.Src.Name, exc.Message);
                        Console.WriteLine ("Debugging information: {0}", debug != null ? debug : "none");
                        terminate = true;
                        break;
                    case MessageType.Eos:
                        Console.WriteLine ("End-Of-Stream reached.\n");
                        terminate = true;
                        break;
                    case MessageType.StateChanged:
                        // We are only interested in state-changed messages from the pipeline
                        if (msg.Src == pipe) {
                            State oldState, newState, pendingState;
                            msg.ParseStateChanged (out oldState, out newState, out pendingState);
                            Console.WriteLine ("Pipeline state changed from {0} to {1}:",
                                Element.StateGetName (oldState), Element.StateGetName (newState));
                        }
                        break;
                    default:
                        // We should not reach here because we only asked for ERRORs, EOS and STATE_CHANGED
                        Console.WriteLine ("Unexpected message received.");
                        break;
                    }
                }
            } while (!terminate);
        }
        public void Dispose()
        {
            ((IDisposable)_conn).Dispose();
            pipe.SetState(State.Null);
            pipe.Dispose();
        }
    }

    static class WebRtcSendRcv
    {
        const string SERVER = "wss://webrtc.gstreamer.net:8443";
        static Random random = new Random();

        public static void Main(string[] args)
        {
            // Initialize GStreamer
            Gst.Application.Init (ref args);

            if (args.Length == 0)
                throw new Exception("need peerId");
            int peerId = Int32.Parse(args[0]);
            var server = (args.Length > 1) ? args[1] : SERVER;

            var ourId = random.Next(100, 10000);
            Console.WriteLine($"PeerId:{peerId} OurId:{ourId} ");
            var c = new WebRtcClient(ourId, peerId, server);
            c.Connect();
            c.Run();
            c.Dispose();
        }
    }

}
