/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2009 Andrey Nechypurenko <andreynech@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "pipeline.h"


Pipeline::Pipeline(GstGLContext *context,
                   const QString &videoLocation,
                   QObject *parent)
  : QObject(parent),
    m_videoLocation(videoLocation),
    m_loop(NULL),
    m_bus(NULL),
    m_pipeline(NULL)
{
    this->context = context;
    this->configure();
}

Pipeline::~Pipeline()
{
}

void
Pipeline::configure()
{
    gst_init (NULL, NULL);

#ifdef Q_WS_WIN
    m_loop = g_main_loop_new (NULL, FALSE);
#endif

    if(m_videoLocation.isEmpty())
    {
        qDebug("No video file specified. Using video test source.");
        m_pipeline =
            GST_PIPELINE (gst_parse_launch
                      ("videotestsrc ! "
                       "video/x-raw, width=640, height=480, "
                       "framerate=(fraction)30/1 ! "
                       "gleffects effect=5 ! fakesink sync=1",
                       NULL));
    }
    else
    {
        QByteArray ba = m_videoLocation.toLocal8Bit();
        qDebug("Loading video: %s", ba.data());
        gchar *pipeline = g_strdup_printf ("filesrc location='%s' ! "
                               "decodebin ! gleffects effect=5 ! "
                               "fakesink sync=1", ba.data());
        m_pipeline = GST_PIPELINE (gst_parse_launch (pipeline, NULL));
        g_free (pipeline);
    }

    m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    gst_bus_add_watch(m_bus, (GstBusFunc) bus_call, this);
    gst_object_unref(m_bus);

    /* Retrieve the last gl element */
    GstElement *gl_element = gst_bin_get_by_name(GST_BIN(m_pipeline), "gleffects0");
    if(!gl_element)
    {
        qDebug ("gl element could not be found");
        return;
    }
    g_object_set(G_OBJECT (gl_element), "other-context",
               this->context, NULL);
    gst_object_unref(gl_element);

    gst_element_set_state(GST_ELEMENT(this->m_pipeline), GST_STATE_PAUSED);
    GstState state = GST_STATE_PAUSED;
    if(gst_element_get_state(GST_ELEMENT(this->m_pipeline),
            &state, NULL, GST_CLOCK_TIME_NONE)
            != GST_STATE_CHANGE_SUCCESS)
    {
        qDebug("failed to pause pipeline");
        return;
    }
}

void
Pipeline::start()
{
    // set a callback to retrieve the gst gl textures
    GstElement *fakesink = gst_bin_get_by_name(GST_BIN(this->m_pipeline),
        "fakesink0");
    g_object_set(G_OBJECT (fakesink), "signal-handoffs", TRUE, NULL);
    g_signal_connect(fakesink, "handoff", G_CALLBACK (on_gst_buffer), this);
    gst_object_unref(fakesink);

    GstStateChangeReturn ret =
    gst_element_set_state(GST_ELEMENT(this->m_pipeline), GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        qDebug("Failed to start up pipeline!");

        /* check if there is an error message with details on the bus */
        GstMessage* msg = gst_bus_poll(this->m_bus, GST_MESSAGE_ERROR, 0);
        if (msg)
        {
            GError *err = NULL;
            gst_message_parse_error (msg, &err, NULL);
            qDebug ("ERROR: %s", err->message);
            g_error_free (err);
            gst_message_unref (msg);
        }
        return;
    }

#ifdef Q_WS_WIN
    g_main_loop_run(m_loop);
#endif
}

/* fakesink handoff callback */
void
Pipeline::on_gst_buffer(GstElement * element,
                        GstBuffer * buf,
                        GstPad * pad,
                        Pipeline* p)
{
    Q_UNUSED(pad)
    Q_UNUSED(element)

    /* ref then push buffer to use it in qt */
    gst_buffer_ref(buf);
    p->queue_input_buf.put(buf);

    if (p->queue_input_buf.size() > 3)
        p->notifyNewFrame();

    /* pop then unref buffer we have finished to use in qt */
    if (p->queue_output_buf.size() > 3)
    {
        GstBuffer *buf_old = (p->queue_output_buf.get());
        if (buf_old)
            gst_buffer_unref(buf_old);
    }
}

void
Pipeline::stop()
{
#ifdef Q_WS_WIN
    g_main_loop_quit(m_loop);
#else
    emit stopRequested();
#endif
}

void
Pipeline::unconfigure()
{
    gst_element_set_state(GST_ELEMENT(this->m_pipeline), GST_STATE_NULL);

    GstBuffer *buf;
    while(this->queue_input_buf.size())
    {
        buf = (GstBuffer*)(this->queue_input_buf.get());
        gst_buffer_unref(buf);
    }
    while(this->queue_output_buf.size())
    {
        buf = (GstBuffer*)(this->queue_output_buf.get());
        gst_buffer_unref(buf);
    }

    gst_object_unref(m_pipeline);
}

gboolean
Pipeline::bus_call(GstBus *bus, GstMessage *msg, Pipeline* p)
{
  Q_UNUSED(bus)

    switch(GST_MESSAGE_TYPE(msg))
    {
        case GST_MESSAGE_EOS:
            qDebug("End-of-stream received. Stopping.");
            p->stop();
        break;

        case GST_MESSAGE_ERROR:
        {
            gchar *debug = NULL;
            GError *err = NULL;
            gst_message_parse_error(msg, &err, &debug);
            qDebug("Error: %s", err->message);
            g_error_free (err);
            if(debug)
            {
            qDebug("Debug deails: %s", debug);
            g_free(debug);
            }
            p->stop();
            break;
        }

        default:
            break;
    }

    return TRUE;
}
