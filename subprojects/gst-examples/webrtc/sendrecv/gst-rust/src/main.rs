mod macos_workaround;

use std::sync::{Arc, Mutex, Weak};

use rand::prelude::*;

use clap::Parser;

use async_std::prelude::*;
use async_std::task;
use futures::channel::mpsc;
use futures::sink::{Sink, SinkExt};
use futures::stream::StreamExt;

use async_tungstenite::tungstenite;
use tungstenite::Error as WsError;
use tungstenite::Message as WsMessage;

use gst::glib;
use gst::prelude::*;
use gst_rtp::prelude::*;

use serde_derive::{Deserialize, Serialize};

use anyhow::{anyhow, bail, Context};

const STUN_SERVER: &str = "stun://stun.l.google.com:19302";
const TURN_SERVER: &str = "turn://foo:bar@webrtc.gstreamer.net:3478";

const TWCC_URI: &str = "http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01";

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

#[derive(Debug, clap::Parser)]
struct Args {
    #[clap(short, long, default_value = "wss://webrtc.gstreamer.net:8443")]
    server: String,
    /// Peer ID that should be called. If not given then an incoming call is expected.
    #[clap(short, long)]
    peer_id: Option<u32>,
    /// Our ID. If not given then a random ID is created.
    #[clap(short, long)]
    our_id: Option<u32>,
    /// Request that the peer creates an offer.
    #[clap(short, long, default_value = "false")]
    remote_offerer: bool,
}

// JSON messages we communicate with
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

// Strong reference to our application state
#[derive(Debug, Clone)]
struct App(Arc<AppInner>);

// Weak reference to our application state
#[derive(Debug, Clone)]
struct AppWeak(Weak<AppInner>);

// Actual application state
#[derive(Debug)]
struct AppInner {
    args: Args,
    pipeline: gst::Pipeline,
    webrtcbin: gst::Element,
    send_msg_tx: Mutex<mpsc::UnboundedSender<WsMessage>>,
}

// To be able to access the App's fields directly
impl std::ops::Deref for App {
    type Target = AppInner;

    fn deref(&self) -> &AppInner {
        &self.0
    }
}

impl AppWeak {
    // Try upgrading a weak reference to a strong one
    fn upgrade(&self) -> Option<App> {
        self.0.upgrade().map(App)
    }
}

impl App {
    // Downgrade the strong reference to a weak reference
    fn downgrade(&self) -> AppWeak {
        AppWeak(Arc::downgrade(&self.0))
    }

