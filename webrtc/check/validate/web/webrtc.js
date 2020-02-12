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
var rtc_configuration = {iceServers: [{urls: "stun:stun.services.mozilla.com"},
                                      {urls: "stun:stun.l.google.com:19302"},]};
var default_constraints = {video: true, audio: false};

var connect_attempts = 0;
var peer_connection;
var channels = []
var ws_conn;
// Promise for local stream after constraints are approved by the user
var local_stream_promise;

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
    return document.getElementById("stream");
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
    ws_conn.send(JSON.stringify({'STATE': 'error', 'msg' : text}))
}

function resetVideo() {
    // Release the webcam and mic
    if (local_stream_promise)
        local_stream_promise.then(stream => {
            if (stream) {
                stream.getTracks().forEach(function (track) { track.stop(); });
            }
        });

    // Reset the video element and stop showing the last received frame
    var videoElement = getVideoElement();
    videoElement.pause();
    videoElement.src = "";
    videoElement.load();
}

function updateRemoteStateFromSetSDPJson(sdp) {
  if (sdp.type == "offer")
      ws_conn.send(JSON.stringify({'STATE': 'offer-set', 'description' : sdp}))
  else if (sdp.type == "answer")
      ws_conn.send(JSON.stringify({'STATE': 'answer-set', 'description' : sdp}))
  else
      throw new Error ("Unknown SDP type!");
}

function updateRemoteStateFromGeneratedSDPJson(sdp) {
  if (sdp.type == "offer")
      ws_conn.send(JSON.stringify({'STATE': 'offer-created', 'description' : sdp}))
  else if (sdp.type == "answer")
      ws_conn.send(JSON.stringify({'STATE': 'answer-created', 'description' : sdp}))
  else
      throw new Error ("Unknown SDP type!");
}

// SDP offer received from peer, set remote description and create an answer
function onIncomingSDP(sdp) {
    peer_connection.setRemoteDescription(sdp).then(() => {
        updateRemoteStateFromSetSDPJson(sdp)
        setStatus("Set remote SDP", sdp.type);
    }).catch(setError);
}

// Local description was set, send it to peer
function onLocalDescription(desc) {
    updateRemoteStateFromGeneratedSDPJson(desc)
    console.log("Got local description: " + JSON.stringify(desc));
    peer_connection.setLocalDescription(desc).then(function() {
        updateRemoteStateFromSetSDPJson(desc)
        sdp = {'sdp': desc}
        setStatus("Sending SDP", sdp.type);
        ws_conn.send(JSON.stringify(sdp));
    });
}

// ICE candidate received from peer, add it to the peer connection
function onIncomingICE(ice) {
    var candidate = new RTCIceCandidate(ice);
    console.log("adding candidate", candidate)
    peer_connection.addIceCandidate(candidate).catch(setError);
}

function createOffer(offer) {
    local_stream_promise.then((stream) => {
        setStatus("Got local stream, creating offer");
        peer_connection.createOffer()
            .then(onLocalDescription).catch(setError);
    }).catch(setError)
}

function createAnswer(offer) {
    local_stream_promise.then((stream) => {
        setStatus("Got local stream, creating answer");
        peer_connection.createAnswer()
            .then(onLocalDescription).catch(setError);
    }).catch(setError)
}

function handleOptions(options) {
    console.log ('received options', options);
    if (options.bundlePolicy != null) {
        rtc_configuration['bundlePolicy'] = options.bundlePolicy;
    }
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

            if (msg.SET_TITLE != null) {
                // some debugging for tests that hang around
                document.title = msg['SET_TITLE']
                return;
            } else if (msg.OPTIONS != null) {
                handleOptions(msg.OPTIONS);
                return;
            }

            // Incoming JSON signals the beginning of a call
            if (!peer_connection)
                createCall();

            if (msg.sdp != null) {
                onIncomingSDP(msg.sdp);
            } else if (msg.ice != null) {
                onIncomingICE(msg.ice);
            } else if (msg.CREATE_OFFER != null) {
                createOffer(msg.CREATE_OFFER)
            } else if (msg.CREATE_ANSWER != null) {
                createAnswer(msg.CREATE_ANSWER)
            } else if (msg.DATA_CREATE != null) {
                addDataChannel(msg.DATA_CREATE.id)
            } else if (msg.DATA_CLOSE != null) {
                closeDataChannel(msg.DATA_CLOSE.id)
            } else if (msg.DATA_SEND_MSG != null) {
                sendDataChannelMessage(msg.DATA_SEND_MSG)
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
        peer_connection = null;
    }
    channels = []

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
    constraints = default_constraints;
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
    // Fetch the peer id to use
    var url = new URL(window.location.href);

    peer_id = url.searchParams.get("id");
    peer_id = peer_id || default_peer_id || getOurId();

    ws_port = ws_port || url.searchParams.get("port");
    ws_port = ws_port || '8443';

    ws_server = ws_server || url.searchParams.get("server");
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
    });
    ws_conn.addEventListener('error', onServerError);
    ws_conn.addEventListener('message', onServerMessage);
    ws_conn.addEventListener('close', onServerClose);
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
    setError("Browser doesn't support getUserMedia!");
}

