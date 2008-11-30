<?xml version="1.0"?>
<api version="1.0">
	<namespace name="Gst">
		<struct name="GstRTSPMediaStream">
			<field name="media" type="GstRTSPMedia*"/>
			<field name="idx" type="guint"/>
			<field name="name" type="gchar*"/>
			<field name="element" type="GstElement*"/>
			<field name="srcpad" type="GstPad*"/>
			<field name="payloader" type="GstElement*"/>
			<field name="caps_sig" type="gulong"/>
			<field name="caps" type="GstCaps*"/>
		</struct>
		<struct name="GstRTSPSessionMedia">
			<method name="pause" symbol="gst_rtsp_session_media_pause">
				<return-type type="GstStateChangeReturn"/>
				<parameters>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
				</parameters>
			</method>
			<method name="play" symbol="gst_rtsp_session_media_play">
				<return-type type="GstStateChangeReturn"/>
				<parameters>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
				</parameters>
			</method>
			<method name="stop" symbol="gst_rtsp_session_media_stop">
				<return-type type="GstStateChangeReturn"/>
				<parameters>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
				</parameters>
			</method>
			<field name="session" type="GstRTSPSession*"/>
			<field name="media" type="GstRTSPMedia*"/>
			<field name="pipeline" type="GstElement*"/>
			<field name="rtpbin" type="GstElement*"/>
			<field name="fdsink" type="GstElement*"/>
			<field name="streams" type="GList*"/>
		</struct>
		<struct name="GstRTSPSessionStream">
			<method name="set_transport" symbol="gst_rtsp_session_stream_set_transport">
				<return-type type="GstRTSPTransport*"/>
				<parameters>
					<parameter name="stream" type="GstRTSPSessionStream*"/>
					<parameter name="destination" type="gchar*"/>
					<parameter name="ct" type="GstRTSPTransport*"/>
				</parameters>
			</method>
			<field name="idx" type="guint"/>
			<field name="media" type="GstRTSPSessionMedia*"/>
			<field name="media_stream" type="GstRTSPMediaStream*"/>
			<field name="destination" type="gchar*"/>
			<field name="client_trans" type="GstRTSPTransport*"/>
			<field name="server_trans" type="GstRTSPTransport*"/>
			<field name="recv_rtcp_sink" type="GstPad*"/>
			<field name="send_rtp_sink" type="GstPad*"/>
			<field name="send_rtp_src" type="GstPad*"/>
			<field name="send_rtcp_src" type="GstPad*"/>
			<field name="udpsrc" type="GstElement*[]"/>
			<field name="udpsink" type="GstElement*[]"/>
		</struct>
		<object name="GstRTSPClient" parent="GObject" type-name="GstRTSPClient" get-type="gst_rtsp_client_get_type">
			<method name="accept" symbol="gst_rtsp_client_accept">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="client" type="GstRTSPClient*"/>
					<parameter name="source" type="GIOChannel*"/>
				</parameters>
			</method>
			<method name="get_session_pool" symbol="gst_rtsp_client_get_session_pool">
				<return-type type="GstRTSPSessionPool*"/>
				<parameters>
					<parameter name="client" type="GstRTSPClient*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_client_new">
				<return-type type="GstRTSPClient*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</constructor>
			<method name="set_session_pool" symbol="gst_rtsp_client_set_session_pool">
				<return-type type="void"/>
				<parameters>
					<parameter name="client" type="GstRTSPClient*"/>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<property name="server" type="GstRTSPServer*" readable="1" writable="1" construct="0" construct-only="1"/>
			<field name="server" type="GstRTSPServer*"/>
			<field name="connection" type="GstRTSPConnection*"/>
			<field name="address" type="struct sockaddr_in"/>
			<field name="media" type="GstRTSPMedia*"/>
			<field name="pool" type="GstRTSPSessionPool*"/>
			<field name="session" type="GstRTSPSession*"/>
			<field name="thread" type="GThread*"/>
		</object>
		<object name="GstRTSPMedia" parent="GObject" type-name="GstRTSPMedia" get-type="gst_rtsp_media_get_type">
			<method name="get_stream" symbol="gst_rtsp_media_get_stream">
				<return-type type="GstRTSPMediaStream*"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="idx" type="guint"/>
				</parameters>
			</method>
			<method name="n_streams" symbol="gst_rtsp_media_n_streams">
				<return-type type="guint"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_media_new">
				<return-type type="GstRTSPMedia*"/>
				<parameters>
					<parameter name="name" type="gchar*"/>
				</parameters>
			</constructor>
			<property name="location" type="char*" readable="1" writable="1" construct="0" construct-only="1"/>
			<property name="url" type="GstRTSPUrl*" readable="1" writable="1" construct="0" construct-only="1"/>
			<field name="location" type="gchar*"/>
			<field name="url" type="GstRTSPUrl*"/>
			<field name="prepared" type="gboolean"/>
			<field name="streams" type="GArray*"/>
		</object>
		<object name="GstRTSPServer" parent="GstObject" type-name="GstRTSPServer" get-type="gst_rtsp_server_get_type">
			<method name="attach" symbol="gst_rtsp_server_attach">
				<return-type type="guint"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="context" type="GMainContext*"/>
				</parameters>
			</method>
			<method name="prepare_media" symbol="gst_rtsp_server_prepare_media">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="bin" type="GstBin*"/>
				</parameters>
			</method>
			<property name="port" type="gint" readable="1" writable="1" construct="0" construct-only="1"/>
			<vfunc name="prepare_media">
				<return-type type="GstElement*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="bin" type="GstBin*"/>
				</parameters>
			</vfunc>
			<field name="server_port" type="int"/>
			<field name="host" type="gchar*"/>
			<field name="server_sin" type="struct sockaddr_in"/>
			<field name="server_sock" type="GstPollFD"/>
			<field name="io_channel" type="GIOChannel*"/>
			<field name="io_watch" type="GSource*"/>
			<field name="pool" type="GstRTSPSessionPool*"/>
		</object>
		<object name="GstRTSPSession" parent="GObject" type-name="GstRTSPSession" get-type="gst_rtsp_session_get_type">
			<method name="get_media" symbol="gst_rtsp_session_get_media">
				<return-type type="GstRTSPSessionMedia*"/>
				<parameters>
					<parameter name="sess" type="GstRTSPSession*"/>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<method name="get_stream" symbol="gst_rtsp_session_get_stream">
				<return-type type="GstRTSPSessionStream*"/>
				<parameters>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
					<parameter name="idx" type="guint"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_session_new">
				<return-type type="GstRTSPSession*"/>
				<parameters>
					<parameter name="sessionid" type="gchar*"/>
				</parameters>
			</constructor>
			<field name="sessionid" type="gchar*"/>
			<field name="medias" type="GList*"/>
		</object>
		<object name="GstRTSPSessionPool" parent="GObject" type-name="GstRTSPSessionPool" get-type="gst_rtsp_session_pool_get_type">
			<method name="create" symbol="gst_rtsp_session_pool_create">
				<return-type type="GstRTSPSession*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<method name="find" symbol="gst_rtsp_session_pool_find">
				<return-type type="GstRTSPSession*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
					<parameter name="sessionid" type="gchar*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_session_pool_new">
				<return-type type="GstRTSPSessionPool*"/>
			</constructor>
			<method name="remove" symbol="gst_rtsp_session_pool_remove">
				<return-type type="void"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
					<parameter name="sess" type="GstRTSPSession*"/>
				</parameters>
			</method>
			<field name="lock" type="GMutex*"/>
			<field name="sessions" type="GHashTable*"/>
		</object>
	</namespace>
</api>