    fn new(
        args: Args,
    ) -> Result<
        (
            Self,
            impl Stream<Item = gst::Message>,
            impl Stream<Item = WsMessage>,
        ),
        anyhow::Error,
    > {
        // Create the GStreamer pipeline
        let pipeline = gst::parse_launch(
        "videotestsrc pattern=ball is-live=true ! vp8enc deadline=1 keyframe-max-dist=2000 ! rtpvp8pay name=vpay pt=96 picture-id-mode=15-bit ! webrtcbin. \
         audiotestsrc is-live=true ! opusenc perfect-timestamp=true ! rtpopuspay name=apay pt=97 ! application/x-rtp,encoding-name=OPUS ! webrtcbin. \
         webrtcbin name=webrtcbin"
    )?;

        // Downcast from gst::Element to gst::Pipeline
        let pipeline = pipeline
            .downcast::<gst::Pipeline>()
            .expect("not a pipeline");

        // Get access to the webrtcbin by name
        let webrtcbin = pipeline.by_name("webrtcbin").expect("can't find webrtcbin");

        // Set some properties on webrtcbin
        webrtcbin.set_property_from_str("stun-server", STUN_SERVER);
        webrtcbin.set_property_from_str("turn-server", TURN_SERVER);
        webrtcbin.set_property_from_str("bundle-policy", "max-bundle");

        // Create a stream for handling the GStreamer message asynchronously
        let bus = pipeline.bus().unwrap();
        let send_gst_msg_rx = bus.stream();

        // Channel for outgoing WebSocket messages from other threads
        let (send_ws_msg_tx, send_ws_msg_rx) = mpsc::unbounded::<WsMessage>();

        let app = App(Arc::new(AppInner {
            args,
            pipeline,
            webrtcbin,
            send_msg_tx: Mutex::new(send_ws_msg_tx),
        }));

        // Connect to on-negotiation-needed to handle sending an Offer
        if app.args.peer_id.is_some() && !app.args.remote_offerer
            || app.args.peer_id.is_none() && app.args.remote_offerer
        {
            let vpay = app.pipeline.by_name("vpay").unwrap();
            let apay = app.pipeline.by_name("apay").unwrap();

            for pay in [vpay, apay] {
                let twcc = gst_rtp::RTPHeaderExtension::create_from_uri(TWCC_URI).unwrap();
                twcc.set_id(1);
                pay.emit_by_name::<()>("add-extension", &[&twcc]);
            }

            let app_clone = app.downgrade();
            app.webrtcbin.connect_closure(
                "on-negotiation-needed",
                false,
                glib::closure!(move |_webrtcbin: &gst::Element| {
                    let app = upgrade_weak!(app_clone);
                    if let Err(err) = app.on_negotiation_needed() {
                        gst::element_error!(
                            app.pipeline,
                            gst::LibraryError::Failed,
                            ("Failed to negotiate: {:?}", err)
                        );
                    }
                }),
            );
        }

        // Whenever there is a new ICE candidate, send it to the peer
        let app_clone = app.downgrade();
        app.webrtcbin.connect_closure(
            "on-ice-candidate",
            false,
            glib::closure!(
                move |_webrtcbin: &gst::Element, mlineindex: u32, candidate: &str| {
                    let app = upgrade_weak!(app_clone);

                    if let Err(err) = app.on_ice_candidate(mlineindex, candidate) {
                        gst::element_error!(
                            app.pipeline,
                            gst::LibraryError::Failed,
                            ("Failed to send ICE candidate: {:?}", err)
                        );
                    }
                }
            ),
        );

        // Whenever there is a new stream incoming from the peer, handle it
        let app_clone = app.downgrade();
        app.webrtcbin.connect_pad_added(move |_webrtc, pad| {
            let app = upgrade_weak!(app_clone);

            if let Err(err) = app.on_incoming_stream(pad) {
                gst::element_error!(
                    app.pipeline,
                    gst::LibraryError::Failed,
                    ("Failed to handle incoming stream: {:?}", err)
                );
            }
        });

        // Asynchronously set the pipeline to Playing if we're creating the offer,
        // otherwise do that after the offer was received.
        if app.args.peer_id.is_some() && !app.args.remote_offerer {
            app.pipeline.call_async(|pipeline| {
                // If this fails, post an error on the bus so we exit
                if pipeline.set_state(gst::State::Playing).is_err() {
                    gst::element_error!(
                        pipeline,
                        gst::LibraryError::Failed,
                        ("Failed to set pipeline to Playing")
                    );
                }
            });
        }

        Ok((app, send_gst_msg_rx, send_ws_msg_rx))
    }

    // Handle WebSocket messages, both our own as well as WebSocket protocol messages
    fn handle_websocket_message(&self, msg: &str) -> Result<(), anyhow::Error> {
        if msg.starts_with("ERROR") {
            bail!("Got error message: {msg}");
        }

        if msg == "OFFER_REQUEST" {
            self.pipeline.call_async(|pipeline| {
                // If this fails, post an error on the bus so we exit
                if pipeline.set_state(gst::State::Playing).is_err() {
                    gst::element_error!(
                        pipeline,
                        gst::LibraryError::Failed,
                        ("Failed to set pipeline to Playing")
                    );
                }
            });

            return Ok(());
        }

        let json_msg: JsonMsg = serde_json::from_str(msg)?;

        match json_msg {
            JsonMsg::Sdp { type_, sdp } => self.handle_sdp(&type_, &sdp),
            JsonMsg::Ice {
                sdp_mline_index,
                candidate,
            } => self.handle_ice(sdp_mline_index, &candidate),
        }
    }

