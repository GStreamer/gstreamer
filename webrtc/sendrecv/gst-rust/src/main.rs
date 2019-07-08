extern crate clap;
#[macro_use]
extern crate failure;
extern crate glib;
#[macro_use]
extern crate gstreamer as gst;
extern crate gstreamer_sdp as gst_sdp;
extern crate gstreamer_webrtc as gst_webrtc;
extern crate rand;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;
extern crate tokio;
extern crate websocket;
#[macro_use]
extern crate lazy_static;

use failure::Error;
use gst::prelude::*;
use rand::Rng;
use std::sync::{Arc, Mutex, Weak};
use tokio::prelude::*;
use tokio::sync::mpsc;
use websocket::message::OwnedMessage;

const STUN_SERVER: &str = "stun://stun.l.google.com:19302";

lazy_static! {
    static ref RTP_CAPS_OPUS: gst::Caps = {
        gst::Caps::new_simple(
            "application/x-rtp",
            &[
                ("media", &"audio"),
                ("encoding-name", &"OPUS"),
                ("payload", &(97i32)),
            ],
        )
    };
    static ref RTP_CAPS_VP8: gst::Caps = {
        gst::Caps::new_simple(
            "application/x-rtp",
            &[
                ("media", &"video"),
                ("encoding-name", &"VP8"),
                ("payload", &(96i32)),
            ],
        )
    };
}

