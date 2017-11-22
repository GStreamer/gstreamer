/* vim: set sts=4 sw=4 et :
 *
 * Demo Javascript app for negotiating and streaming a sendrecv webrtc stream
 * with a GStreamer app. Runs only in passive mode, i.e., responds to offers
 * with answers, exchanges ICE candidates, and streams.
 *
 * Author: Nirbheek Chauhan <nirbheek@centricular.com>
 */

var connect_attempts = 0;

var peer_connection = null;
var rtc_configuration = {iceServers: [{urls: "stun:stun.services.mozilla.com"},
                                      {urls: "stun:stun.l.google.com:19302"}]};
var ws_conn;
var local_stream;
var peer_id;

function getOurId() {
    return Math.floor(Math.random() * (9000 - 10) + 10).toString();
}

function resetState() {
    // This will call onServerClose()
    ws_conn.close();
}

function handleIncomingError(error) {
    setStatus("ERROR: " + error);
    resetState();
}

function getVideoElement() {
    return document.getElementById("stream");
}

function setStatus(text) {
    console.log(text);
    document.getElementById("status").textContent = text;
}

function resetVideoElement() {
    var videoElement = getVideoElement();
    videoElement.pause();
    videoElement.src = "";
    videoElement.load();
}

// SDP offer received from peer, set remote description and create an answer
function onIncomingSDP(sdp) {
    console.log("Incoming SDP is "+ JSON.stringify(sdp));
    peer_connection.setRemoteDescription(sdp).then(() => {
        setStatus("Remote SDP set");
        if (sdp.type != "offer")
            return;
        setStatus("Got SDP offer, creating answer");
        peer_connection.createAnswer().then(onLocalDescription).catch(setStatus);
    }).catch(setStatus);
}

// Local description was set, send it to peer
function onLocalDescription(desc) {
    console.log("Got local description: " + JSON.stringify(desc));
    peer_connection.setLocalDescription(desc).then(function() {
        setStatus("Sending SDP answer");
        ws_conn.send(JSON.stringify(peer_connection.localDescription));
    });
}

// ICE candidate received from peer, add it to the peer connection
function onIncomingICE(ice) {
    console.log("Incoming ICE: " + JSON.stringify(ice));
    var candidate = new RTCIceCandidate(ice);
    peer_connection.addIceCandidate(candidate).catch(setStatus);
}

function onServerMessage(event) {
    console.log("Received " + event.data);
    switch (event.data) {
        case "HELLO":
            setStatus("Registered with server, waiting for call");
            return;
        default:
            if (event.data.startsWith("ERROR")) {
                handleIncomingError(event.data);
                return;
            }
            // Handle incoming JSON SDP and ICE messages
            try {
                msg = JSON.parse(event.data);
            } catch (e) {
                if (e instanceof SyntaxError) {
                    handleIncomingError("Error parsing incoming JSON: " + event.data);
                } else {
                    handleIncomingError("Unknown error parsing response: " + event.data);
                }
                return;
            }

            // Incoming JSON signals the beginning of a call
            if (peer_connection == null)
                createCall(msg);

            if (msg.sdp != null) {
                onIncomingSDP(msg.sdp);
            } else if (msg.ice != null) {
                onIncomingICE(msg.ice);
            } else {
                handleIncomingError("Unknown incoming JSON: " + msg);
            }
    }
}

function onServerClose(event) {
    resetVideoElement();

    if (peer_connection != null) {
        peer_connection.close();
        peer_connection = null;
    }

    // Reset after a second
    window.setTimeout(websocketServerConnect, 1000);
}

function onServerError(event) {
    setStatus("Unable to connect to server, did you add an exception for the certificate?")
    // Retry after 3 seconds
    window.setTimeout(websocketServerConnect, 3000);
}

function websocketServerConnect() {
    connect_attempts++;
    if (connect_attempts > 3) {
        setStatus("Too many connection attempts, aborting. Refresh page to try again");
        return;
    }
    peer_id = getOurId();
    setStatus("Connecting to server");
    loc = null;
    if (window.location.protocol.startsWith ("file")) {
        loc = "127.0.0.1";
    } else if (window.location.protocol.startsWith ("http")) {
        loc = window.location.hostname;
    } else {
        throw new Error ("Don't know how to connect to the signalling server with uri" + window.location);
    }
    ws_conn = new WebSocket('wss://' + loc + ':8443');
    /* When connected, immediately register with the server */
    ws_conn.addEventListener('open', (event) => {
        document.getElementById("peer-id").textContent = peer_id;
        ws_conn.send('HELLO ' + peer_id);
        setStatus("Registering with server");
    });
    ws_conn.addEventListener('error', onServerError);
    ws_conn.addEventListener('message', onServerMessage);
    ws_conn.addEventListener('close', onServerClose);

    var constraints = {video: true, audio: true};

    // Add local stream
    if (navigator.mediaDevices.getUserMedia) {
        navigator.mediaDevices.getUserMedia(constraints)
            .then((stream) => { local_stream = stream })
            .catch(errorUserMediaHandler);
    } else {
        errorUserMediaHandler();
    }
}

function onRemoteStreamAdded(event) {
    videoTracks = event.stream.getVideoTracks();
    audioTracks = event.stream.getAudioTracks();

    if (videoTracks.length > 0) {
        console.log('Incoming stream: ' + videoTracks.length + ' video tracks and ' + audioTracks.length + ' audio tracks');
        getVideoElement().srcObject = event.stream;
    } else {
        handleIncomingError('Stream with unknown tracks added, resetting');
    }
}

function errorUserMediaHandler() {
    setStatus("Browser doesn't support getUserMedia!");
}

function createCall(msg) {
    // Reset connection attempts because we connected successfully
    connect_attempts = 0;

    peer_connection = new RTCPeerConnection(rtc_configuration);
    peer_connection.onaddstream = onRemoteStreamAdded;
    /* Send our video/audio to the other peer */
    peer_connection.addStream(local_stream);

    if (!msg.sdp) {
        console.log("WARNING: First message wasn't an SDP message!?");
    }

    peer_connection.onicecandidate = (event) => {
	// We have a candidate, send it to the remote party with the
	// same uuid
	if (event.candidate == null) {
            console.log("ICE Candidate was null, done");
            return;
	}
	ws_conn.send(JSON.stringify({'ice': event.candidate}));
    };

    setStatus("Created peer connection for call, waiting for SDP");
}