    // Handle GStreamer messages coming from the pipeline
    fn handle_pipeline_message(&self, message: &gst::Message) -> Result<(), anyhow::Error> {
        use gst::message::MessageView;

        match message.view() {
            MessageView::Error(err) => bail!(
                "Error from element {}: {} ({})",
                err.src()
                    .map(|s| String::from(s.path_string()))
                    .unwrap_or_else(|| String::from("None")),
                err.error(),
                err.debug().unwrap_or_else(|| glib::GString::from("None")),
            ),
            MessageView::Warning(warning) => {
                println!("Warning: \"{}\"", warning.debug().unwrap());
            }
            MessageView::Latency(_) => {
                let _ = self.pipeline.recalculate_latency();
            }
            _ => (),
        }

        Ok(())
    }

    // Whenever webrtcbin tells us that (re-)negotiation is needed, simply ask
    // for a new offer SDP from webrtcbin without any customization and then
    // asynchronously send it to the peer via the WebSocket connection
    fn on_negotiation_needed(&self) -> Result<(), anyhow::Error> {
        println!("starting negotiation");

        let app_clone = self.downgrade();
        let promise = gst::Promise::with_change_func(move |reply| {
            let app = upgrade_weak!(app_clone);

            if let Err(err) = app.on_offer_created(reply) {
                gst::element_error!(
                    app.pipeline,
                    gst::LibraryError::Failed,
                    ("Failed to send SDP offer: {:?}", err)
                );
            }
        });

        self.webrtcbin
            .emit_by_name::<()>("create-offer", &[&None::<gst::Structure>, &promise]);

        Ok(())
    }

    // Once webrtcbin has create the offer SDP for us, handle it by sending it to the peer via the
    // WebSocket connection
    fn on_offer_created(
        &self,
        reply: Result<Option<&gst::StructureRef>, gst::PromiseError>,
    ) -> Result<(), anyhow::Error> {
        let reply = match reply {
            Ok(Some(reply)) => reply,
            Ok(None) => {
                bail!("Offer creation future got no response");
            }
            Err(err) => {
                bail!("Offer creation future got error response: {err:?}");
            }
        };

        let offer = reply
            .value("offer")
            .unwrap()
            .get::<gst_webrtc::WebRTCSessionDescription>()
            .expect("Invalid argument");
        self.webrtcbin
            .emit_by_name::<()>("set-local-description", &[&offer, &None::<gst::Promise>]);

        println!(
            "sending SDP offer to peer: {}",
            offer.sdp().as_text().unwrap()
        );

        let message = serde_json::to_string(&JsonMsg::Sdp {
            type_: "offer".to_string(),
            sdp: offer.sdp().as_text().unwrap(),
        })
        .unwrap();

        self.send_msg_tx
            .lock()
            .unwrap()
            .unbounded_send(WsMessage::Text(message))
            .context("Failed to send SDP offer")?;

        Ok(())
    }

    // Once webrtcbin has create the answer SDP for us, handle it by sending it to the peer via the
    // WebSocket connection
    fn on_answer_created(
        &self,
        reply: Result<Option<&gst::StructureRef>, gst::PromiseError>,
    ) -> Result<(), anyhow::Error> {
        let reply = match reply {
            Ok(Some(reply)) => reply,
            Ok(None) => {
                bail!("Answer creation future got no response");
            }
            Err(err) => {
                bail!("Answer creation future got error response: {err:?}");
            }
        };

        let answer = reply
            .value("answer")
            .unwrap()
            .get::<gst_webrtc::WebRTCSessionDescription>()
            .expect("Invalid argument");
        self.webrtcbin
            .emit_by_name::<()>("set-local-description", &[&answer, &None::<gst::Promise>]);

        println!(
            "sending SDP answer to peer: {}",
            answer.sdp().as_text().unwrap()
        );

        let message = serde_json::to_string(&JsonMsg::Sdp {
            type_: "answer".to_string(),
            sdp: answer.sdp().as_text().unwrap(),
        })
        .unwrap();

        self.send_msg_tx
            .lock()
            .unwrap()
            .unbounded_send(WsMessage::Text(message))
            .context("Failed to send SDP answer")?;

        Ok(())
    }