const handleDataChannelMessageReceived = (event) =>{
    console.log("dataChannel.OnMessage:", event, event.data.type);
    setStatus("Received data channel message");
    ws_conn.send(JSON.stringify({'DATA-MSG' : event.data, 'id' : event.target.label}));
};

const handleDataChannelOpen = (event) =>{
    console.log("dataChannel.OnOpen", event);
    ws_conn.send(JSON.stringify({'DATA-STATE' : 'open', 'id' : event.target.label}));
};

const handleDataChannelError = (error) =>{
    console.log("dataChannel.OnError:", error);
    ws_conn.send(JSON.stringify({'DATA-STATE' : error, 'id' : event.target.label}));
};

const handleDataChannelClose = (event) =>{
    console.log("dataChannel.OnClose", event);
    ws_conn.send(JSON.stringify({'DATA-STATE' : 'closed', 'id' : event.target.label}));
};

function onDataChannel(event) {
    setStatus("Data channel created");
    let channel = event.channel;
    console.log('adding remote data channel with label', channel.label)
    ws_conn.send(JSON.stringify({'DATA-NEW' : {'id' : channel.label, 'location' : 'remote'}}));
    channel.onopen = handleDataChannelOpen;
    channel.onmessage = handleDataChannelMessageReceived;
    channel.onerror = handleDataChannelError;
    channel.onclose = handleDataChannelClose;
    channels.push(channel)
}

function addDataChannel(label) {
    channel = peer_connection.createDataChannel(label, null);
    console.log('adding local data channel with label', label)
    ws_conn.send(JSON.stringify({'DATA-NEW' : {'id' : label, 'location' : 'local'}}));
    channel.onopen = handleDataChannelOpen;
    channel.onmessage = handleDataChannelMessageReceived;
    channel.onerror = handleDataChannelError;
    channel.onclose = handleDataChannelClose;
    channels.push(channel)
}

function find_channel(label) {
    console.log('find', label, 'in', channels)
    for (var c in channels) {
        if (channels[c].label === label) {
            console.log('found', label, c)
            return channels[c];
        }
    }
    return null;
}

function closeDataChannel(label) {
    channel = find_channel (label)
    console.log('closing data channel with label', label)
    channel.close()
}

function sendDataChannelMessage(msg) {
    channel = find_channel (msg.id)
    console.log('sending on data channel', msg.id, 'message', msg.msg)
    channel.send(msg.msg)
}

function createCall() {
    // Reset connection attempts because we connected successfully
    connect_attempts = 0;

    console.log('Creating RTCPeerConnection with configuration', rtc_configuration);

    peer_connection = new RTCPeerConnection(rtc_configuration);
    peer_connection.ondatachannel = onDataChannel;
    peer_connection.onaddstream = onRemoteStreamAdded;
    /* Send our video/audio to the other peer */
    local_stream_promise = getLocalStream().then((stream) => {
        console.log('Adding local stream');
        peer_connection.addStream(stream);
        return stream;
    }).catch(setError);

    peer_connection.onicecandidate = (event) => {
        // We have a candidate, send it to the remote party with the
        // same uuid
        if (event.candidate == null) {
            console.log("ICE Candidate was null, done");
            return;
        }
        console.log("generated ICE Candidate", event.candidate);
        ws_conn.send(JSON.stringify({'ice': event.candidate}));
    };

    setStatus("Created peer connection for call, waiting for SDP");
}
