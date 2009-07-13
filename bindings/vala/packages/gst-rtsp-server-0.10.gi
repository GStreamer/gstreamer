<?xml version="1.0"?>
<api version="1.0">
	<namespace name="Gst">
		<function name="rtsp_params_get" symbol="gst_rtsp_params_get">
			<return-type type="GstRTSPResult"/>
			<parameters>
				<parameter name="client" type="GstRTSPClient*"/>
				<parameter name="uri" type="GstRTSPUrl*"/>
				<parameter name="session" type="GstRTSPSession*"/>
				<parameter name="request" type="GstRTSPMessage*"/>
				<parameter name="response" type="GstRTSPMessage*"/>
			</parameters>
		</function>
		<function name="rtsp_params_set" symbol="gst_rtsp_params_set">
			<return-type type="GstRTSPResult"/>
			<parameters>
				<parameter name="client" type="GstRTSPClient*"/>
				<parameter name="uri" type="GstRTSPUrl*"/>
				<parameter name="session" type="GstRTSPSession*"/>
				<parameter name="request" type="GstRTSPMessage*"/>
				<parameter name="response" type="GstRTSPMessage*"/>
			</parameters>
		</function>
		<function name="rtsp_sdp_from_media" symbol="gst_rtsp_sdp_from_media">
			<return-type type="GstSDPMessage*"/>
			<parameters>
				<parameter name="media" type="GstRTSPMedia*"/>
			</parameters>
		</function>
		<callback name="GstRTSPKeepAliveFunc">
			<return-type type="void"/>
			<parameters>
				<parameter name="user_data" type="gpointer"/>
			</parameters>
		</callback>
		<callback name="GstRTSPSendFunc">
			<return-type type="gboolean"/>
			<parameters>
				<parameter name="buffer" type="GstBuffer*"/>
				<parameter name="channel" type="guint8"/>
				<parameter name="user_data" type="gpointer"/>
			</parameters>
		</callback>
		<callback name="GstRTSPSessionFilterFunc">
			<return-type type="GstRTSPFilterResult"/>
			<parameters>
				<parameter name="pool" type="GstRTSPSessionPool*"/>
				<parameter name="session" type="GstRTSPSession*"/>
				<parameter name="user_data" type="gpointer"/>
			</parameters>
		</callback>
		<callback name="GstRTSPSessionPoolFunc">
			<return-type type="gboolean"/>
			<parameters>
				<parameter name="pool" type="GstRTSPSessionPool*"/>
				<parameter name="user_data" type="gpointer"/>
			</parameters>
		</callback>
		<struct name="GstRTSPMediaStream">
			<method name="rtcp" symbol="gst_rtsp_media_stream_rtcp">
				<return-type type="GstFlowReturn"/>
				<parameters>
					<parameter name="stream" type="GstRTSPMediaStream*"/>
					<parameter name="buffer" type="GstBuffer*"/>
				</parameters>
			</method>
			<method name="rtp" symbol="gst_rtsp_media_stream_rtp">
				<return-type type="GstFlowReturn"/>
				<parameters>
					<parameter name="stream" type="GstRTSPMediaStream*"/>
					<parameter name="buffer" type="GstBuffer*"/>
				</parameters>
			</method>
			<field name="srcpad" type="GstPad*"/>
			<field name="payloader" type="GstElement*"/>
			<field name="prepared" type="gboolean"/>
			<field name="recv_rtcp_sink" type="GstPad*"/>
			<field name="recv_rtp_sink" type="GstPad*"/>
			<field name="send_rtp_sink" type="GstPad*"/>
			<field name="send_rtp_src" type="GstPad*"/>
			<field name="send_rtcp_src" type="GstPad*"/>
			<field name="session" type="GObject*"/>
			<field name="udpsrc" type="GstElement*[]"/>
			<field name="udpsink" type="GstElement*[]"/>
			<field name="appsrc" type="GstElement*[]"/>
			<field name="appsink" type="GstElement*[]"/>
			<field name="tee" type="GstElement*[]"/>
			<field name="selector" type="GstElement*[]"/>
			<field name="server_port" type="GstRTSPRange"/>
			<field name="caps_sig" type="gulong"/>
			<field name="caps" type="GstCaps*"/>
			<field name="transports" type="GList*"/>
		</struct>
		<struct name="GstRTSPMediaTrans">
			<field name="idx" type="guint"/>
			<field name="send_rtp" type="GstRTSPSendFunc"/>
			<field name="send_rtcp" type="GstRTSPSendFunc"/>
			<field name="user_data" type="gpointer"/>
			<field name="notify" type="GDestroyNotify"/>
			<field name="keep_alive" type="GstRTSPKeepAliveFunc"/>
			<field name="ka_user_data" type="gpointer"/>
			<field name="ka_notify" type="GDestroyNotify"/>
			<field name="active" type="gboolean"/>
			<field name="timeout" type="gboolean"/>
			<field name="transport" type="GstRTSPTransport*"/>
			<field name="rtpsource" type="GObject*"/>
		</struct>
		<struct name="GstRTSPSessionMedia">
			<method name="get_stream" symbol="gst_rtsp_session_media_get_stream">
				<return-type type="GstRTSPSessionStream*"/>
				<parameters>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
					<parameter name="idx" type="guint"/>
				</parameters>
			</method>
			<method name="set_state" symbol="gst_rtsp_session_media_set_state">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
					<parameter name="state" type="GstState"/>
				</parameters>
			</method>
			<field name="url" type="GstRTSPUrl*"/>
			<field name="media" type="GstRTSPMedia*"/>
			<field name="state" type="GstRTSPState"/>
			<field name="streams" type="GArray*"/>
		</struct>
		<struct name="GstRTSPSessionStream">
			<method name="set_callbacks" symbol="gst_rtsp_session_stream_set_callbacks">
				<return-type type="void"/>
				<parameters>
					<parameter name="stream" type="GstRTSPSessionStream*"/>
					<parameter name="send_rtp" type="GstRTSPSendFunc"/>
					<parameter name="send_rtcp" type="GstRTSPSendFunc"/>
					<parameter name="user_data" type="gpointer"/>
					<parameter name="notify" type="GDestroyNotify"/>
				</parameters>
			</method>
			<method name="set_keepalive" symbol="gst_rtsp_session_stream_set_keepalive">
				<return-type type="void"/>
				<parameters>
					<parameter name="stream" type="GstRTSPSessionStream*"/>
					<parameter name="keep_alive" type="GstRTSPKeepAliveFunc"/>
					<parameter name="user_data" type="gpointer"/>
					<parameter name="notify" type="GDestroyNotify"/>
				</parameters>
			</method>
			<method name="set_transport" symbol="gst_rtsp_session_stream_set_transport">
				<return-type type="GstRTSPTransport*"/>
				<parameters>
					<parameter name="stream" type="GstRTSPSessionStream*"/>
					<parameter name="ct" type="GstRTSPTransport*"/>
				</parameters>
			</method>
			<field name="trans" type="GstRTSPMediaTrans"/>
			<field name="media_stream" type="GstRTSPMediaStream*"/>
		</struct>
		<enum name="GstRTSPFilterResult">
			<member name="GST_RTSP_FILTER_REMOVE" value="0"/>
			<member name="GST_RTSP_FILTER_KEEP" value="1"/>
			<member name="GST_RTSP_FILTER_REF" value="2"/>
		</enum>
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
			<property name="media-mapping" type="GstRTSPMediaMapping*" readable="1" writable="1" construct="0" construct-only="0"/>
			<property name="session-pool" type="GstRTSPSessionPool*" readable="1" writable="1" construct="0" construct-only="0"/>
			<field name="connection" type="GstRTSPConnection*"/>
			<field name="watch" type="GstRTSPWatch*"/>
			<field name="watchid" type="guint"/>
			<field name="session_pool" type="GstRTSPSessionPool*"/>
			<field name="media_mapping" type="GstRTSPMediaMapping*"/>
			<field name="uri" type="GstRTSPUrl*"/>
			<field name="media" type="GstRTSPMedia*"/>
			<field name="streams" type="GList*"/>
			<field name="sessions" type="GList*"/>
		</object>
		<object name="GstRTSPMedia" parent="GObject" type-name="GstRTSPMedia" get-type="gst_rtsp_media_get_type">
			<method name="get_stream" symbol="gst_rtsp_media_get_stream">
				<return-type type="GstRTSPMediaStream*"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="idx" type="guint"/>
				</parameters>
			</method>
			<method name="is_prepared" symbol="gst_rtsp_media_is_prepared">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<method name="is_reusable" symbol="gst_rtsp_media_is_reusable">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<method name="is_shared" symbol="gst_rtsp_media_is_shared">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
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
			</constructor>
			<method name="prepare" symbol="gst_rtsp_media_prepare">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<method name="remove_elements" symbol="gst_rtsp_media_remove_elements">
				<return-type type="void"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<method name="seek" symbol="gst_rtsp_media_seek">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="range" type="GstRTSPTimeRange*"/>
				</parameters>
			</method>
			<method name="set_reusable" symbol="gst_rtsp_media_set_reusable">
				<return-type type="void"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="reusable" type="gboolean"/>
				</parameters>
			</method>
			<method name="set_shared" symbol="gst_rtsp_media_set_shared">
				<return-type type="void"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="shared" type="gboolean"/>
				</parameters>
			</method>
			<method name="set_state" symbol="gst_rtsp_media_set_state">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="state" type="GstState"/>
					<parameter name="trans" type="GArray*"/>
				</parameters>
			</method>
			<method name="unprepare" symbol="gst_rtsp_media_unprepare">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<property name="reusable" type="gboolean" readable="1" writable="1" construct="0" construct-only="0"/>
			<property name="shared" type="gboolean" readable="1" writable="1" construct="0" construct-only="0"/>
			<signal name="unprepared" when="LAST">
				<return-type type="void"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</signal>
			<vfunc name="handle_message">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
					<parameter name="message" type="GstMessage*"/>
				</parameters>
			</vfunc>
			<vfunc name="unprepare">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</vfunc>
			<field name="shared" type="gboolean"/>
			<field name="reusable" type="gboolean"/>
			<field name="reused" type="gboolean"/>
			<field name="element" type="GstElement*"/>
			<field name="streams" type="GArray*"/>
			<field name="dynamic" type="GList*"/>
			<field name="prepared" type="gboolean"/>
			<field name="active" type="gint"/>
			<field name="pipeline" type="GstElement*"/>
			<field name="fakesink" type="GstElement*"/>
			<field name="source" type="GSource*"/>
			<field name="id" type="guint"/>
			<field name="is_live" type="gboolean"/>
			<field name="buffering" type="gboolean"/>
			<field name="target_state" type="GstState"/>
			<field name="rtpbin" type="GstElement*"/>
			<field name="range" type="GstRTSPTimeRange"/>
		</object>
		<object name="GstRTSPMediaFactory" parent="GObject" type-name="GstRTSPMediaFactory" get-type="gst_rtsp_media_factory_get_type">
			<method name="collect_streams" symbol="gst_rtsp_media_factory_collect_streams">
				<return-type type="void"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
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
			<property name="shared" type="gboolean" readable="1" writable="1" construct="0" construct-only="0"/>
			<vfunc name="configure">
				<return-type type="void"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</vfunc>
			<vfunc name="construct">
				<return-type type="GstRTSPMedia*"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="url" type="GstRTSPUrl*"/>
				</parameters>
			</vfunc>
			<vfunc name="create_pipeline">
				<return-type type="GstElement*"/>
				<parameters>
					<parameter name="factory" type="GstRTSPMediaFactory*"/>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</vfunc>
			<vfunc name="gen_key">
				<return-type type="gchar*"/>
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
			<field name="lock" type="GMutex*"/>
			<field name="launch" type="gchar*"/>
			<field name="shared" type="gboolean"/>
			<field name="medias_lock" type="GMutex*"/>
			<field name="medias" type="GHashTable*"/>
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
			<property name="media-mapping" type="GstRTSPMediaMapping*" readable="1" writable="1" construct="0" construct-only="0"/>
			<property name="port" type="gint" readable="1" writable="1" construct="0" construct-only="0"/>
			<property name="session-pool" type="GstRTSPSessionPool*" readable="1" writable="1" construct="0" construct-only="0"/>
			<vfunc name="accept_client">
				<return-type type="GstRTSPClient*"/>
				<parameters>
					<parameter name="server" type="GstRTSPServer*"/>
					<parameter name="channel" type="GIOChannel*"/>
				</parameters>
			</vfunc>
			<field name="port" type="gint"/>
			<field name="backlog" type="gint"/>
			<field name="host" type="gchar*"/>
			<field name="server_sin" type="struct sockaddr_in"/>
			<field name="server_sock" type="GstPollFD"/>
			<field name="io_channel" type="GIOChannel*"/>
			<field name="io_watch" type="GSource*"/>
			<field name="session_pool" type="GstRTSPSessionPool*"/>
			<field name="media_mapping" type="GstRTSPMediaMapping*"/>
		</object>
		<object name="GstRTSPSession" parent="GObject" type-name="GstRTSPSession" get-type="gst_rtsp_session_get_type">
			<method name="get_media" symbol="gst_rtsp_session_get_media">
				<return-type type="GstRTSPSessionMedia*"/>
				<parameters>
					<parameter name="sess" type="GstRTSPSession*"/>
					<parameter name="uri" type="GstRTSPUrl*"/>
				</parameters>
			</method>
			<method name="get_sessionid" symbol="gst_rtsp_session_get_sessionid">
				<return-type type="gchar*"/>
				<parameters>
					<parameter name="session" type="GstRTSPSession*"/>
				</parameters>
			</method>
			<method name="get_timeout" symbol="gst_rtsp_session_get_timeout">
				<return-type type="guint"/>
				<parameters>
					<parameter name="session" type="GstRTSPSession*"/>
				</parameters>
			</method>
			<method name="is_expired" symbol="gst_rtsp_session_is_expired">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="session" type="GstRTSPSession*"/>
					<parameter name="now" type="GTimeVal*"/>
				</parameters>
			</method>
			<method name="manage_media" symbol="gst_rtsp_session_manage_media">
				<return-type type="GstRTSPSessionMedia*"/>
				<parameters>
					<parameter name="sess" type="GstRTSPSession*"/>
					<parameter name="uri" type="GstRTSPUrl*"/>
					<parameter name="media" type="GstRTSPMedia*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_session_new">
				<return-type type="GstRTSPSession*"/>
				<parameters>
					<parameter name="sessionid" type="gchar*"/>
				</parameters>
			</constructor>
			<method name="next_timeout" symbol="gst_rtsp_session_next_timeout">
				<return-type type="gint"/>
				<parameters>
					<parameter name="session" type="GstRTSPSession*"/>
					<parameter name="now" type="GTimeVal*"/>
				</parameters>
			</method>
			<method name="release_media" symbol="gst_rtsp_session_release_media">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="sess" type="GstRTSPSession*"/>
					<parameter name="media" type="GstRTSPSessionMedia*"/>
				</parameters>
			</method>
			<method name="set_timeout" symbol="gst_rtsp_session_set_timeout">
				<return-type type="void"/>
				<parameters>
					<parameter name="session" type="GstRTSPSession*"/>
					<parameter name="timeout" type="guint"/>
				</parameters>
			</method>
			<method name="touch" symbol="gst_rtsp_session_touch">
				<return-type type="void"/>
				<parameters>
					<parameter name="session" type="GstRTSPSession*"/>
				</parameters>
			</method>
			<property name="sessionid" type="char*" readable="1" writable="1" construct="0" construct-only="1"/>
			<property name="timeout" type="guint" readable="1" writable="1" construct="0" construct-only="0"/>
			<field name="sessionid" type="gchar*"/>
			<field name="timeout" type="guint"/>
			<field name="create_time" type="GTimeVal"/>
			<field name="last_access" type="GTimeVal"/>
			<field name="medias" type="GList*"/>
		</object>
		<object name="GstRTSPSessionPool" parent="GObject" type-name="GstRTSPSessionPool" get-type="gst_rtsp_session_pool_get_type">
			<method name="cleanup" symbol="gst_rtsp_session_pool_cleanup">
				<return-type type="guint"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<method name="create" symbol="gst_rtsp_session_pool_create">
				<return-type type="GstRTSPSession*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<method name="create_watch" symbol="gst_rtsp_session_pool_create_watch">
				<return-type type="GSource*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<method name="filter" symbol="gst_rtsp_session_pool_filter">
				<return-type type="GList*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
					<parameter name="func" type="GstRTSPSessionFilterFunc"/>
					<parameter name="user_data" type="gpointer"/>
				</parameters>
			</method>
			<method name="find" symbol="gst_rtsp_session_pool_find">
				<return-type type="GstRTSPSession*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
					<parameter name="sessionid" type="gchar*"/>
				</parameters>
			</method>
			<method name="get_max_sessions" symbol="gst_rtsp_session_pool_get_max_sessions">
				<return-type type="guint"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<method name="get_n_sessions" symbol="gst_rtsp_session_pool_get_n_sessions">
				<return-type type="guint"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</method>
			<constructor name="new" symbol="gst_rtsp_session_pool_new">
				<return-type type="GstRTSPSessionPool*"/>
			</constructor>
			<method name="remove" symbol="gst_rtsp_session_pool_remove">
				<return-type type="gboolean"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
					<parameter name="sess" type="GstRTSPSession*"/>
				</parameters>
			</method>
			<method name="set_max_sessions" symbol="gst_rtsp_session_pool_set_max_sessions">
				<return-type type="void"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
					<parameter name="max" type="guint"/>
				</parameters>
			</method>
			<property name="max-sessions" type="guint" readable="1" writable="1" construct="0" construct-only="0"/>
			<vfunc name="create_session_id">
				<return-type type="gchar*"/>
				<parameters>
					<parameter name="pool" type="GstRTSPSessionPool*"/>
				</parameters>
			</vfunc>
			<field name="max_sessions" type="guint"/>
			<field name="lock" type="GMutex*"/>
			<field name="sessions" type="GHashTable*"/>
		</object>
	</namespace>
</api>