#[derive(Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
enum JsonMsg {
    Ice {
        candidate: String,
        #[serde(rename = "sdpMLineIndex")]
        sdp_mline_index: u32,
    },
    Sdp {
        #[serde(rename = "type")]
        type_: String,
        sdp: String,
    },
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
enum MediaType {
    Audio,
    Video,
}

// Strong reference to our application state
#[derive(Debug, Clone)]
struct App(Arc<AppInner>);

// Weak reference to our application state
#[derive(Debug, Clone)]
struct AppWeak(Weak<AppInner>);

// Actual application state
#[derive(Debug)]
struct AppInner {
    // None if we wait for a peer to appear
    peer_id: Option<String>,
    pipeline: gst::Pipeline,
    webrtcbin: gst::Element,
    send_msg_tx: Mutex<mpsc::UnboundedSender<OwnedMessage>>,
    rtx: bool,
}

// Various error types for the different errors that can happen here
#[derive(Debug, Fail)]
#[fail(display = "WebSocket error: {:?}", _0)]
struct WebSocketError(websocket::WebSocketError);

#[derive(Debug, Fail)]
#[fail(display = "GStreamer error: {:?}", _0)]
struct GStreamerError(String);

#[derive(Debug, Fail)]
#[fail(display = "Peer error: {:?}", _0)]
struct PeerError(String);

#[derive(Debug, Fail)]
#[fail(display = "Missing elements {:?}", _0)]
struct MissingElements(Vec<&'static str>);

// upgrade weak reference or return
#[macro_export]
macro_rules! upgrade_weak {
    ($x:ident, $r:expr) => {{
        match $x.upgrade() {
            Some(o) => o,
            None => return $r,
        }
    }};
    ($x:ident) => {
        upgrade_weak!($x, ())
    };
}

impl AppWeak {
    // Try upgrading a weak reference to a strong one
    fn upgrade(&self) -> Option<App> {
        self.0.upgrade().map(App)
    }
}

impl Drop for AppInner {
    fn drop(&mut self) {
        // When dropping we need to ensure that the final pipeline state is actually Null
        self.pipeline.set_state(gst::State::Null).unwrap();
    }
}

impl App {
    // Downgrade the strong reference to a weak reference
    fn downgrade(&self) -> AppWeak {
        AppWeak(Arc::downgrade(&self.0))
    }

    // Post an error message on the bus to asynchronously handle anything that goes
    // wrong on GStreamer threads
    fn post_error(&self, msg: &str) {
        gst_element_error!(self.0.pipeline, gst::LibraryError::Failed, (msg));
    }

    // Send a plain text message asynchronously over the WebSocket connection. This can
    // be called from any thread at any time and would send the actual message from the
    // IO threads of the runtime
    fn send_text_msg(&self, msg: String) -> Result<(), Error> {
        self.0
            .send_msg_tx
            .lock()
            .unwrap()
            .try_send(OwnedMessage::Text(msg))
            .map_err(|_| {
                WebSocketError(websocket::WebSocketError::IoError(std::io::Error::new(
                    std::io::ErrorKind::BrokenPipe,
                    "Connection Closed",
                )))
                .into()
            })
    }

    // Send our SDP offer via the WebSocket connection to the peer as JSON message
    fn send_sdp_offer(&self, offer: &gst_webrtc::WebRTCSessionDescription) -> Result<(), Error> {
        let message = serde_json::to_string(&JsonMsg::Sdp {
            type_: "offer".to_string(),
            sdp: offer.get_sdp().as_text().unwrap(),
        })
        .unwrap();

        println!("Sending SDP offer to peer: {}", message);

        self.send_text_msg(message)
    }

    // Once webrtcbin has create the offer SDP for us, handle it by sending it to the peer via the
    // WebSocket connection
    fn on_offer_created(&self, promise: &gst::Promise) -> Result<(), Error> {
        let reply = match promise.wait() {
            gst::PromiseResult::Replied => promise.get_reply().unwrap(),
            err => {
                return Err(GStreamerError(format!(
                    "Offer creation future got no reponse: {:?}",
                    err
                ))
                .into());
            }
        };

        let offer = reply
            .get_value("offer")
            .unwrap()
            .get::<gst_webrtc::WebRTCSessionDescription>()
            .expect("Invalid argument");
        self.0
            .webrtcbin
            .emit("set-local-description", &[&offer, &None::<gst::Promise>])
            .unwrap();

        self.send_sdp_offer(&offer)
    }

    // Send our SDP answer via the WebSocket connection to the peer as JSON message
    fn send_sdp_answer(&self, offer: &gst_webrtc::WebRTCSessionDescription) -> Result<(), Error> {
        let message = serde_json::to_string(&JsonMsg::Sdp {
            type_: "answer".to_string(),
            sdp: offer.get_sdp().as_text().unwrap(),
        })
        .unwrap();

        println!("Sending SDP answer to peer: {}", message);

        self.send_text_msg(message)
    }

    // Once webrtcbin has create the answer SDP for us, handle it by sending it to the peer via the
    // WebSocket connection
    fn on_answer_created(&self, promise: &gst::Promise) -> Result<(), Error> {
        let reply = match promise.wait() {
            gst::PromiseResult::Replied => promise.get_reply().unwrap(),
            err => {
                return Err(GStreamerError(format!(
                    "Offer creation future got no reponse: {:?}",
                    err
                ))
                .into());
            }
        };

        let answer = reply
            .get_value("answer")
            .unwrap()
            .get::<gst_webrtc::WebRTCSessionDescription>()
            .expect("Invalid argument");
        self.0
            .webrtcbin
            .emit("set-local-description", &[&answer, &None::<gst::Promise>])
            .unwrap();

        self.send_sdp_answer(&answer)
    }

    // Whenever webrtcbin tells us that (re-)negotiation is needed, simply ask
    // for a new offer SDP from webrtcbin without any customization and then
    // asynchronously send it to the peer via the WebSocket connection
    fn on_negotiation_needed(&self) -> Result<(), Error> {
        println!("Starting negotiation");

        let app_clone = self.downgrade();
        let promise = gst::Promise::new_with_change_func(move |promise| {
            let app = upgrade_weak!(app_clone);

            if let Err(err) = app.on_offer_created(promise) {
                app.post_error(format!("Failed to send SDP offer: {:?}", err).as_str());
            }
        });

        self.0
            .webrtcbin
            .emit("create-offer", &[&None::<gst::Structure>, &promise])
            .unwrap();

        Ok(())
    }

    // Handle a newly decoded stream from decodebin, i.e. one of the streams that the peer is
    // sending to us. Connect it to newly create sink elements and converters.
    fn handle_media_stream(&self, pad: &gst::Pad, media_type: MediaType) -> Result<(), Error> {
        println!("Trying to handle stream {:?}", media_type);

        let (q, conv, sink) = match media_type {
            MediaType::Audio => {
                let q = gst::ElementFactory::make("queue", None).unwrap();
                let conv = gst::ElementFactory::make("audioconvert", None).unwrap();
                let sink = gst::ElementFactory::make("autoaudiosink", None).unwrap();
                let resample = gst::ElementFactory::make("audioresample", None).unwrap();

                self.0
                    .pipeline
                    .add_many(&[&q, &conv, &resample, &sink])
                    .unwrap();
                gst::Element::link_many(&[&q, &conv, &resample, &sink])?;

                resample.sync_state_with_parent()?;

                (q, conv, sink)
            }
            MediaType::Video => {
                let q = gst::ElementFactory::make("queue", None).unwrap();
                let conv = gst::ElementFactory::make("videoconvert", None).unwrap();
                let sink = gst::ElementFactory::make("autovideosink", None).unwrap();

                self.0.pipeline.add_many(&[&q, &conv, &sink]).unwrap();
                gst::Element::link_many(&[&q, &conv, &sink])?;

                (q, conv, sink)
            }
        };

        q.sync_state_with_parent()?;
        conv.sync_state_with_parent()?;
        sink.sync_state_with_parent()?;

        let qpad = q.get_static_pad("sink").unwrap();
        pad.link(&qpad)?;

        Ok(())
    }

    // Handle a newly decoded decodebin stream and depending on its type, create the relevant
    // elements or simply ignore it
    fn on_incoming_decodebin_stream(&self, pad: &gst::Pad) -> Result<(), Error> {
        let caps = pad.get_current_caps().unwrap();
        let name = caps.get_structure(0).unwrap().get_name();

        if name.starts_with("video/") {
            self.handle_media_stream(&pad, MediaType::Video)
        } else if name.starts_with("audio/") {
            self.handle_media_stream(&pad, MediaType::Audio)
        } else {
            println!("Unknown pad {:?}, ignoring", pad);
            Ok(())
        }
    }

    // Whenever there's a new incoming, encoded stream from the peer create a new decodebin
    fn on_incoming_stream(&self, pad: &gst::Pad) -> Result<(), Error> {
        // Early return for the source pads we're adding ourselves
        if pad.get_direction() != gst::PadDirection::Src {
            return Ok(());
        }

        let decodebin = gst::ElementFactory::make("decodebin", None).unwrap();
        let app_clone = self.downgrade();
        decodebin.connect_pad_added(move |_decodebin, pad| {
            let app = upgrade_weak!(app_clone);

            if let Err(err) = app.on_incoming_decodebin_stream(pad) {
                app.post_error(format!("Failed to handle decoded stream: {:?}", err).as_str());
            }
        });

        self.0.pipeline.add(&decodebin).unwrap();

        decodebin.sync_state_with_parent()?;

        let sinkpad = decodebin.get_static_pad("sink").unwrap();
        pad.link(&sinkpad).unwrap();

        Ok(())
    }

    // Asynchronously send ICE candidates to the peer via the WebSocket connection as a JSON
    // message
    fn send_ice_candidate_message(&self, mlineindex: u32, candidate: String) -> Result<(), Error> {
        let message = serde_json::to_string(&JsonMsg::Ice {
            candidate,
            sdp_mline_index: mlineindex,
        })
        .unwrap();

        self.send_text_msg(message)
    }

    // Create a video test source plus encoder for the video stream we send to the peer
    fn add_video_source(&self) -> Result<(), Error> {
        let videotestsrc = gst::ElementFactory::make("videotestsrc", None).unwrap();
        let videoconvert = gst::ElementFactory::make("videoconvert", None).unwrap();
        let queue = gst::ElementFactory::make("queue", None).unwrap();
        let vp8enc = gst::ElementFactory::make("vp8enc", None).unwrap();

        videotestsrc.set_property_from_str("pattern", "ball");
        videotestsrc.set_property("is-live", &true).unwrap();
        vp8enc.set_property("deadline", &1i64).unwrap();

        let rtpvp8pay = gst::ElementFactory::make("rtpvp8pay", None).unwrap();
        let queue2 = gst::ElementFactory::make("queue", None).unwrap();

        self.0
            .pipeline
            .add_many(&[
                &videotestsrc,
                &videoconvert,
                &queue,
                &vp8enc,
                &rtpvp8pay,
                &queue2,
            ])
            .unwrap();

        gst::Element::link_many(&[
            &videotestsrc,
            &videoconvert,
            &queue,
            &vp8enc,
            &rtpvp8pay,
            &queue2,
        ])?;

        queue2.link_filtered(&self.0.webrtcbin, Some(&*RTP_CAPS_VP8))?;

        Ok(())
    }

    // Create a audio test source plus encoders for the audio stream we send to the peer
    fn add_audio_source(&self) -> Result<(), Error> {
        let audiotestsrc = gst::ElementFactory::make("audiotestsrc", None).unwrap();
        let queue = gst::ElementFactory::make("queue", None).unwrap();
        let audioconvert = gst::ElementFactory::make("audioconvert", None).unwrap();
        let audioresample = gst::ElementFactory::make("audioresample", None).unwrap();
        let queue2 = gst::ElementFactory::make("queue", None).unwrap();
        let opusenc = gst::ElementFactory::make("opusenc", None).unwrap();
        let rtpopuspay = gst::ElementFactory::make("rtpopuspay", None).unwrap();
        let queue3 = gst::ElementFactory::make("queue", None).unwrap();

        audiotestsrc.set_property_from_str("wave", "red-noise");
        audiotestsrc.set_property("is-live", &true).unwrap();

        self.0
            .pipeline
            .add_many(&[
                &audiotestsrc,
                &queue,
                &audioconvert,
                &audioresample,
                &queue2,
                &opusenc,
                &rtpopuspay,
                &queue3,
            ])
            .unwrap();

        gst::Element::link_many(&[
            &audiotestsrc,
            &queue,
            &audioconvert,
            &audioresample,
            &queue2,
            &opusenc,
            &rtpopuspay,
            &queue3,
        ])?;

        queue3.link_filtered(&self.0.webrtcbin, Some(&*RTP_CAPS_OPUS))?;

        Ok(())
    }

    // Finish creating our pipeline and actually start it once the connection with the peer is
    // there
    fn setup_pipeline(&self) -> Result<(), Error> {
        println!("Start pipeline");

        // Whenever (re-)negotiation is needed, do so but this is only needed if
        // we send the initial offer
        if self.0.peer_id.is_some() {
            let app_clone = self.downgrade();
            self.0
                .webrtcbin
                .connect("on-negotiation-needed", false, move |values| {
                    let _webrtc = values[0].get::<gst::Element>().unwrap();

                    let app = upgrade_weak!(app_clone, None);

                    if let Err(err) = app.on_negotiation_needed() {
                        app.post_error(format!("Failed to start negotiation: {:?}", err).as_str());
                    }

                    None
                })
                .unwrap();
        }

        let app_clone = self.downgrade();
        self.0
            .webrtcbin
            .connect("on-new-transceiver", false, move |values| {
                let _webrtc = values[0].get::<gst::Element>().unwrap();
                let transceiver = values[1].get::<glib::Object>().unwrap();

                let app = upgrade_weak!(app_clone, None);

                transceiver.set_property("do-nack", &app.0.rtx).unwrap();

                None
            })
            .unwrap();

        // Whenever there is a new ICE candidate, send it to the peer
        let app_clone = self.downgrade();
        self.0
            .webrtcbin
            .connect("on-ice-candidate", false, move |values| {
                let _webrtc = values[0].get::<gst::Element>().expect("Invalid argument");
                let mlineindex = values[1].get::<u32>().expect("Invalid argument");
                let candidate = values[2].get::<String>().expect("Invalid argument");

                let app = upgrade_weak!(app_clone, None);

                if let Err(err) = app.send_ice_candidate_message(mlineindex, candidate) {
                    app.post_error(format!("Failed to send ICE candidate: {:?}", err).as_str());
                }

                None
            })
            .unwrap();

        // Whenever there is a new stream incoming from the peer, handle it
        let app_clone = self.downgrade();
        self.0.webrtcbin.connect_pad_added(move |_webrtc, pad| {
            let app = upgrade_weak!(app_clone);

            if let Err(err) = app.on_incoming_stream(pad) {
                app.post_error(format!("Failed to handle incoming stream: {:?}", err).as_str());
            }
        });

        // Create our audio/video sources we send to the peer
        self.add_video_source()?;
        self.add_audio_source()?;

        Ok(())
    }

    // Send ID of the peer we want to talk to via the WebSocket connection
    fn setup_call(&self, peer_id: &str) -> Result<OwnedMessage, Error> {
        println!("Setting up signalling server call with {}", peer_id);
        Ok(OwnedMessage::Text(format!("SESSION {}", peer_id)))
    }

    // Once we got the HELLO message from the WebSocket connection, start setting up the call
    fn handle_hello(&self) -> Result<Option<OwnedMessage>, Error> {
        if let Some(ref peer_id) = self.0.peer_id {
            self.setup_call(peer_id).map(Some)
        } else {
            // Wait for a peer to appear
            Ok(None)
        }
    }

    // Once the session is set up correctly we start our pipeline
    fn handle_session_ok(&self) -> Result<Option<OwnedMessage>, Error> {
        self.setup_pipeline()?;

        // And finally asynchronously start our pipeline
        let app_clone = self.downgrade();
        self.0.pipeline.call_async(move |pipeline| {
            let app = upgrade_weak!(app_clone);

            if let Err(err) = pipeline.set_state(gst::State::Playing) {
                app.post_error(format!("Failed to set pipeline to Playing: {:?}", err).as_str());
            }
        });

        Ok(None)
    }

    // Handle errors from the peer send to us via the WebSocket connection
    fn handle_error(&self, msg: &str) -> Result<Option<OwnedMessage>, Error> {
        println!("Got error message! {}", msg);

        Err(PeerError(msg.into()).into())
    }

    // Handle incoming SDP answers from the peer
    fn handle_sdp(&self, type_: &str, sdp: &str) -> Result<Option<OwnedMessage>, Error> {
        if type_ == "answer" {
            print!("Received answer:\n{}\n", sdp);

            let ret = gst_sdp::SDPMessage::parse_buffer(sdp.as_bytes())
                .map_err(|_| GStreamerError("Failed to parse SDP answer".into()))?;
            let answer =
                gst_webrtc::WebRTCSessionDescription::new(gst_webrtc::WebRTCSDPType::Answer, ret);
            self.0
                .webrtcbin
                .emit("set-remote-description", &[&answer, &None::<gst::Promise>])
                .unwrap();

            Ok(None)
        } else if type_ == "offer" {
            print!("Received offer:\n{}\n", sdp);

            // Need to start the pipeline as a first step here
            self.setup_pipeline()?;

            let ret = gst_sdp::SDPMessage::parse_buffer(sdp.as_bytes())
                .map_err(|_| GStreamerError("Failed to parse SDP offer".into()))?;

            // And then asynchronously start our pipeline and do the next steps. The
            // pipeline needs to be started before we can create an answer
            let app_clone = self.downgrade();
            self.0.pipeline.call_async(move |pipeline| {
                let app = upgrade_weak!(app_clone);

                if let Err(err) = pipeline.set_state(gst::State::Playing) {
                    app.post_error(
                        format!("Failed to set pipeline to Playing: {:?}", err).as_str(),
                    );
                    return;
                }

                let offer = gst_webrtc::WebRTCSessionDescription::new(
                    gst_webrtc::WebRTCSDPType::Offer,
                    ret,
                );

                app.0
                    .webrtcbin
                    .emit("set-remote-description", &[&offer, &None::<gst::Promise>])
                    .unwrap();

                let app_clone = app.downgrade();
                let promise = gst::Promise::new_with_change_func(move |promise| {
                    let app = upgrade_weak!(app_clone);

                    if let Err(err) = app.on_answer_created(promise) {
                        app.post_error(format!("Failed to send SDP answer: {:?}", err).as_str());
                    }
                });

                app.0
                    .webrtcbin
                    .emit("create-answer", &[&None::<gst::Structure>, &promise])
                    .unwrap();
            });

            Ok(None)
        } else {
            Err(PeerError(format!("Sdp type is not \"answer\" but \"{}\"", type_)).into())
        }
    }

    // Handle incoming ICE candidates from the peer by passing them to webrtcbin
    fn handle_ice(
        &self,
        sdp_mline_index: u32,
        candidate: &str,
    ) -> Result<Option<OwnedMessage>, Error> {
        self.0
            .webrtcbin
            .emit("add-ice-candidate", &[&sdp_mline_index, &candidate])
            .unwrap();

        Ok(None)
    }

    // Handle messages we got from the peer via the WebSocket connection
    fn on_message(&self, msg: &str) -> Result<Option<OwnedMessage>, Error> {
        match msg {
            "HELLO" => self.handle_hello(),

            "SESSION_OK" => self.handle_session_ok(),

            x if x.starts_with("ERROR") => self.handle_error(msg),

            _ => {
                let json_msg: JsonMsg = serde_json::from_str(msg)?;

                match json_msg {
                    JsonMsg::Sdp { type_, sdp } => self.handle_sdp(&type_, &sdp),
                    JsonMsg::Ice {
                        sdp_mline_index,
                        candidate,
                    } => self.handle_ice(sdp_mline_index, &candidate),
                }
            }
        }
    }

    // Handle WebSocket messages, both our own as well as WebSocket protocol messages
    fn handle_websocket_message(
        &self,
        message: OwnedMessage,
    ) -> Result<Option<OwnedMessage>, Error> {
        match message {
            OwnedMessage::Close(_) => Ok(Some(OwnedMessage::Close(None))),

            OwnedMessage::Ping(data) => Ok(Some(OwnedMessage::Pong(data))),

            OwnedMessage::Text(msg) => self.on_message(&msg),
            OwnedMessage::Binary(_) => Ok(None),
            OwnedMessage::Pong(_) => Ok(None),
        }
    }

    // Handle GStreamer messages coming from the pipeline
    fn handle_pipeline_message(
        &self,
        message: &gst::Message,
    ) -> Result<Option<OwnedMessage>, Error> {
        use gst::message::MessageView;

        match message.view() {
            MessageView::Error(err) => Err(GStreamerError(format!(
                "Error from element {}: {} ({})",
                err.get_src()
                    .map(|s| String::from(s.get_path_string()))
                    .unwrap_or_else(|| String::from("None")),
                err.get_error(),
                err.get_debug().unwrap_or_else(|| String::from("None")),
            ))
            .into()),
            MessageView::Warning(warning) => {
                println!("Warning: \"{}\"", warning.get_debug().unwrap());
                Ok(None)
            }
            _ => Ok(None),
        }
    }

    // Entry-point to start everything
    fn register_with_server(&self) {
        let our_id = rand::thread_rng().gen_range(10, 10_000);
        println!("Registering id {} with server", our_id);
        self.send_text_msg(format!("HELLO {}", our_id)).unwrap();
    }
}

fn parse_args() -> (String, Option<String>, bool) {
    let matches = clap::App::new("Sendrecv rust")
        .arg(
            clap::Arg::with_name("peer-id")
                .help("String ID of the peer to connect to")
                .long("peer-id")
                .required(false)
                .takes_value(true),
        )
        .arg(
            clap::Arg::with_name("server")
                .help("Signalling server to connect to")
                .long("server")
                .required(false)
                .takes_value(true),
        )
        .arg(
            clap::Arg::with_name("rtx")
                .help("Enable retransmissions (RTX)")
                .long("rtx")
                .required(false),
        )
        .get_matches();

    let server = matches
        .value_of("server")
        .unwrap_or("wss://webrtc.nirbheek.in:8443");

    let peer_id = matches.value_of("peer-id");

    let rtx = matches.is_present("rtx");

    (server.to_string(), peer_id.map(String::from), rtx)
}

fn check_plugins() -> Result<(), Error> {
    let needed = [
        "opus",
        "vpx",
        "nice",
        "webrtc",
        "dtls",
        "srtp",
        "rtpmanager",
        "videotestsrc",
        "audiotestsrc",
    ];

    let registry = gst::Registry::get();
    let missing = needed
        .iter()
        .filter(|n| registry.find_plugin(n).is_none())
        .cloned()
        .collect::<Vec<_>>();

    if !missing.is_empty() {
        Err(MissingElements(missing))?
    } else {
        Ok(())
    }
}

fn main() {
    gst::init().unwrap();
    if let Err(err) = check_plugins() {
        println!("{:?}", err);
        return;
    }

    let (server, peer_id, rtx) = parse_args();

    let mut runtime = tokio::runtime::Runtime::new().unwrap();

    println!("Connecting to server {}", server);
    let res = runtime.block_on(
        websocket::client::ClientBuilder::new(&server)
            .unwrap()
            .async_connect(None)
            .map_err(|err| Error::from(WebSocketError(err)))
            .and_then(move |(stream, _)| {
                println!("connected");

                // Create basic pipeline
                let pipeline = gst::Pipeline::new(Some("main"));
                let webrtcbin = gst::ElementFactory::make("webrtcbin", None).unwrap();
                pipeline.add(&webrtcbin).unwrap();

                webrtcbin.set_property_from_str("stun-server", STUN_SERVER);
                webrtcbin.set_property_from_str("bundle-policy", "max-bundle");

                let bus = pipeline.get_bus().unwrap();

                // Send our bus messages via a futures channel to be handled asynchronously
                let (send_gst_msg_tx, send_gst_msg_rx) = mpsc::unbounded_channel::<gst::Message>();
                let send_gst_msg_tx = Mutex::new(send_gst_msg_tx);
                bus.set_sync_handler(move |_, msg| {
                    let _ = send_gst_msg_tx.lock().unwrap().try_send(msg.clone());
                    gst::BusSyncReply::Pass
                });

                // Create our application control logic
                let (send_ws_msg_tx, send_ws_msg_rx) = mpsc::unbounded_channel::<OwnedMessage>();
                let app = App(Arc::new(AppInner {
                    peer_id,
                    pipeline,
                    webrtcbin,
                    send_msg_tx: Mutex::new(send_ws_msg_tx),
                    rtx,
                }));

                // Start registration process with the server. This will insert a
                // message into the send_ws_msg channel that will then be sent later
                app.register_with_server();

                // Split the stream into the receive part (stream) and send part (sink)
                let (sink, stream) = stream.split();

                // Pass the WebSocket messages to our application control logic
                // and convert them into potential messages to send out
                let app_clone = app.clone();
                let ws_messages = stream
                    .map_err(|err| Error::from(WebSocketError(err)))
                    .and_then(move |msg| app_clone.handle_websocket_message(msg))
                    .filter_map(|msg| msg);

                // Pass the GStreamer messages to the application control logic
                // and convert them into potential messages to send out
                let app_clone = app.clone();
                let gst_messages = send_gst_msg_rx
                    .map_err(Error::from)
                    .and_then(move |msg| app_clone.handle_pipeline_message(&msg))
                    .filter_map(|msg| msg);

                // Merge the two outgoing message streams
                let sync_outgoing_messages = gst_messages.select(ws_messages);

                // And here collect all the asynchronous outgoing messages that come
                // from other threads
                let async_outgoing_messages = send_ws_msg_rx.map_err(Error::from);

                // Merge both outgoing messages streams and send them out directly
                sink.sink_map_err(|err| Error::from(WebSocketError(err)))
                    .send_all(sync_outgoing_messages.select(async_outgoing_messages))
                    .map(|_| ())
            }),
    );

    if let Err(err) = res {
        println!("Error: {:?}", err);
    }

    // And now shut down the runtime
    runtime.shutdown_now().wait().unwrap();
}
