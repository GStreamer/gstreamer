extern crate clap;
#[macro_use]
extern crate failure;
extern crate glib;
extern crate gstreamer as gst;
extern crate gstreamer_sdp as gst_sdp;
extern crate gstreamer_webrtc as gst_webrtc;
extern crate rand;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate serde_json;
extern crate websocket;
#[macro_use]
extern crate lazy_static;

use failure::Error;
use gst::prelude::*;
use rand::Rng;
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use websocket::message::OwnedMessage;

const STUN_SERVER: &str = "stun://stun.l.google.com:19302 ";
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

#[derive(PartialEq, PartialOrd, Eq, Debug, Copy, Clone, Ord)]
enum AppState {
    Error = 1,
    ServerConnected,
    ServerRegistering = 2000,
    ServerRegisteringError,
    ServerRegistered,
    PeerConnecting = 3000,
    PeerConnectionError,
    PeerConnected,
    PeerCallNegotiating = 4000,
    PeerCallStarted,
    PeerCallError,
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

#[derive(Clone)]
struct AppControl(Arc<Mutex<AppControlInner>>);

struct AppControlInner {
    webrtc: Option<gst::Element>,
    app_state: AppState,
    pipeline: gst::Pipeline,
    send_msg_tx: mpsc::Sender<OwnedMessage>,
    peer_id: String,
    main_loop: glib::MainLoop,
    bus: gst::Bus,
}

#[derive(Debug, Fail)]
#[fail(display = "Out of order error: {}", _0)]
struct OutOfOrder(&'static str);

#[derive(Debug, Fail)]
#[fail(display = "Websocket error while app_state was {:?}", _0)]
struct WsError(AppState);

#[derive(Debug, Fail)]
#[fail(display = "Error on bus: {}", _0)]
struct BusError(String);

#[derive(Debug, Fail)]
#[fail(display = "Missing elements {:?}", _0)]
struct MissingElements(Vec<&'static str>);

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
        .filter(|n| registry.find_plugin(n).is_some())
        .map(|n| *n)
        .collect::<Vec<_>>();

    if !missing.is_empty() {
        Err(MissingElements(missing))?
    } else {
        Ok(())
    }
}

fn send_sdp_offer(app_control: &AppControl, offer: &gst_webrtc::WebRTCSessionDescription) {
    if !app_control.assert_app_state_is_at_least(
        AppState::PeerCallNegotiating,
        "Can't send offer, not in call",
    ) {
        return;
    }

    let message = serde_json::to_string(&JsonMsg::Sdp {
        type_: "offer".to_string(),
        sdp: offer.get_sdp().as_text().unwrap(),
    }).unwrap();

    app_control.send_text_msg(message);
}

fn on_offer_created(
    app_control: &AppControl,
    webrtc: &gst::Element,
    promise: &gst::Promise,
) -> Result<(), Error> {
    if !app_control.assert_app_state_is(
        AppState::PeerCallNegotiating,
        "Not negotiating call when creating offer",
    ) {
        return Ok(());
    }

    let reply = promise.get_reply().unwrap();

    let offer = reply
        .get_value("offer")
        .unwrap()
        .get::<gst_webrtc::WebRTCSessionDescription>()
        .expect("Invalid argument");
    webrtc.emit("set-local-description", &[&offer, &None::<gst::Promise>])?;

    send_sdp_offer(&app_control, &offer);

    Ok(())
}

fn on_negotiation_needed(app_control: &AppControl, values: &[glib::Value]) -> Result<(), Error> {
    app_control.0.lock().unwrap().app_state = AppState::PeerCallNegotiating;

    let webrtc = values[0].get::<gst::Element>().unwrap();
    let webrtc_clone = webrtc.clone();
    let app_control_clone = app_control.clone();
    let promise = gst::Promise::new_with_change_func(move |promise| {
        on_offer_created(&app_control_clone, &webrtc, promise).unwrap();
    });

    webrtc_clone.emit("create-offer", &[&None::<gst::Structure>, &promise])?;

    Ok(())
}

fn handle_media_stream(
    pad: &gst::Pad,
    pipe: &gst::Pipeline,
    media_type: MediaType,
) -> Result<(), Error> {
    println!("Trying to handle stream {:?}", media_type);

    let (q, conv, sink) = match media_type {
        MediaType::Audio => {
            let q = gst::ElementFactory::make("queue", None).unwrap();
            let conv = gst::ElementFactory::make("audioconvert", None).unwrap();
            let sink = gst::ElementFactory::make("autoaudiosink", None).unwrap();
            let resample = gst::ElementFactory::make("audioresample", None).unwrap();

            pipe.add_many(&[&q, &conv, &resample, &sink])?;
            gst::Element::link_many(&[&q, &conv, &resample, &sink])?;

            resample.sync_state_with_parent()?;

            (q, conv, sink)
        }
        MediaType::Video => {
            let q = gst::ElementFactory::make("queue", None).unwrap();
            let conv = gst::ElementFactory::make("videoconvert", None).unwrap();
            let sink = gst::ElementFactory::make("autovideosink", None).unwrap();

            pipe.add_many(&[&q, &conv, &sink])?;
            gst::Element::link_many(&[&q, &conv, &sink])?;

            (q, conv, sink)
        }
    };
    q.sync_state_with_parent()?;
    conv.sync_state_with_parent()?;
    sink.sync_state_with_parent()?;

    let qpad = q.get_static_pad("sink").unwrap();
    pad.link(&qpad).into_result()?;

    Ok(())
}

fn on_incoming_decodebin_stream(
    app_control: &AppControl,
    values: &[glib::Value],
    pipe: &gst::Pipeline,
) -> Option<glib::Value> {
    let pad = values[1].get::<gst::Pad>().expect("Invalid argument");
    if !pad.has_current_caps() {
        println!("Pad {:?} has no caps, can't do anything, ignoring", pad);
        return None;
    }

    let caps = pad.get_current_caps().unwrap();
    let name = caps.get_structure(0).unwrap().get_name();

    let handled = if name.starts_with("video") {
        handle_media_stream(&pad, &pipe, MediaType::Video)
    } else if name.starts_with("audio") {
        handle_media_stream(&pad, &pipe, MediaType::Audio)
    } else {
        println!("Unknown pad {:?}, ignoring", pad);
        Ok(())
    };

    if let Err(err) = handled {
        app_control.send_bus_error(&format!("Error adding pad with caps {} {:?}", name, err));
    }

    None
}

fn on_incoming_stream(
    app_control: &AppControl,
    values: &[glib::Value],
    pipe: &gst::Pipeline,
) -> Option<glib::Value> {
    let webrtc = values[0].get::<gst::Element>().expect("Invalid argument");

    let decodebin = gst::ElementFactory::make("decodebin", None).unwrap();
    let pipe_clone = pipe.clone();
    let app_control_clone = app_control.clone();
    decodebin
        .connect("pad-added", false, move |values| {
            on_incoming_decodebin_stream(&app_control_clone, values, &pipe_clone)
        })
        .unwrap();

    pipe.add(&decodebin).unwrap();

    decodebin.sync_state_with_parent().unwrap();
    webrtc.link(&decodebin).unwrap();

    None
}

fn send_ice_candidate_message(app_control: &AppControl, values: &[glib::Value]) {
    if !app_control
        .assert_app_state_is_at_least(AppState::PeerCallNegotiating, "Can't send ICE, not in call")
    {
        return;
    }

    let _webrtc = values[0].get::<gst::Element>().expect("Invalid argument");
    let mlineindex = values[1].get::<u32>().expect("Invalid argument");
    let candidate = values[2].get::<String>().expect("Invalid argument");

    let message = serde_json::to_string(&JsonMsg::Ice {
        candidate,
        sdp_mline_index: mlineindex,
    }).unwrap();

    app_control.send_text_msg(message);
}

fn add_video_source(pipeline: &gst::Pipeline, webrtcbin: &gst::Element) -> Result<(), Error> {
    let videotestsrc = gst::ElementFactory::make("videotestsrc", None).unwrap();
    let videoconvert = gst::ElementFactory::make("videoconvert", None).unwrap();
    let queue = gst::ElementFactory::make("queue", None).unwrap();
    let vp8enc = gst::ElementFactory::make("vp8enc", None).unwrap();

    videotestsrc.set_property_from_str("pattern", "ball");
    vp8enc.set_property("deadline", &1i64).unwrap();

    let rtpvp8pay = gst::ElementFactory::make("rtpvp8pay", None).unwrap();
    let queue2 = gst::ElementFactory::make("queue", None).unwrap();

    pipeline.add_many(&[
        &videotestsrc,
        &videoconvert,
        &queue,
        &vp8enc,
        &rtpvp8pay,
        &queue2,
    ])?;

    gst::Element::link_many(&[
        &videotestsrc,
        &videoconvert,
        &queue,
        &vp8enc,
        &rtpvp8pay,
        &queue2,
    ])?;

    queue2.link_filtered(webrtcbin, &*RTP_CAPS_VP8)?;

    Ok(())
}

fn add_audio_source(pipeline: &gst::Pipeline, webrtcbin: &gst::Element) -> Result<(), Error> {
    let audiotestsrc = gst::ElementFactory::make("audiotestsrc", None).unwrap();
    let queue = gst::ElementFactory::make("queue", None).unwrap();
    let audioconvert = gst::ElementFactory::make("audioconvert", None).unwrap();
    let audioresample = gst::ElementFactory::make("audioresample", None).unwrap();
    let queue2 = gst::ElementFactory::make("queue", None).unwrap();
    let opusenc = gst::ElementFactory::make("opusenc", None).unwrap();
    let rtpopuspay = gst::ElementFactory::make("rtpopuspay", None).unwrap();
    let queue3 = gst::ElementFactory::make("queue", None).unwrap();

    audiotestsrc.set_property_from_str("wave", "red-noise");

    pipeline.add_many(&[
        &audiotestsrc,
        &queue,
        &audioconvert,
        &audioresample,
        &queue2,
        &opusenc,
        &rtpopuspay,
        &queue3,
    ])?;

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

    queue3.link_filtered(webrtcbin, &*RTP_CAPS_OPUS)?;

    Ok(())
}

impl AppControl {
    fn assert_app_state_is(&self, state: AppState, error_msg: &'static str) -> bool {
        if self.0.lock().unwrap().app_state != state {
            self.send_bus_error(error_msg);

            false
        } else {
            true
        }
    }

    fn assert_app_state_is_at_least(&self, state: AppState, error_msg: &'static str) -> bool {
        if self.0.lock().unwrap().app_state < state {
            self.send_bus_error(error_msg);

            false
        } else {
            true
        }
    }

    fn send_bus_error(&self, body: &str) {
        let mbuilder =
            gst::Message::new_application(gst::Structure::new("error", &[("body", &body)]));
        let _ = self.0.lock().unwrap().bus.post(&mbuilder.build());
    }

    fn update_state(&self, state: AppState) {
        self.0.lock().unwrap().app_state = state
    }

    fn send_text_msg(&self, msg: String) {
        self.0
            .lock()
            .unwrap()
            .send_msg_tx
            .send(OwnedMessage::Text(msg))
            .unwrap();
    }

    fn construct_pipeline(&self) -> Result<gst::Pipeline, Error> {
        let pipeline = { self.0.lock().unwrap().pipeline.clone() };

        let webrtcbin = gst::ElementFactory::make("webrtcbin", "sendrecv").unwrap();
        pipeline.add(&webrtcbin)?;

        webrtcbin.set_property_from_str("stun-server", STUN_SERVER);

        add_video_source(&pipeline, &webrtcbin)?;
        add_audio_source(&pipeline, &webrtcbin)?;

        Ok(pipeline)
    }

    fn start_pipeline(&self) -> Result<(), Error> {
        let pipe = self.construct_pipeline()?;
        let webrtc = pipe.get_by_name("sendrecv").unwrap();

        let app_control_clone = self.clone();
        webrtc.connect("on-negotiation-needed", false, move |values| {
            on_negotiation_needed(&app_control_clone, values).unwrap();
            None
        })?;

        let app_control_clone = self.clone();
        webrtc.connect("on-ice-candidate", false, move |values| {
            send_ice_candidate_message(&app_control_clone, values);
            None
        })?;

        let pipe_clone = pipe.clone();
        let app_control_clone = self.clone();
        webrtc.connect("pad-added", false, move |values| {
            on_incoming_stream(&app_control_clone, values, &pipe_clone)
        })?;

        pipe.set_state(gst::State::Playing).into_result()?;

        self.0.lock().unwrap().webrtc = Some(webrtc);

        Ok(())
    }

    fn register_with_server(&self) {
        self.update_state(AppState::ServerRegistering);

        let our_id = rand::thread_rng().gen_range(10, 10_000);
        println!("Registering id {} with server", our_id);
        self.send_text_msg(format!("HELLO {}", our_id));
    }

    fn setup_call(&self) {
        self.update_state(AppState::PeerConnecting);

        let peer_id = { self.0.lock().unwrap().peer_id.clone() };
        println!("Setting up signalling server call with {}", peer_id);
        self.send_text_msg(format!("SESSION {}", peer_id));
    }

    fn handle_hello(&self) -> Result<(), Error> {
        {
            let app_control = self.0.lock().unwrap();
            if app_control.app_state != AppState::ServerRegistering {
                return Err(OutOfOrder("Received HELLO when not registering"))?;
            }
        }

        self.update_state(AppState::ServerRegistered);
        self.setup_call();

        Ok(())
    }

    fn handle_session_ok(&self) -> Result<(), Error> {
        {
            let mut app_control = self.0.lock().unwrap();
            if app_control.app_state != AppState::PeerConnecting {
                return Err(OutOfOrder("Received SESSION_OK when not calling"))?;
            }
            app_control.app_state = AppState::PeerConnected;
        }

        self.start_pipeline()
    }

    fn handle_error(&self) -> Result<(), Error> {
        let app_control = self.0.lock().unwrap();
        let error = match app_control.app_state {
            AppState::ServerRegistering => AppState::ServerRegisteringError,
            AppState::PeerConnecting => AppState::PeerConnectionError,
            AppState::PeerConnected => AppState::PeerCallError,
            AppState::PeerCallNegotiating => AppState::PeerCallError,
            AppState::ServerRegisteringError => AppState::ServerRegisteringError,
            AppState::PeerConnectionError => AppState::PeerConnectionError,
            AppState::PeerCallError => AppState::PeerCallError,
            AppState::Error => AppState::Error,
            AppState::ServerConnected => AppState::Error,
            AppState::ServerRegistered => AppState::Error,
            AppState::PeerCallStarted => AppState::Error,
        };

        Err(WsError(error))?
    }

    fn handle_sdp(&self, type_: &str, sdp: &str) {
        if !self.assert_app_state_is(AppState::PeerCallNegotiating, "Not ready to handle sdp") {
            return;
        }

        if type_ != "answer" {
            self.send_bus_error("Sdp type is not \"answer\"");
            return;
        }

        let mut app_control = self.0.lock().unwrap();

        print!("Received answer:\n{}\n", sdp);

        let ret = gst_sdp::SDPMessage::parse_buffer(sdp.as_bytes()).unwrap();
        let answer =
            gst_webrtc::WebRTCSessionDescription::new(gst_webrtc::WebRTCSDPType::Answer, ret);
        let promise = gst::Promise::new();
        app_control
            .webrtc
            .as_ref()
            .unwrap()
            .emit("set-remote-description", &[&answer, &promise])
            .unwrap();

        app_control.app_state = AppState::PeerCallStarted;
    }

    fn handle_ice(&self, sdp_mline_index: u32, candidate: &str) {
        let app_control = self.0.lock().unwrap();
        app_control
            .webrtc
            .as_ref()
            .unwrap()
            .emit("add-ice-candidate", &[&sdp_mline_index, &candidate])
            .unwrap();
    }

    fn on_message(&self, msg: &str) -> Result<(), Error> {
        match msg {
            "HELLO" => self.handle_hello(),

            "SESSION_OK" => self.handle_session_ok(),

            x if x.starts_with("ERROR") => {
                println!("Got error message! {}", msg);
                self.handle_error()
            }

            _ => {
                let json_msg: JsonMsg = serde_json::from_str(msg)?;

                match json_msg {
                    JsonMsg::Sdp { type_, sdp } => self.handle_sdp(&type_, &sdp),
                    JsonMsg::Ice {
                        sdp_mline_index,
                        candidate,
                    } => self.handle_ice(sdp_mline_index, &candidate),
                };

                Ok(())
            }
        }
    }

    fn close_and_quit(&self, err: &Error) {
        println!("{}\nquitting", err);

        // Must not hold mutex while shutting down the pipeline
        // as something might call into here and take the mutex too
        let (pipeline, main_loop) = {
            let app_control = self.0.lock().unwrap();

            app_control
                .send_msg_tx
                .send(OwnedMessage::Close(Some(websocket::message::CloseData {
                    status_code: 1011, //Internal Error
                    reason: err.to_string(),
                })))
                .unwrap();

            (app_control.pipeline.clone(), app_control.main_loop.clone())
        };

        pipeline.set_state(gst::State::Null).into_result().unwrap();

        main_loop.quit();
    }
}

fn parse_args() -> (String, String) {
    let matches = clap::App::new("Sendrcv rust")
        .arg(
            clap::Arg::with_name("peer-id")
                .help("String ID of the peer to connect to")
                .long("peer-id")
                .required(true)
                .takes_value(true),
        )
        .arg(
            clap::Arg::with_name("server")
                .help("Signalling server to connect to")
                .long("server")
                .required(false)
                .takes_value(true),
        )
        .get_matches();

    let server = matches
        .value_of("server")
        .unwrap_or("wss://webrtc.nirbheek.in:8443");

    let peer_id = matches.value_of("peer-id").unwrap();

    (server.to_string(), peer_id.to_string())
}

fn send_loop(
    mut sender: websocket::sender::Writer<std::net::TcpStream>,
    send_msg_rx: mpsc::Receiver<OwnedMessage>,
) -> thread::JoinHandle<()> {
    thread::spawn(move || loop {
        let msg = match send_msg_rx.recv() {
            Ok(msg) => msg,
            Err(err) => {
                println!("Send loop error {:?}", err);
                return;
            }
        };

        if let OwnedMessage::Close(_) = msg {
            let _ = sender.send_message(&msg);
            return;
        }

        if let Err(err) = sender.send_message(&msg) {
            println!("Error sending {:?}", err);
        }
    })
}

fn receive_loop(
    mut receiver: websocket::receiver::Reader<std::net::TcpStream>,
    send_msg_tx: mpsc::Sender<OwnedMessage>,
    bus: gst::Bus,
) -> std::thread::JoinHandle<()> {
    thread::spawn(move || {
        for message in receiver.incoming_messages() {
            let message = match message {
                Ok(m) => m,
                Err(e) => {
                    println!("Receive Loop error: {:?}", e);
                    let mbuilder =
                        gst::Message::new_application(gst::Structure::new("ws-error", &[]));
                    let _ = bus.post(&mbuilder.build());
                    let _ = send_msg_tx.send(OwnedMessage::Close(None));
                    return;
                }
            };

            match message {
                OwnedMessage::Close(_) => {
                    let _ = send_msg_tx.send(OwnedMessage::Close(None));
                    return;
                }

                OwnedMessage::Ping(data) => {
                    if let Err(e) = send_msg_tx.send(OwnedMessage::Pong(data)) {
                        println!("Receive Loop error: {:?}", e);
                        return;
                    }
                }

                OwnedMessage::Text(msg) => {
                    let mbuilder = gst::Message::new_application(gst::Structure::new(
                        "ws-message",
                        &[("body", &msg)],
                    ));
                    let _ = bus.post(&mbuilder.build());
                }

                _ => {
                    println!("Unmatched message type: {:?}", message);
                }
            }
        }
    })
}

fn handle_application_msg(
    app_control: &AppControl,
    struc: &gst::StructureRef,
) -> Result<(), Error> {
    match struc.get_name() {
        "ws-message" => {
            let msg = struc.get_value("body").unwrap();
            app_control.on_message(msg.get().unwrap())
        }
        "ws-error" => Err(WsError(app_control.0.lock().unwrap().app_state))?,
        "error" => {
            let msg: String = struc.get_value("body").unwrap().get().unwrap();
            Err(BusError(msg))?
        }
        u => {
            println!("Got unknown application message {:?}", u);

            Ok(())
        }
    }
}

fn main() {
    gst::init().unwrap();
    if let Err(err) = check_plugins() {
        println!("{:?}", err);
        return;
    }

    let (server, peer_id) = parse_args();

    println!("Connecting to server {}", server);
    let client = match websocket::client::ClientBuilder::new(&server)
        .unwrap()
        .connect_insecure()
    {
        Ok(client) => client,
        Err(err) => {
            println!("Failed to connect to {} with error: {:?}", server, err);
            return;
        }
    };

    let main_loop = glib::MainLoop::new(None, false);
    let pipeline = gst::Pipeline::new("main");
    let bus = pipeline.get_bus().unwrap();
    let (receiver, sender) = client.split().unwrap();
    let (send_msg_tx, send_msg_rx) = mpsc::channel::<OwnedMessage>();

    let send_loop = send_loop(sender, send_msg_rx);

    let bus_clone = bus.clone();
    let send_msg_tx_clone = send_msg_tx.clone();
    let receive_loop = receive_loop(receiver, send_msg_tx_clone, bus_clone);

    let app_control = AppControl(Arc::new(Mutex::new(AppControlInner {
        webrtc: None,
        pipeline,
        send_msg_tx,
        bus: bus.clone(),
        main_loop: main_loop.clone(),
        peer_id: peer_id.to_string(),
        app_state: AppState::ServerConnected,
    })));
    app_control.register_with_server();

    bus.add_watch(move |_, msg| {
        use gst::message::MessageView;

        let app_control = app_control.clone();
        match msg.view() {
            MessageView::Error(err) => app_control.close_and_quit(&Error::from(err.get_error())),
            MessageView::Warning(warning) => {
                println!("Warning: \"{}\"", warning.get_debug().unwrap());
            }
            MessageView::Application(a) => {
                let struc = a.get_structure().unwrap();
                if let Err(err) = handle_application_msg(&app_control, struc) {
                    app_control.close_and_quit(&err)
                }
            }
            _ => {}
        };

        glib::Continue(true)
    });

    main_loop.run();
    let _ = send_loop.join();
    let _ = receive_loop.join();
}
