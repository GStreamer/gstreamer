import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import org.asynchttpclient.DefaultAsyncHttpClient;
import org.asynchttpclient.DefaultAsyncHttpClientConfig;
import org.asynchttpclient.ws.WebSocket;
import org.asynchttpclient.ws.WebSocketListener;
import org.asynchttpclient.ws.WebSocketUpgradeHandler;
import org.freedesktop.gstreamer.*;
import org.freedesktop.gstreamer.Element.PAD_ADDED;
import org.freedesktop.gstreamer.elements.DecodeBin;
import org.freedesktop.gstreamer.elements.WebRTCBin;
import org.freedesktop.gstreamer.elements.WebRTCBin.CREATE_OFFER;
import org.freedesktop.gstreamer.elements.WebRTCBin.ON_ICE_CANDIDATE;
import org.freedesktop.gstreamer.elements.WebRTCBin.ON_NEGOTIATION_NEEDED;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

/**
 * Demo gstreamer app for negotiating and streaming a sendrecv webrtc stream
 * with a browser JS app.
 *
 * @author stevevangasse
 */
public class WebrtcSendRecv {

    private static final Logger logger = LoggerFactory.getLogger(WebrtcSendRecv.class);
    private static final String REMOTE_SERVER_URL = "wss://webrtc.gstreamer.net:8443";
    private static final String VIDEO_BIN_DESCRIPTION = "videotestsrc ! videoconvert ! queue ! vp8enc deadline=1 ! rtpvp8pay ! queue ! capsfilter caps=application/x-rtp,media=video,encoding-name=VP8,payload=97";
    private static final String AUDIO_BIN_DESCRIPTION = "audiotestsrc ! audioconvert ! audioresample ! queue ! opusenc perfect-timestamp=true ! rtpopuspay ! queue ! capsfilter caps=application/x-rtp,media=audio,encoding-name=OPUS,payload=96";