    fn configure_pipeline_on_offer(
        &self,
        offer: &gst_sdp::SDPMessage,
    ) -> Result<(), anyhow::Error> {
        // Extract audio/video payload types from the SDP and configure accordingly on the
        // pipeline as these have to match with the offer
        let mut opus_id = None;
        let mut vp8_id = None;
        for media in offer.medias() {
            for fmt in media.formats() {
                if fmt == "webrtc-datachannel" {
                    continue;
                }

                let pt = match fmt.parse::<u8>() {
                    Ok(pt) => pt,
                    Err(_) => continue,
                };

                let caps = match media.caps_from_media(pt as i32) {
                    Some(caps) if caps.size() > 0 => caps,
                    _ => continue,
                };

                let s = caps.structure(0).unwrap();
                let encoding_name = match s.get::<&str>("encoding-name") {
                    Ok(encoding_name) => encoding_name,
                    Err(_) => continue,
                };

                let twcc_id = media.attributes().find_map(|attr| {
                    let key = attr.key();
                    let value = attr.value();
                    if key != "extmap" || !value.map_or(false, |value| value.ends_with(TWCC_URI)) {
                        return None;
                    }
                    let value = value.unwrap();

                    let id = value
                        .strip_suffix(TWCC_URI)
                        .and_then(|id| id.trim().parse::<u8>().ok());

                    id
                });

                if encoding_name == "VP8" && vp8_id.is_none() {
                    vp8_id = Some((pt, twcc_id));
                } else if encoding_name == "OPUS" && opus_id.is_none() {
                    opus_id = Some((pt, twcc_id));
                }
            }
        }

        if let (Some(opus_id), Some(vp8_id)) = (opus_id, vp8_id) {
            let apay = self.pipeline.by_name("apay").unwrap();
            let vpay = self.pipeline.by_name("vpay").unwrap();

            for (pay, (pt, twcc_id)) in [(apay, opus_id), (vpay, vp8_id)] {
                pay.set_property("pt", pt as u32);

                if let Some(twcc_id) = twcc_id {
                    let twcc = gst_rtp::RTPHeaderExtension::create_from_uri(TWCC_URI).unwrap();
                    twcc.set_id(twcc_id as u32);
                    pay.emit_by_name::<()>("add-extension", &[&twcc]);
                }
            }
        } else {
            gst::element_error!(
                self.pipeline,
                gst::LibraryError::Failed,
                ("Not all streams found in the offer")
            );
            bail!("Not all streams found in the offer");
        }

        Ok(())
    }

    // Handle incoming SDP answers from the peer
    fn handle_sdp(&self, type_: &str, sdp: &str) -> Result<(), anyhow::Error> {
        if type_ == "answer" {
            print!("Received answer:\n{sdp}\n");

            let ret = gst_sdp::SDPMessage::parse_buffer(sdp.as_bytes())
                .map_err(|_| anyhow!("Failed to parse SDP answer"))?;
            let answer =
                gst_webrtc::WebRTCSessionDescription::new(gst_webrtc::WebRTCSDPType::Answer, ret);

            self.webrtcbin
                .emit_by_name::<()>("set-remote-description", &[&answer, &None::<gst::Promise>]);

            Ok(())
        } else if type_ == "offer" {
            print!("Received offer:\n{sdp}\n");

            let ret = gst_sdp::SDPMessage::parse_buffer(sdp.as_bytes())
                .map_err(|_| anyhow!("Failed to parse SDP offer"))?;

            // And then asynchronously start our pipeline and do the next steps. The
            // pipeline needs to be started before we can create an answer
            let app_clone = self.downgrade();
            self.pipeline.call_async(move |_pipeline| {
                let app = upgrade_weak!(app_clone);

                if app.configure_pipeline_on_offer(&ret).is_err() {
                    return;
                }

                // If this fails, post an error on the bus so we exit
                if app.pipeline.set_state(gst::State::Playing).is_err() {
                    gst::element_error!(
                        app.pipeline,
                        gst::LibraryError::Failed,
                        ("Failed to set pipeline to Playing")
                    );
                    return;
                }

                let offer = gst_webrtc::WebRTCSessionDescription::new(
                    gst_webrtc::WebRTCSDPType::Offer,
                    ret,
                );

                app.0
                    .webrtcbin
                    .emit_by_name::<()>("set-remote-description", &[&offer, &None::<gst::Promise>]);

                let app_clone = app.downgrade();
                let promise = gst::Promise::with_change_func(move |reply| {
                    let app = upgrade_weak!(app_clone);

                    if let Err(err) = app.on_answer_created(reply) {
                        gst::element_error!(
                            app.pipeline,
                            gst::LibraryError::Failed,
                            ("Failed to send SDP answer: {:?}", err)
                        );
                    }
                });

                app.0
                    .webrtcbin
                    .emit_by_name::<()>("create-answer", &[&None::<gst::Structure>, &promise]);
            });

            Ok(())
        } else {
            bail!("Sdp type is not \"answer\" but \"{type_}\"")
        }
    }

