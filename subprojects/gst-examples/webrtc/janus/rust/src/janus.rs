// GStreamer
//
// Copyright (C) 2018 maxmcd <max.t.mcdonnell@gmail.com>
// Copyright (C) 2019 Sebastian Dröge <sebastian@centricular.com>
// Copyright (C) 2020 Philippe Normand <philn@igalia.com>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Library General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Library General Public License for more details.
//
// You should have received a copy of the GNU Library General Public
// License along with this library; if not, write to the
// Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
// Boston, MA 02110-1301, USA.

use {
    anyhow::{anyhow, bail, Context},
    async_tungstenite::{gio::connect_async, tungstenite},
    clap::Parser,
    futures::channel::mpsc,
    futures::sink::{Sink, SinkExt},
    futures::stream::{Stream, StreamExt},
    gst::prelude::*,
    http::Uri,
    log::{debug, error, info, trace},
    rand::prelude::*,
    serde_derive::{Deserialize, Serialize},
    serde_json::json,
    std::sync::{Arc, Mutex, Weak},
    std::time::Duration,
    tungstenite::Message as WsMessage,
};

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

#[derive(Debug, Clone)]
struct VideoParameter {
    encoder: &'static str,
    encoding_name: &'static str,
    payloader: &'static str,
}

const VP8: VideoParameter = VideoParameter {
    encoder: "vp8enc target-bitrate=100000 overshoot=25 undershoot=100 deadline=33000 keyframe-max-dist=1",
    encoding_name: "VP8",
    payloader: "rtpvp8pay picture-id-mode=2"
};

const H264: VideoParameter = VideoParameter {
    encoder: "x264enc tune=zerolatency",
    encoding_name: "H264",
    payloader: "rtph264pay aggregate-mode=zero-latency",
};

impl std::str::FromStr for VideoParameter {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "vp8" => Ok(VP8),
            "h264" => Ok(H264),
            _ => Err(anyhow!(
                "Invalid video parameter: {}. Use either vp8 or h264",
                s
            )),
        }
    }
}

#[derive(Debug, clap::Parser)]
pub struct Args {
    #[clap(short, long, default_value = "wss://janus.conf.meetecho.com/ws:8989")]
    server: String,
    #[clap(short, long, default_value = "1234")]
    room_id: u32,
    #[clap(short, long, default_value = "1234")]
    feed_id: u32,
    #[clap(short, long, default_value = "vp8")]
    webrtc_video_codec: VideoParameter,
}

#[derive(Serialize, Deserialize, Debug)]
struct Base {
    janus: String,
    transaction: Option<String>,
    session_id: Option<i64>,
    sender: Option<i64>,
}

#[derive(Serialize, Deserialize, Debug)]
struct DataHolder {
    id: i64,
}

#[derive(Serialize, Deserialize, Debug)]
struct PluginDataHolder {
    videoroom: String,
    room: i64,
    description: Option<String>,
    id: Option<i64>,
    configured: Option<String>,
    video_codec: Option<String>,
    unpublished: Option<i64>,
}

#[derive(Serialize, Deserialize, Debug)]
struct PluginHolder {
    plugin: String,
    data: PluginDataHolder,
}

#[derive(Serialize, Deserialize, Debug)]
struct IceHolder {
    candidate: String,
    #[serde(rename = "sdpMLineIndex")]
    sdp_mline_index: u32,
}

#[derive(Serialize, Deserialize, Debug)]
struct JsepHolder {
    #[serde(rename = "type")]
    type_: String,
    sdp: Option<String>,
    ice: Option<IceHolder>,
}

#[derive(Serialize, Deserialize, Debug)]
struct JsonReply {
    #[serde(flatten)]
    base: Base,
    data: Option<DataHolder>,
    #[serde(rename = "plugindata")]
    plugin_data: Option<PluginHolder>,
    jsep: Option<JsepHolder>,
}

fn transaction_id() -> String {
    thread_rng()
        .sample_iter(&rand::distributions::Alphanumeric)
        .map(char::from)
        .take(30)
        .collect()
}

// Strong reference to the state of one peer
#[derive(Debug, Clone)]
struct Peer(Arc<PeerInner>);

// Weak reference to the state of one peer
#[derive(Debug, Clone)]
struct PeerWeak(Weak<PeerInner>);

