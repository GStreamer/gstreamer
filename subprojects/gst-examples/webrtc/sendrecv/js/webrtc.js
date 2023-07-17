/* vim: set sts=4 sw=4 et :
 *
 * Demo Javascript app for negotiating and streaming a sendrecv webrtc stream
 * with a GStreamer app. Runs only in passive mode, i.e., responds to offers
 * with answers, exchanges ICE candidates, and streams.
 *
 * Author: Nirbheek Chauhan <nirbheek@centricular.com>
 */

// Set this to override the automatic detection in websocketServerConnect()
var ws_server;
var ws_port;
// Set this to use a specific peer id instead of a random one
var default_peer_id;
// Override with your own STUN servers if you want
var rtc_configuration = {iceServers: [{urls: "stun:stun.l.google.com:19302"}]};
// The default constraints that will be attempted. Can be overriden by the user.
var default_constraints = {video: true, audio: true};

var connect_attempts = 0;
var peer_connection = new RTCPeerConnection(rtc_configuration);
var send_channel;
var ws_conn;
// Local stream after constraints are approved by the user
var local_stream = null;

// keep track of some negotiation state to prevent races and errors
var callCreateTriggered = false;
var makingOffer = false;
var isSettingRemoteAnswerPending = false;

function setConnectButtonState(value) {
    document.getElementById("peer-connect-button").value = value;
}

function wantRemoteOfferer() {
   return document.getElementById("remote-offerer").checked;
}

function onConnectClicked() {
    if (document.getElementById("peer-connect-button").value == "Disconnect") {
        resetState();
        return;
    }

    var id = document.getElementById("peer-connect").value;
    if (id == "") {
        alert("Peer id must be filled out");
        return;
    }

    ws_conn.send("SESSION " + id);
    setConnectButtonState("Disconnect");
}

function onTextKeyPress(e) {
    e = e ? e : window.event;
    if (e.code == "Enter") {
        onConnectClicked();
        return false;
    }
    return true;
}

function getOurId() {
    return Math.floor(Math.random() * (9000 - 10) + 10).toString();
}

function resetState() {
    // This will call onServerClose()
    ws_conn.close();
}

function handleIncomingError(error) {
    setError("ERROR: " + error);
    resetState();
}

function getVideoElement() {
    var div = document.getElementById("video");
    var video_tag = document.createElement("video");
    video_tag.textContent = "Your browser doesn't support video";
    video_tag.autoplay = true;
    video_tag.playsinline = true;
    div.appendChild(video_tag);
    return video_tag
}

function setStatus(text) {
    console.log(text);
    var span = document.getElementById("status")
    // Don't set the status if it already contains an error
    if (!span.classList.contains('error'))
        span.textContent = text;
}

function setError(text) {
    console.error(text);
    var span = document.getElementById("status")
    span.textContent = text;
    span.classList.add('error');
}

function resetVideo() {
    // Release the webcam and mic
    if (local_stream) {
        local_stream.then(stream => {
            if (stream) {
                stream.getTracks().forEach(function (track) { track.stop(); });
            }
        });
        local_stream = null;
    }

    // Remove all video players
    document.getElementById("video").innerHTML = "";
}

function onIncomingSDP(sdp) {
    try {
        // An offer may come in while we are busy processing SRD(answer).
        // In this case, we will be in "stable" by the time the offer is processed
        // so it is safe to chain it on our Operations Chain now.
        const readyForOffer =
            !makingOffer &&
            (peer_connection.signalingState == "stable" || isSettingRemoteAnswerPending);
        const offerCollision = sdp.type == "offer" && !readyForOffer;

        if (offerCollision) {
            return;
        }
        isSettingRemoteAnswerPending = sdp.type == "answer";
        peer_connection.setRemoteDescription(sdp).then(() => {
            setStatus("Remote SDP set");
            isSettingRemoteAnswerPending = false;
            if (sdp.type == "offer") {
                setStatus("Got SDP offer, waiting for getUserMedia to complete");
                local_stream.then((stream) => {
                    setStatus("getUserMedia to completed, setting local description");
                    peer_connection.setLocalDescription().then(() => {
                        let desc = peer_connection.localDescription;
                        console.log("Got local description: " + JSON.stringify(desc));
                        setStatus("Sending SDP " + desc.type);
                        ws_conn.send(JSON.stringify({'sdp': desc}));
                        if (peer_connection.iceConnectionState == "connected") {
                            setStatus("SDP " + desc.type + " sent, ICE connected, all looks OK");
                        }
                    });
                });
            }
        });
    } catch (err) {
        handleIncomingError(err);
    }
}