    // Handle incoming ICE candidates from the peer by passing them to webrtcbin
    fn handle_ice(&self, sdp_mline_index: u32, candidate: &str) -> Result<(), anyhow::Error> {
        self.webrtcbin
            .emit_by_name::<()>("add-ice-candidate", &[&sdp_mline_index, &candidate]);

        Ok(())
    }

    // Asynchronously send ICE candidates to the peer via the WebSocket connection as a JSON
    // message
    fn on_ice_candidate(&self, mlineindex: u32, candidate: &str) -> Result<(), anyhow::Error> {
        let message = serde_json::to_string(&JsonMsg::Ice {
            candidate: candidate.to_string(),
            sdp_mline_index: mlineindex,
        })
        .unwrap();

        self.send_msg_tx
            .lock()
            .unwrap()
            .unbounded_send(WsMessage::Text(message))
            .context("Failed to send ICE candidate")?;

        Ok(())
    }

    // Whenever there's a new incoming, encoded stream from the peer create a new decodebin
    fn on_incoming_stream(&self, pad: &gst::Pad) -> Result<(), anyhow::Error> {
        // Early return for the source pads we're adding ourselves
        if pad.direction() != gst::PadDirection::Src {
            return Ok(());
        }

        let decodebin = gst::ElementFactory::make("decodebin").build().unwrap();
        let app_clone = self.downgrade();
        decodebin.connect_pad_added(move |_decodebin, pad| {
            let app = upgrade_weak!(app_clone);

            if let Err(err) = app.on_incoming_decodebin_stream(pad) {
                gst::element_error!(
                    app.pipeline,
                    gst::LibraryError::Failed,
                    ("Failed to handle decoded stream: {:?}", err)
                );
            }
        });

        self.pipeline.add(&decodebin).unwrap();
        decodebin.sync_state_with_parent().unwrap();

        let sinkpad = decodebin.static_pad("sink").unwrap();
        pad.link(&sinkpad).unwrap();

        Ok(())
    }

    // Handle a newly decoded decodebin stream and depending on its type, create the relevant
    // elements or simply ignore it
    fn on_incoming_decodebin_stream(&self, pad: &gst::Pad) -> Result<(), anyhow::Error> {
        let caps = pad.current_caps().unwrap();
        let name = caps.structure(0).unwrap().name();

        let sink = if name.starts_with("video/") {
            gst::parse_bin_from_description(
                "queue ! videoconvert ! videoscale ! autovideosink",
                true,
            )?
        } else if name.starts_with("audio/") {
            gst::parse_bin_from_description(
                "queue ! audioconvert ! audioresample ! autoaudiosink",
                true,
            )?
        } else {
            println!("Unknown pad {pad:?}, ignoring");
            return Ok(());
        };

        self.pipeline.add(&sink).unwrap();
        sink.sync_state_with_parent()
            .with_context(|| format!("can't start sink for stream {caps:?}"))?;

        let sinkpad = sink.static_pad("sink").unwrap();
        pad.link(&sinkpad)
            .with_context(|| format!("can't link sink for stream {caps:?}"))?;

        Ok(())
    }
}

// Make sure to shut down the pipeline when it goes out of scope
// to release any system resources
impl Drop for AppInner {
    fn drop(&mut self) {
        let _ = self.pipeline.set_state(gst::State::Null);
    }
}

