<?xml version="1.0"?>
<api version="1.0">
	<namespace name="Gst">
		<struct name="GstRTSPMediaStream">
			<field name="media" type="GstRTSPMedia*"/>
			<field name="idx" type="guint"/>
			<field name="element" type="GstElement*"/>
			<field name="srcpad" type="GstPad*"/>
			<field name="payloader" type="GstElement*"/>
			<field name="caps_sig" type="gulong"/>
			<field name="caps" type="GstCaps*"/>
		</struct>
		<struct name="GstRTSPSessionMedia">
			<method name="get_stream" symbol="gst_rtsp_session_media_get_stream">
				<return-type type="GstRTSPSessionStream*"/>
				<parameters>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
					<parameter name="idx" type="guint"/>
				</parameters>
			</method>
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
			<field name="factory" type="GstRTSPMediaFactory*"/>
			<field name="pipeline" type="GstElement*"/>
			<field name="media" type="GstRTSPMedia*"/>
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
					<parameter name="channel" type="GIOChannel*"/>
				</parameters>
			</method>
			<method name="get_media_mapping" symbol="gst_rtsp_client_get_media_mapping">
				<return-type type="GstRTSPMediaMapping*"/>
				<parameters>
					<parameter name="client" type="GstRTSPClient*"/>
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
			</constructor>
			<method name="set_media_mapping" symbol="gst_rtsp_client_set_media_mapping">
				<return-type type="void"/>
				<parameters>
					<parameter name="client" type="GstRTSPClient*"/>
					<parameter name="mapping" type="GstRTSPMediaMapping*"/>
				</parameters>
			</method>
			<method name="set_session_pool" symbol="gst_rtsp_client_set_session_pool">
				<return-type type="void"/>
				<parameters>
					<parameter name="client" type="GstRTSPClient*"/>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<field name="connection" type="GstRTSPConnection*"/>
			<field name="address" type="struct sockaddr_in"/>
			<field name="thread" type="GThread*"/>
			<field name="pool" type="GstRTSPSessionPool*"/>
			<field name="factory" type="GstRTSPMediaFactory*"/>
			<field name="mapping" type="GstRTSPMediaMapping*"/>
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
			<field name="element" type="GstElement*"/>
			<field name="streams" type="GArray*"/>
		</object>
		<object name="GstRTSPMediaFactory" parent="GObject" type-name="GstRTSPMediaFactory" get-type="gst_rtsp_media_factory_get_type">
			<method name="construct" symbol="gst_rtsp_media_factory_construct">
				<return-type type="GstRTSPMedia*"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
				</parameters>
			</method>
			<method name="get_launch" symbol="gst_rtsp_media_factory_get_launch">
				<return-type type="gchar*"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
				</parameters>
			</method>
			<method name="is_shared" symbol="gst_rtsp_media_factory_is_shared">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_media_factory_new">
				<return-type type="GstRTSPMediaFactory*"/>
			</constructor>
			<method name="set_launch" symbol="gst_rtsp_media_factory_set_launch">
				<return-type type="void"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="launch" type="gchar*"/>
				</parameters>
			</method>
			<method name="set_shared" symbol="gst_rtsp_media_factory_set_shared">
				<return-type type="void"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="shared" type="gboolean"/>
				</parameters>
			</method>
			<property name="launch" type="char*" readable="1" writable="1" construct="0" construct-only="0"/>
			<vfunc name="construct">
				<return-type type="GstRTSPMedia*"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
				</parameters>
			</vfunc>
			<vfunc name="get_element">
				<return-type type="GstElement*"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
				</parameters>
			</vfunc>
			<field name="launch" type="gchar*"/>
		</object>
		<object name="GstRTSPMediaMapping" parent="GObject" type-name="GstRTSPMediaMapping" get-type="gst_rtsp_media_mapping_get_type">
			<method name="add_factory" symbol="gst_rtsp_media_mapping_add_factory">
				<return-type type="void"/>
				<parameters>
					<parameter name="mapping" type="GstRTSPMediaMapping*"/>
					<parameter name="path" type="gchar*"/>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
				</parameters>
			</method>
			<method name="find_factory" symbol="gst_rtsp_media_mapping_find_factory">
				<return-type type="GstRTSPMediaFactory*"/>
				<parameters>
					<parameter name="mapping" type="GstRTSPMediaMapping*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_media_mapping_new">
				<return-type type="GstRTSPMediaMapping*"/>
			</constructor>
			<method name="remove_factory" symbol="gst_rtsp_media_mapping_remove_factory">
				<return-type type="void"/>
				<parameters>
					<parameter name="mapping" type="GstRTSPMediaMapping*"/>
					<parameter name="path" type="gchar*"/>
				</parameters>
			</method>
			<vfunc name="find_media">
				<return-type type="GstRTSPMediaFactory*"/>
				<parameters>
					<parameter name="mapping" type="GstRTSPMediaMapping*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
				</parameters>
			</vfunc>
			<field name="mappings" type="GHashTable*"/>
		</object>
		<object name="GstRTSPServer" parent="GObject" type-name="GstRTSPServer" get-type="gst_rtsp_server_get_type">
			<method name="attach" symbol="gst_rtsp_server_attach">
				<return-type type="guint"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="context" type="GMainContext*"/>
				</parameters>
			</method>
			<method name="create_watch" symbol="gst_rtsp_server_create_watch">
				<return-type type="GSource*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</method>
			<method name="get_backlog" symbol="gst_rtsp_server_get_backlog">
				<return-type type="gint"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</method>
			<method name="get_io_channel" symbol="gst_rtsp_server_get_io_channel">
				<return-type type="GIOChannel*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</method>
			<method name="get_media_mapping" symbol="gst_rtsp_server_get_media_mapping">
				<return-type type="GstRTSPMediaMapping*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</method>
			<method name="get_port" symbol="gst_rtsp_server_get_port">
				<return-type type="gint"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</method>
			<method name="get_session_pool" symbol="gst_rtsp_server_get_session_pool">
				<return-type type="GstRTSPSessionPool*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</method>
			<method name="io_func" symbol="gst_rtsp_server_io_func">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="channel" type="GIOChannel*"/>
					<parameter name="condition" type="GIOCondition"/>
					<parameter name="server" type="GstRTSPServer*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_server_new">
				<return-type type="GstRTSPServer*"/>
			</constructor>
			<method name="set_backlog" symbol="gst_rtsp_server_set_backlog">
				<return-type type="void"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="backlog" type="gint"/>
				</parameters>
			</method>
			<method name="set_media_mapping" symbol="gst_rtsp_server_set_media_mapping">
				<return-type type="void"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="mapping" type="GstRTSPMediaMapping*"/>
				</parameters>
			</method>
			<method name="set_port" symbol="gst_rtsp_server_set_port">
				<return-type type="void"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="port" type="gint"/>
				</parameters>
			</method>
			<method name="set_session_pool" symbol="gst_rtsp_server_set_session_pool">
				<return-type type="void"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<property name="backlog" type="gint" readable="1" writable="1" construct="0" construct-only="0"/>
			<property name="mapping" type="GstRTSPMediaMapping*" readable="1" writable="1" construct="0" construct-only="0"/>
			<property name="pool" type="GstRTSPSessionPool*" readable="1" writable="1" construct="0" construct-only="0"/>
			<property name="port" type="gint" readable="1" writable="1" construct="0" construct-only="0"/>
			<vfunc name="accept_client">
				<return-type type="GstRTSPClient*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="channel" type="GIOChannel*"/>
				</parameters>
			</vfunc>
			<field name="server_port" type="gint"/>
			<field name="backlog" type="gint"/>
			<field name="host" type="gchar*"/>
			<field name="server_sin" type="struct sockaddr_in"/>
			<field name="server_sock" type="GstPollFD"/>
			<field name="io_channel" type="GIOChannel*"/>
			<field name="io_watch" type="GSource*"/>
			<field name="pool" type="GstRTSPSessionPool*"/>
			<field name="mapping" type="GstRTSPMediaMapping*"/>
		</object>
		<object name="GstRTSPSession" parent="GObject" type-name="GstRTSPSession" get-type="gst_rtsp_session_get_type">
			<method name="get_media" symbol="gst_rtsp_session_get_media">
				<return-type type="GstRTSPSessionMedia*"/>
				<parameters>
					<parameter name="sess" type="GstRTSPSession*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
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
			<vfunc name="create_session_id">
				<return-type type="gchar*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</vfunc>
			<field name="lock" type="GMutex*"/>
			<field name="sessions" type="GHashTable*"/>
		</object>
	</namespace>
</api>