// Local description was set by incoming SDP offer, send answer to peer
function onLocalDescription(desc) {
    if (desc.type != "answer") {
        console.warn("Expected SDP answer, received: " + desc.type);
    }
    console.log("Got local description: " + JSON.stringify(desc));
    peer_connection.setLocalDescription(desc).then(() => {
        var dsc = peer_connection.localDescription;
        setStatus("Sending SDP " + desc.type);
        ws_conn.send(JSON.stringify({'sdp': desc}));
    });
}

// ICE candidate received from peer, add it to the peer connection
function onIncomingICE(ice) {
    var candidate = new RTCIceCandidate(ice);
    peer_connection.addIceCandidate(candidate).catch(setError);
}

function onServerMessage(event) {
    console.log("Received " + event.data);
    switch (event.data) {
        case "HELLO":
            setStatus("Registered with server, waiting for call");
            return;
        case "SESSION_OK":
            setStatus("Starting negotiation");
            if (wantRemoteOfferer()) {
                ws_conn.send("OFFER_REQUEST");
                setStatus("Sent OFFER_REQUEST, waiting for offer");
                return;
            }
            if (!callCreateTriggered) {
                createCall();
                setStatus("Created peer connection for call, waiting for SDP");
            }
            return;
        case "OFFER_REQUEST":
            // The peer wants us to set up and then send an offer
            if (!callCreateTriggered)
                createCall();
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
            if (!callCreateTriggered)
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
    setStatus('Disconnected from server');
    resetVideo();

    if (peer_connection) {
        peer_connection.close();
        peer_connection = new RTCPeerConnection(rtc_configuration);
    }
    callCreateTriggered = false;

    // Reset after a second
    window.setTimeout(websocketServerConnect, 1000);
}

function onServerError(event) {
    setError("Unable to connect to server, did you add an exception for the certificate?")
    // Retry after 3 seconds
    window.setTimeout(websocketServerConnect, 3000);
}

function getLocalStream() {
    var constraints;
    var textarea = document.getElementById('constraints');
    try {
        constraints = JSON.parse(textarea.value);
    } catch (e) {
        console.error(e);
        setError('ERROR parsing constraints: ' + e.message + ', using default constraints');
        constraints = default_constraints;
    }
    console.log(JSON.stringify(constraints));

    // Add local stream
    if (navigator.mediaDevices.getUserMedia) {
        return navigator.mediaDevices.getUserMedia(constraints);
    } else {
        errorUserMediaHandler();
    }
}

function websocketServerConnect() {
    connect_attempts++;
    if (connect_attempts > 3) {
        setError("Too many connection attempts, aborting. Refresh page to try again");
        return;
    }
    // Clear errors in the status span
    var span = document.getElementById("status");
    span.classList.remove('error');
    span.textContent = '';
    // Populate constraints
    var textarea = document.getElementById('constraints');
    if (textarea.value == '')
        textarea.value = JSON.stringify(default_constraints);
    // Fetch the peer id to use
    peer_id = default_peer_id || getOurId();
    ws_port = ws_port || '8443';
    if (window.location.protocol.startsWith ("file")) {
        ws_server = ws_server || "127.0.0.1";
    } else if (window.location.protocol.startsWith ("http")) {
        ws_server = ws_server || window.location.hostname;
    } else {
        throw new Error ("Don't know how to connect to the signalling server with uri" + window.location);
    }
    var ws_url = 'wss://' + ws_server + ':' + ws_port
    setStatus("Connecting to server " + ws_url);
    ws_conn = new WebSocket(ws_url);
    /* When connected, immediately register with the server */
    ws_conn.addEventListener('open', (event) => {
        document.getElementById("peer-id").textContent = peer_id;
        ws_conn.send('HELLO ' + peer_id);
        setStatus("Registering with server");
        setConnectButtonState("Connect");
        // Reset connection attempts because we connected successfully
        connect_attempts = 0;
    });
    ws_conn.addEventListener('error', onServerError);
    ws_conn.addEventListener('message', onServerMessage);
    ws_conn.addEventListener('close', onServerClose);
}

function errorUserMediaHandler() {
    setError("Browser doesn't support getUserMedia!");
}

const handleDataChannelOpen = (event) =>{
    console.log("dataChannel.OnOpen", event);
};

const handleDataChannelMessageReceived = (event) =>{
    console.log("dataChannel.OnMessage:", event, event.data.type);

    setStatus("Received data channel message");
    if (typeof event.data === 'string' || event.data instanceof String) {
        console.log('Incoming string message: ' + event.data);
        textarea = document.getElementById("text")
        textarea.value = textarea.value + '\n' + event.data
    } else {
        console.log('Incoming data message');
    }
    send_channel.send("Hi! (from browser)");
};

const handleDataChannelError = (error) =>{
    console.log("dataChannel.OnError:", error);
};

const handleDataChannelClose = (event) =>{
    console.log("dataChannel.OnClose", event);
};

function onDataChannel(event) {
    setStatus("Data channel created");
    let receiveChannel = event.channel;
    receiveChannel.onopen = handleDataChannelOpen;
    receiveChannel.onmessage = handleDataChannelMessageReceived;
    receiveChannel.onerror = handleDataChannelError;
    receiveChannel.onclose = handleDataChannelClose;
}

function createCall() {
    callCreateTriggered = true;
    console.log('Configuring RTCPeerConnection');
    send_channel = peer_connection.createDataChannel('label', null);
    send_channel.onopen = handleDataChannelOpen;
    send_channel.onmessage = handleDataChannelMessageReceived;
    send_channel.onerror = handleDataChannelError;
    send_channel.onclose = handleDataChannelClose;
    peer_connection.ondatachannel = onDataChannel;

    peer_connection.ontrack = ({track, streams}) => {
        console.log("ontrack triggered");
        var videoElem = getVideoElement();
        if (event.track.kind === 'audio')
            videoElem.style.display = 'none';

        videoElem.srcObject = streams[0];
        videoElem.srcObject.addEventListener('mute', (e) => {
            console.log("track muted, hiding video element");
            videoElem.style.display = 'none';
        });
        videoElem.srcObject.addEventListener('unmute', (e) => {
            console.log("track unmuted, showing video element");
            videoElem.style.display = 'block';
        });
        videoElem.srcObject.addEventListener('removetrack', (e) => {
            console.log("track removed, removing video element");
            videoElem.remove();
        });
    };

    peer_connection.onicecandidate = (event) => {
        // We have a candidate, send it to the remote party with the
        // same uuid
        if (event.candidate == null) {
                console.log("ICE Candidate was null, done");
                return;
        }
        ws_conn.send(JSON.stringify({'ice': event.candidate}));
    };
    peer_connection.oniceconnectionstatechange = (event) => {
        if (peer_connection.iceConnectionState == "connected") {
            setStatus("ICE gathering complete");
        }
    };

    // let the "negotiationneeded" event trigger offer generation
    peer_connection.onnegotiationneeded = async () => {
        setStatus("Negotiation needed");
        if (wantRemoteOfferer())
            return;
        try {
            makingOffer = true;
            await peer_connection.setLocalDescription();
            let desc = peer_connection.localDescription;
            setStatus("Sending SDP " + desc.type);
            ws_conn.send(JSON.stringify({'sdp': desc}));
        } catch (err) {
            handleIncomingError(err);
        } finally {
            makingOffer = false;
        }
    };

    /* Send our video/audio to the other peer */
    local_stream = getLocalStream().then((stream) => {
        console.log('Adding local stream');
        for (const track of stream.getTracks()) {
            peer_connection.addTrack(track, stream);
        }
        return stream;
    }).catch(setError);

    setConnectButtonState("Disconnect");
}