async fn run(
    args: Args,
    ws: impl Sink<WsMessage, Error = WsError> + Stream<Item = Result<WsMessage, WsError>>,
) -> Result<(), anyhow::Error> {
    // Split the websocket into the Sink and Stream
    let (mut ws_sink, ws_stream) = ws.split();
    // Fuse the Stream, required for the select macro
    let mut ws_stream = ws_stream.fuse();

    // Create our application state
    let (app, send_gst_msg_rx, send_ws_msg_rx) = App::new(args)?;

    let mut send_gst_msg_rx = send_gst_msg_rx.fuse();
    let mut send_ws_msg_rx = send_ws_msg_rx.fuse();

    // And now let's start our message loop
    loop {
        let ws_msg = futures::select! {
            // Handle the WebSocket messages here
            ws_msg = ws_stream.select_next_some() => {
                match ws_msg? {
                    WsMessage::Close(_) => {
                        println!("peer disconnected");
                        break
                    },
                    WsMessage::Ping(data) => Some(WsMessage::Pong(data)),
                    WsMessage::Pong(_) => None,
                    WsMessage::Binary(_) => None,
                    WsMessage::Text(text) => {
                        app.handle_websocket_message(&text)?;
                        None
                    },
                    WsMessage::Frame(_) => unreachable!(),
                }
            },
            // Pass the GStreamer messages to the application control logic
            gst_msg = send_gst_msg_rx.select_next_some() => {
                app.handle_pipeline_message(&gst_msg)?;
                None
            },
            // Handle WebSocket messages we created asynchronously
            // to send them out now
            ws_msg = send_ws_msg_rx.select_next_some() => Some(ws_msg),
            // Once we're done, break the loop and return
            complete => break,
        };

        // If there's a message to send out, do so now
        if let Some(ws_msg) = ws_msg {
            ws_sink.send(ws_msg).await?;
        }
    }

    Ok(())
}

// Check if all GStreamer plugins we require are available
fn check_plugins() -> Result<(), anyhow::Error> {
    let needed = [
        "videotestsrc",
        "audiotestsrc",
        "videoconvertscale",
        "audioconvert",
        "autodetect",
        "opus",
        "vpx",
        "webrtc",
        "nice",
        "dtls",
        "srtp",
        "rtpmanager",
        "rtp",
        "playback",
        "audioresample",
    ];

    let registry = gst::Registry::get();
    let missing = needed
        .iter()
        .filter(|n| registry.find_plugin(n).is_none())
        .cloned()
        .collect::<Vec<_>>();

    if !missing.is_empty() {
        bail!("Missing plugins: {missing:?}");
    } else {
        Ok(())
    }
}

async fn async_main() -> Result<(), anyhow::Error> {
    // Initialize GStreamer first
    gst::init()?;

    check_plugins()?;

    let args = Args::parse();

    // Connect to the given server
    let (mut ws, _) = async_tungstenite::async_std::connect_async(&args.server).await?;

    println!("connected");

    // Say HELLO to the server and see if it replies with HELLO
    let our_id = args
        .our_id
        .unwrap_or_else(|| rand::thread_rng().gen_range(10..10_000));
    println!("Registering id {our_id} with server");
    ws.send(WsMessage::Text(format!("HELLO {our_id}"))).await?;

    let msg = ws
        .next()
        .await
        .ok_or_else(|| anyhow!("didn't receive anything"))??;

    if msg != WsMessage::Text("HELLO".into()) {
        bail!("server didn't say HELLO");
    }

    if let Some(peer_id) = args.peer_id {
        println!("Setting up call with peer id {peer_id}");

        // Join the given session
        ws.send(WsMessage::Text(format!("SESSION {peer_id}")))
            .await?;

        let msg = ws
            .next()
            .await
            .ok_or_else(|| anyhow!("didn't receive anything"))??;

        if msg != WsMessage::Text("SESSION_OK".into()) {
            bail!("server error: {msg:?}");
        }

        // If we expect the peer to create the offer request it now
        if args.remote_offerer {
            println!("Requesting offer from peer");
            ws.send(WsMessage::Text("OFFER_REQUEST".into())).await?;
        }
    } else {
        println!("Waiting for incoming call");
    }

    // All good, let's run our message loop
    run(args, ws).await
}

fn main() -> Result<(), anyhow::Error> {
    macos_workaround::run(|| task::block_on(async_main()))
}