    private final String serverUrl;
    private final String peerId;
    private final ObjectMapper mapper = new ObjectMapper();
    private WebSocket websocket;
    private WebRTCBin webRTCBin;
    private Pipeline pipe;

    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            logger.error("Please pass at least the peer-id from the signalling server e.g java -jar build/libs/gst-java.jar --peer-id=1234 --server=wss://webrtc.gstreamer.net:8443");
            return;
        }
        String serverUrl = REMOTE_SERVER_URL;
        String peerId = null;
        for (int i=0; i<args.length; i++) {
            if (args[i].startsWith("--server=")) {
                serverUrl = args[i].substring("--server=".length());
            } else if (args[i].startsWith("--peer-id=")) {
                peerId = args[i].substring("--peer-id=".length());
            }
        }
        logger.info("Using peer id {}, on server: {}", peerId, serverUrl);
        WebrtcSendRecv webrtcSendRecv = new WebrtcSendRecv(peerId, serverUrl);
        webrtcSendRecv.startCall();
    }

    private WebrtcSendRecv(String peerId, String serverUrl) {
        this.peerId = peerId;
        this.serverUrl = serverUrl;
        Gst.init();
        webRTCBin = new WebRTCBin("sendrecv");

        Bin video = Gst.parseBinFromDescription(VIDEO_BIN_DESCRIPTION, true);
        Bin audio = Gst.parseBinFromDescription(AUDIO_BIN_DESCRIPTION, true);

        pipe = new Pipeline();
        pipe.addMany(webRTCBin, video, audio);
        video.link(webRTCBin);
        audio.link(webRTCBin);
        setupPipeLogging(pipe);

        // When the pipeline goes to PLAYING, the on_negotiation_needed() callback will be called, and we will ask webrtcbin to create an offer which will match the pipeline above.
        webRTCBin.connect(onNegotiationNeeded);
        webRTCBin.connect(onIceCandidate);
        webRTCBin.connect(onIncomingStream);
    }

    private void startCall() throws Exception {
        DefaultAsyncHttpClientConfig httpClientConfig =
                new DefaultAsyncHttpClientConfig
                        .Builder()
                        .setUseInsecureTrustManager(true)
                        .build();

        websocket = new DefaultAsyncHttpClient(httpClientConfig)
                .prepareGet(serverUrl)
                .execute(
                        new WebSocketUpgradeHandler
                                .Builder()
                                .addWebSocketListener(webSocketListener)
                                .build())
                .get();

        Gst.main();
    }

    private WebSocketListener webSocketListener = new WebSocketListener() {

        @Override
        public void onOpen(WebSocket websocket) {
            logger.info("websocket onOpen");
            websocket.sendTextFrame("HELLO 564322");
        }

        @Override
        public void onClose(WebSocket websocket, int code, String reason) {
            logger.info("websocket onClose: " + code + " : " + reason);
            Gst.quit();
        }

        @Override
        public void onTextFrame(String payload, boolean finalFragment, int rsv) {
            if (payload.equals("HELLO")) {
                websocket.sendTextFrame("SESSION " + peerId);
            } else if (payload.equals("SESSION_OK")) {
                pipe.play();
            } else if (payload.startsWith("ERROR")) {
                logger.error(payload);
                Gst.quit();
            } else {
                handleSdp(payload);
            }
        }

        @Override
        public void onError(Throwable t) {
            logger.error("onError", t);
        }
    };

    private void handleSdp(String payload) {
        try {
            JsonNode answer = mapper.readTree(payload);
            if (answer.has("sdp")) {
                String sdpStr = answer.get("sdp").get("sdp").textValue();
                logger.info("answer SDP:\n{}", sdpStr);
                SDPMessage sdpMessage = new SDPMessage();
                sdpMessage.parseBuffer(sdpStr);
                WebRTCSessionDescription description = new WebRTCSessionDescription(WebRTCSDPType.ANSWER, sdpMessage);
                webRTCBin.setRemoteDescription(description);
            }
            else if (answer.has("ice")) {
                String candidate = answer.get("ice").get("candidate").textValue();
                int sdpMLineIndex = answer.get("ice").get("sdpMLineIndex").intValue();
                logger.info("Adding ICE candidate: {}", candidate);
                webRTCBin.addIceCandidate(sdpMLineIndex, candidate);
            }
        } catch (IOException e) {
            logger.error("Problem reading payload", e);
        }
    }

    private CREATE_OFFER onOfferCreated = offer -> {
        webRTCBin.setLocalDescription(offer);
        try {
            JsonNode rootNode = mapper.createObjectNode();
            JsonNode sdpNode = mapper.createObjectNode();
            ((ObjectNode) sdpNode).put("type", "offer");
            ((ObjectNode) sdpNode).put("sdp", offer.getSDPMessage().toString());
            ((ObjectNode) rootNode).set("sdp", sdpNode);
            String json = mapper.writeValueAsString(rootNode);
            logger.info("Sending offer:\n{}", json);
            websocket.sendTextFrame(json);
        } catch (JsonProcessingException e) {
            logger.error("Couldn't write JSON", e);
        }
    };

    private ON_NEGOTIATION_NEEDED onNegotiationNeeded = elem -> {
        logger.info("onNegotiationNeeded: " + elem.getName());

        // When webrtcbin has created the offer, it will hit our callback and we send SDP offer over the websocket to signalling server
        webRTCBin.createOffer(onOfferCreated);
    };

    private ON_ICE_CANDIDATE onIceCandidate = (sdpMLineIndex, candidate) -> {
        JsonNode rootNode = mapper.createObjectNode();
        JsonNode iceNode = mapper.createObjectNode();
        ((ObjectNode) iceNode).put("candidate", candidate);
        ((ObjectNode) iceNode).put("sdpMLineIndex", sdpMLineIndex);
        ((ObjectNode) rootNode).set("ice", iceNode);

        try {
            String json = mapper.writeValueAsString(rootNode);
            logger.info("ON_ICE_CANDIDATE: " + json);
            websocket.sendTextFrame(json);
        } catch (JsonProcessingException e) {
            logger.error("Couldn't write JSON", e);
        }
    };

    private PAD_ADDED onIncomingDecodebinStream = (element, pad) -> {
        logger.info("onIncomingDecodebinStream");
        if (!pad.hasCurrentCaps()) {
            logger.info("Pad has no caps, ignoring: {}", pad.getName());
            return;
        }
        Structure caps = pad.getCaps().getStructure(0);
        String name = caps.getName();
        if (name.startsWith("video")) {
            logger.info("onIncomingDecodebinStream video");
            Element queue = ElementFactory.make("queue", "my-videoqueue");
            Element videoconvert = ElementFactory.make("videoconvert", "my-videoconvert");
            Element autovideosink = ElementFactory.make("autovideosink", "my-autovideosink");
            pipe.addMany(queue, videoconvert, autovideosink);
            queue.syncStateWithParent();
            videoconvert.syncStateWithParent();
            autovideosink.syncStateWithParent();
            pad.link(queue.getStaticPad("sink"));
            queue.link(videoconvert);
            videoconvert.link(autovideosink);
        }
        if (name.startsWith("audio")) {
            logger.info("onIncomingDecodebinStream audio");
            Element queue = ElementFactory.make("queue", "my-audioqueue");
            Element audioconvert = ElementFactory.make("audioconvert", "my-audioconvert");
            Element audioresample = ElementFactory.make("audioresample", "my-audioresample");
            Element autoaudiosink = ElementFactory.make("autoaudiosink", "my-autoaudiosink");
            pipe.addMany(queue, audioconvert, audioresample, autoaudiosink);
            queue.syncStateWithParent();
            audioconvert.syncStateWithParent();
            audioresample.syncStateWithParent();
            autoaudiosink.syncStateWithParent();
            pad.link(queue.getStaticPad("sink"));
            queue.link(audioconvert);
            audioconvert.link(audioresample);
            audioresample.link(autoaudiosink);
        }
    };

    private PAD_ADDED onIncomingStream = (element, pad) -> {
        if (pad.getDirection() != PadDirection.SRC) {
            logger.info("Pad is not source, ignoring: {}", pad.getDirection());
            return;
        }
        logger.info("Receiving stream! Element: {} Pad: {}", element.getName(), pad.getName());
        DecodeBin decodebin = new DecodeBin("my-decoder-" + pad.getName());
        decodebin.connect(onIncomingDecodebinStream);
        pipe.add(decodebin);
        decodebin.syncStateWithParent();
        webRTCBin.link(decodebin);
    };

    private void setupPipeLogging(Pipeline pipe) {
        Bus bus = pipe.getBus();
        bus.connect((Bus.EOS) source -> {
            logger.info("Reached end of stream: " + source.toString());
            Gst.quit();
        });

        bus.connect((Bus.ERROR) (source, code, message) -> {
            logger.error("Error from source: '{}', with code: {}, and message '{}'", source, code, message);
        });

        bus.connect((source, old, current, pending) -> {
            if (source instanceof Pipeline) {
                logger.info("Pipe state changed from {} to new {}", old, current);
            }
        });
    }
}