impl PeerWeak {
    // Try upgrading a weak reference to a strong one
    fn upgrade(&self) -> Option<Peer> {
        self.0.upgrade().map(Peer)
    }
}

// To be able to access the Peers's fields directly
impl std::ops::Deref for Peer {
    type Target = PeerInner;

    fn deref(&self) -> &PeerInner {
        &self.0
    }
}

#[derive(Clone, Copy, Debug)]
struct ConnectionHandle {
    id: i64,
    session_id: i64,
}

// Actual peer state
#[derive(Debug)]
struct PeerInner {
    handle: ConnectionHandle,
    bin: gst::Bin,
    webrtcbin: gst::Element,
    send_msg_tx: Arc<Mutex<mpsc::UnboundedSender<WsMessage>>>,
}

impl Peer {
    // Downgrade the strong reference to a weak reference
    fn downgrade(&self) -> PeerWeak {
        PeerWeak(Arc::downgrade(&self.0))
    }

    // Whenever webrtcbin tells us that (re-)negotiation is needed, simply ask
    // for a new offer SDP from webrtcbin without any customization and then
    // asynchronously send it to the peer via the WebSocket connection
    fn on_negotiation_needed(&self) -> Result<(), anyhow::Error> {
        info!("starting negotiation with peer");

        let peer_clone = self.downgrade();
        let promise = gst::Promise::with_change_func(move |res| {
            let s = res.ok().flatten().expect("no answer");
            let peer = upgrade_weak!(peer_clone);

            if let Err(err) = peer.on_offer_created(s) {
                gst::element_error!(
                    peer.bin,
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
    fn on_offer_created(&self, reply: &gst::StructureRef) -> Result<(), anyhow::Error> {
        let offer = reply
            .get::<gst_webrtc::WebRTCSessionDescription>("offer")
            .expect("Invalid argument");
        self.webrtcbin
            .emit_by_name::<()>("set-local-description", &[&offer, &None::<gst::Promise>]);

        info!("sending SDP offer to peer: {:?}", offer.sdp().as_text());

        let transaction = transaction_id();
        let sdp_data = offer.sdp().as_text()?;
        let msg = WsMessage::Text(
            json!({
                 "janus": "message",
                 "transaction": transaction,
                 "session_id": self.handle.session_id,
                 "handle_id": self.handle.id,
                 "body": {
                     "request": "publish",
                     "audio": true,
                     "video": true,
                 },
                 "jsep": {
                     "sdp": sdp_data,
                     "trickle": true,
                     "type": "offer"
                 }
            })
            .to_string(),
        );
        self.send_msg_tx
            .lock()
            .expect("Invalid message sender")
            .unbounded_send(msg)
            .context("Failed to send SDP offer")?;

        Ok(())
    }

    // Once webrtcbin has create the answer SDP for us, handle it by sending it to the peer via the
    // WebSocket connection
    fn on_answer_created(&self, reply: &gst::Structure) -> Result<(), anyhow::Error> {
        let answer = reply
            .get::<gst_webrtc::WebRTCSessionDescription>("answer")
            .expect("Invalid answer");
        self.webrtcbin
            .emit_by_name::<()>("set-local-description", &[&answer, &None::<gst::Promise>]);

        info!("sending SDP answer to peer: {:?}", answer.sdp().as_text());

        Ok(())
    }

    // Handle incoming SDP answers from the peer
    fn handle_sdp(&self, type_: &str, sdp: &str) -> Result<(), anyhow::Error> {
        if type_ == "answer" {
            info!("Received answer:\n{}\n", sdp);

            let ret = gst_sdp::SDPMessage::parse_buffer(sdp.as_bytes())
                .map_err(|_| anyhow!("Failed to parse SDP answer"))?;
            let answer =
                gst_webrtc::WebRTCSessionDescription::new(gst_webrtc::WebRTCSDPType::Answer, ret);

            self.webrtcbin
                .emit_by_name::<()>("set-remote-description", &[&answer, &None::<gst::Promise>]);

            Ok(())
        } else if type_ == "offer" {
            info!("Received offer:\n{}\n", sdp);

            let ret = gst_sdp::SDPMessage::parse_buffer(sdp.as_bytes())
                .map_err(|_| anyhow!("Failed to parse SDP offer"))?;

            // And then asynchronously start our pipeline and do the next steps. The
            // pipeline needs to be started before we can create an answer
            let peer_clone = self.downgrade();
            self.bin.call_async(move |_pipeline| {
                let peer = upgrade_weak!(peer_clone);

                let offer = gst_webrtc::WebRTCSessionDescription::new(
                    gst_webrtc::WebRTCSDPType::Offer,
                    ret,
                );

                peer.0
                    .webrtcbin
                    .emit_by_name::<()>("set-remote-description", &[&offer, &None::<gst::Promise>]);

                let peer_clone = peer.downgrade();
                let promise = gst::Promise::with_change_func(move |reply| {
                    let s = reply.ok().flatten().expect("No answer");
                    let peer = upgrade_weak!(peer_clone);

                    if let Err(err) = peer.on_answer_created(&s.to_owned()) {
                        gst::element_error!(
                            peer.bin,
                            gst::LibraryError::Failed,
                            ("Failed to send SDP answer: {:?}", err)
                        );
                    }
                });

                peer.0
                    .webrtcbin
                    .emit_by_name::<()>("create-answer", &[&None::<gst::Structure>, &promise]);
            });

            Ok(())
        } else {
            bail!("Sdp type is not \"answer\" but \"{}\"", type_)
        }
    }

    // Handle incoming ICE candidates from the peer by passing them to webrtcbin
    fn handle_ice(&self, sdp_mline_index: u32, candidate: &str) -> Result<(), anyhow::Error> {
        info!(
            "Received remote ice-candidate {} {}",
            sdp_mline_index, candidate
        );
        self.webrtcbin
            .emit_by_name::<()>("add-ice-candidate", &[&sdp_mline_index, &candidate]);

        Ok(())
    }

    // Asynchronously send ICE candidates to the peer via the WebSocket connection as a JSON
    // message
    fn on_ice_candidate(&self, mlineindex: u32, candidate: &str) -> Result<(), anyhow::Error> {
        let transaction = transaction_id();
        info!("Sending ICE {} {}", mlineindex, &candidate);
        let msg = WsMessage::Text(
            json!({
                "janus": "trickle",
                "transaction": transaction,
                "session_id": self.handle.session_id,
                "handle_id": self.handle.id,
                "candidate": {
                    "candidate": candidate,
                    "sdpMLineIndex": mlineindex
                },
            })
            .to_string(),
        );
        self.send_msg_tx
            .lock()
            .expect("Invalid message sender")
            .unbounded_send(msg)
            .context("Failed to send ICE candidate")?;

        Ok(())
    }
}

// At least shut down the bin here if it didn't happen so far
impl Drop for PeerInner {
    fn drop(&mut self) {
        let _ = self.bin.set_state(gst::State::Null);
    }
}

type WsStream =
    std::pin::Pin<Box<dyn Stream<Item = Result<WsMessage, tungstenite::error::Error>> + Send>>;
type WsSink = std::pin::Pin<Box<dyn Sink<WsMessage, Error = tungstenite::error::Error> + Send>>;

pub struct JanusGateway {
    ws_stream: Option<WsStream>,
    ws_sink: Option<WsSink>,
    handle: ConnectionHandle,
    peer: Mutex<Peer>,
    send_ws_msg_rx: Option<mpsc::UnboundedReceiver<WsMessage>>,
}

impl JanusGateway {
    pub async fn new(pipeline: gst::Bin) -> Result<Self, anyhow::Error> {
        use tungstenite::client::IntoClientRequest;

        let args = Args::parse();

        let mut request = args.server.parse::<Uri>()?.into_client_request()?;
        request.headers_mut().append(
            "Sec-WebSocket-Protocol",
            http::HeaderValue::from_static("janus-protocol"),
        );

        let (mut ws, _) = connect_async(request).await?;

        let transaction = transaction_id();
        let msg = WsMessage::Text(
            json!({
                "janus": "create",
                "transaction": transaction,
            })
            .to_string(),
        );
        ws.send(msg).await?;

        let msg = ws
            .next()
            .await
            .ok_or_else(|| anyhow!("didn't receive anything"))??;
        let payload = msg.to_text()?;
        let json_msg: JsonReply = serde_json::from_str(payload)?;
        assert_eq!(json_msg.base.janus, "success");
        assert_eq!(json_msg.base.transaction, Some(transaction));
        let session_id = json_msg.data.expect("no session id").id;

        let transaction = transaction_id();
        let msg = WsMessage::Text(
            json!({
                "janus": "attach",
                "transaction": transaction,
                "plugin": "janus.plugin.videoroom",
                "session_id": session_id,
            })
            .to_string(),
        );
        ws.send(msg).await?;

        let msg = ws
            .next()
            .await
            .ok_or_else(|| anyhow!("didn't receive anything"))??;
        let payload = msg.to_text()?;
        let json_msg: JsonReply = serde_json::from_str(payload)?;
        assert_eq!(json_msg.base.janus, "success");
        assert_eq!(json_msg.base.transaction, Some(transaction));
        let handle = json_msg.data.expect("no session id").id;

        let transaction = transaction_id();
        let msg = WsMessage::Text(
            json!({
                "janus": "message",
                "transaction": transaction,
                "session_id": session_id,
                "handle_id": handle,
                "body": {
                    "request": "join",
                    "ptype": "publisher",
                    "room": args.room_id,
                    "id": args.feed_id,
                },
            })
            .to_string(),
        );
        ws.send(msg).await?;

        let webrtcbin = pipeline.by_name("webrtcbin").expect("can't find webrtcbin");

        let webrtc_codec = &args.webrtc_video_codec;
        let bin_description = &format!(
            "{encoder} name=encoder ! {payloader} ! queue ! capsfilter name=webrtc-vsink caps=\"application/x-rtp,media=video,encoding-name={encoding_name},payload=96\"",
            encoder=webrtc_codec.encoder, payloader=webrtc_codec.payloader,
            encoding_name=webrtc_codec.encoding_name
        );

        let encode_bin =
            gst::parse_bin_from_description_with_name(bin_description, false, "encode-bin")?;

        pipeline.add(&encode_bin).expect("Failed to add encode bin");

        let video_queue = pipeline.by_name("vqueue").expect("No vqueue found");
        let encoder = encode_bin.by_name("encoder").expect("No encoder");

        let srcpad = video_queue
            .static_pad("src")
            .expect("Failed to get video queue src pad");
        let sinkpad = encoder
            .static_pad("sink")
            .expect("Failed to get sink pad from encoder");

        if let Ok(video_ghost_pad) = gst::GhostPad::with_target(Some("video_sink"), &sinkpad) {
            encode_bin.add_pad(&video_ghost_pad)?;
            srcpad.link(&video_ghost_pad)?;
        }

        let sinkpad2 = webrtcbin
            .request_pad_simple("sink_%u")
            .expect("Unable to request outgoing webrtcbin pad");
        let vsink = encode_bin
            .by_name("webrtc-vsink")
            .expect("No webrtc-vsink found");
        let srcpad = vsink.static_pad("src").expect("Element without src pad");
        if let Ok(webrtc_ghost_pad) = gst::GhostPad::with_target(Some("webrtc_video_src"), &srcpad)
        {
            encode_bin.add_pad(&webrtc_ghost_pad)?;
            webrtc_ghost_pad.link(&sinkpad2)?;
        }

        let transceiver = webrtcbin.emit_by_name::<glib::Object>("get-transceiver", &[&0i32]);
        transceiver.set_property("do-nack", false);

        let (send_ws_msg_tx, send_ws_msg_rx) = mpsc::unbounded::<WsMessage>();

        let connection_handle = ConnectionHandle {
            id: handle,
            session_id,
        };

        let peer = Peer(Arc::new(PeerInner {
            handle: connection_handle,
            bin: pipeline,
            webrtcbin,
            send_msg_tx: Arc::new(Mutex::new(send_ws_msg_tx)),
        }));

        // Connect to on-negotiation-needed to handle sending an Offer
        let peer_clone = peer.downgrade();
        peer.webrtcbin.connect_closure(
            "on-negotiation-needed",
            false,
            glib::closure!(move |_webrtcbin: &gst::Element| {
                let peer = upgrade_weak!(peer_clone);
                if let Err(err) = peer.on_negotiation_needed() {
                    gst::element_error!(
                        peer.bin,
                        gst::LibraryError::Failed,
                        ("Failed to negotiate: {:?}", err)
                    );
                }
            }),
        );

        // Whenever there is a new ICE candidate, send it to the peer
        let peer_clone = peer.downgrade();
        peer.webrtcbin.connect_closure(
            "on-ice-candidate",
            false,
            glib::closure!(
                move |_webrtcbin: &gst::Element, mlineindex: u32, candidate: &str| {
                    let peer = upgrade_weak!(peer_clone);
                    if let Err(err) = peer.on_ice_candidate(mlineindex, candidate) {
                        gst::element_error!(
                            peer.bin,
                            gst::LibraryError::Failed,
                            ("Failed to send ICE candidate: {:?}", err)
                        );
                    }
                }
            ),
        );

        // Split the websocket into the Sink and Stream
        let (ws_sink, ws_stream) = ws.split();

        Ok(Self {
            ws_stream: Some(ws_stream.boxed()),
            ws_sink: Some(Box::pin(ws_sink)),
            handle: connection_handle,
            peer: Mutex::new(peer),
            send_ws_msg_rx: Some(send_ws_msg_rx),
        })
    }

    pub async fn run(&mut self) -> Result<(), anyhow::Error> {
        if let Some(ws_stream) = self.ws_stream.take() {
            // Fuse the Stream, required for the select macro
            let mut ws_stream = ws_stream.fuse();

            // Channel for outgoing WebSocket messages from other threads
            let send_ws_msg_rx = self
                .send_ws_msg_rx
                .take()
                .expect("Invalid message receiver");
            let mut send_ws_msg_rx = send_ws_msg_rx.fuse();

            let timer = glib::interval_stream(Duration::from_secs(10));
            let mut timer_fuse = timer.fuse();

            let mut sink = self.ws_sink.take().expect("Invalid websocket sink");
            loop {
                let ws_msg = futures::select! {
                    // Handle the WebSocket messages here
                    ws_msg = ws_stream.select_next_some() => {
                        match ws_msg? {
                            WsMessage::Close(_) => {
                                info!("peer disconnected");
                                break
                            },
                            WsMessage::Ping(data) => Some(WsMessage::Pong(data)),
                            WsMessage::Pong(_) => None,
                            WsMessage::Binary(_) => None,
                            WsMessage::Text(text) => {
                                if let Err(err) = self.handle_websocket_message(&text) {
                                    error!("Failed to parse message: {} ... error: {}", &text, err);
                                }
                                None
                            },
                            WsMessage::Frame(_) => unreachable!(),
                        }
                    },
                    // Handle WebSocket messages we created asynchronously
                    // to send them out now
                    ws_msg = send_ws_msg_rx.select_next_some() => Some(ws_msg),

                    // Handle keepalive ticks, fired every 10 seconds
                    _ws_msg = timer_fuse.select_next_some() => {
                        let transaction = transaction_id();
                        Some(WsMessage::Text(
                            json!({
                                "janus": "keepalive",
                                "transaction": transaction,
                                "handle_id": self.handle.id,
                                "session_id": self.handle.session_id,
                            }).to_string(),
                        ))
                    },
                    // Once we're done, break the loop and return
                    complete => break,
                };

                // If there's a message to send out, do so now
                if let Some(ws_msg) = ws_msg {
                    sink.send(ws_msg).await?;
                }
            }
        }
        Ok(())
    }

    fn handle_jsep(&self, jsep: &JsepHolder) -> Result<(), anyhow::Error> {
        if let Some(sdp) = &jsep.sdp {
            assert_eq!(jsep.type_, "answer");
            let peer = self.peer.lock().expect("Invalid peer");
            return peer.handle_sdp(&jsep.type_, sdp);
        } else if let Some(ice) = &jsep.ice {
            let peer = self.peer.lock().expect("Invalid peer");
            return peer.handle_ice(ice.sdp_mline_index, &ice.candidate);
        }

        Ok(())
    }

    // Handle WebSocket messages, both our own as well as WebSocket protocol messages
    fn handle_websocket_message(&self, msg: &str) -> Result<(), anyhow::Error> {
        trace!("Incoming raw message: {}", msg);
        let json_msg: JsonReply = serde_json::from_str(msg)?;
        let payload_type = &json_msg.base.janus;
        if payload_type == "ack" {
            trace!(
                "Ack transaction {:#?}, sessionId {:#?}",
                json_msg.base.transaction,
                json_msg.base.session_id
            );
        } else {
            debug!("Incoming JSON WebSocket message: {:#?}", json_msg);
        }
        if payload_type == "event" {
            if let Some(_plugin_data) = json_msg.plugin_data {
                if let Some(jsep) = json_msg.jsep {
                    return self.handle_jsep(&jsep);
                }
            }
        }
        Ok(())
    }
}
