# Using appsink/appsrc in Qt

## Goal

For those times when you need to stream data into or out of GStreamer
through your application, GStreamer includes two helpful elements:

  - `appsink` - Allows applications to easily extract data from a
    GStreamer pipeline
  - `appsrc` - Allows applications to easily stream data into a
    GStreamer pipeline

This tutorial will demonstrate how to use both of them by constructing a
pipeline to decode an audio file, stream it into an application's code,
then stream it back into your audio output device. All this, using
QtGStreamer.

## Steps

First, the files. These are also available in the
`examples/appsink-src` directory of the QGstreamer SDK.

**CMakeLists.txt**

```
project(qtgst-example-appsink-src)
find_package(QtGStreamer REQUIRED)
find_package(Qt4 REQUIRED)
include_directories(${QTGSTREAMER_INCLUDES} ${QT_QTCORE_INCLUDE_DIRS})
add_definitions(${QTGSTREAMER_DEFINITIONS})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${QTGSTREAMER_FLAGS}")
add_executable(appsink-src main.cpp)
target_link_libraries(appsink-src ${QTGSTREAMER_UTILS_LIBRARIES} ${QT_QTCORE_LIBRARIES})
```

**main.cpp**

``` c
#include <iostream>
#include <QtCore/QCoreApplication>
#include <QGlib/Error>
#include <QGlib/Connect>
#include <QGst/Init>
#include <QGst/Bus>
#include <QGst/Pipeline>
#include <QGst/Parse>
#include <QGst/Message>
#include <QGst/Utils/ApplicationSink>
#include <QGst/Utils/ApplicationSource>

class MySink : public QGst::Utils::ApplicationSink
{
public:
    MySink(QGst::Utils::ApplicationSource *src)
        : QGst::Utils::ApplicationSink(), m_src(src) {}
protected:
    virtual void eos()
    {
        m_src->endOfStream();
    }
    virtual QGst::FlowReturn newBuffer()
    {
        m_src->pushBuffer(pullBuffer());
        return QGst::FlowOk;
    }
private:
    QGst::Utils::ApplicationSource *m_src;
};

class Player : public QCoreApplication
{
public:
    Player(int argc, char **argv);
    ~Player();
private:
    void onBusMessage(const QGst::MessagePtr & message);
private:
    QGst::Utils::ApplicationSource m_src;
    MySink m_sink;
    QGst::PipelinePtr pipeline1;
    QGst::PipelinePtr pipeline2;
};
Player::Player(int argc, char **argv)
    : QCoreApplication(argc, argv), m_sink(&m_src)
{
    QGst::init(&argc, &argv);
    if (argc <= 1) {
        std::cerr << "Usage: " << argv[0] << " <audio_file>" << std::endl;
        std::exit(1);
    }
    const char *caps = "audio/x-raw-int,channels=1,rate=8000,"
                       "signed=(boolean)true,width=16,depth=16,endianness=1234";
    /* source pipeline */
    QString pipe1Descr = QString("filesrc location=\"%1\" ! "
                                 "decodebin2 ! "
                                 "audioconvert ! "
                                 "audioresample ! "
                                 "appsink name=\"mysink\" caps=\"%2\"").arg(argv[1], caps);
    pipeline1 = QGst::Parse::launch(pipe1Descr).dynamicCast<QGst::Pipeline>();
    m_sink.setElement(pipeline1->getElementByName("mysink"));
    QGlib::connect(pipeline1->bus(), "message::error", this, &Player::onBusMessage);
    pipeline1->bus()->addSignalWatch();
    /* sink pipeline */
    QString pipe2Descr = QString("appsrc name=\"mysrc\" caps=\"%1\" ! autoaudiosink").arg(caps);
    pipeline2 = QGst::Parse::launch(pipe2Descr).dynamicCast<QGst::Pipeline>();
    m_src.setElement(pipeline2->getElementByName("mysrc"));
    QGlib::connect(pipeline2->bus(), "message", this, &Player::onBusMessage);
    pipeline2->bus()->addSignalWatch();
    /* start playing */
    pipeline1->setState(QGst::StatePlaying);
    pipeline2->setState(QGst::StatePlaying);
}
Player::~Player()
{
    pipeline1->setState(QGst::StateNull);
    pipeline2->setState(QGst::StateNull);
}
void Player::onBusMessage(const QGst::MessagePtr & message)
{
    switch (message->type()) {
    case QGst::MessageEos:
        quit();
        break;
    case QGst::MessageError:
        qCritical() << message.staticCast<QGst::ErrorMessage>()->error();
        break;
    default:
        break;
    }
}

int main(int argc, char **argv)
{
    Player p(argc, argv);
    return p.exec();
}
```

### Walkthrough

As this is a very simple example, most of the action happens in the
`Player`'s constructor. First, GStreamer is initialized through
`QGst::init()`:

**GStreamer Initialization**

``` c
    QGst::init(&argc, &argv);
```

Now we can construct the first half of the pipeline:

**Pipeline Setup**

``` c
    const char *caps = "audio/x-raw-int,channels=1,rate=8000,"
                       "signed=(boolean)true,width=16,depth=16,endianness=1234";
 
    /* source pipeline */
    QString pipe1Descr = QString("filesrc location=\"%1\" ! "
                                 "decodebin2 ! "
                                 "audioconvert ! "
                                 "audioresample ! "
                                 "appsink name=\"mysink\" caps=\"%2\"").arg(argv[1], caps);
    pipeline1 = QGst::Parse::launch(pipe1Descr).dynamicCast<QGst::Pipeline>();
    m_sink.setElement(pipeline1->getElementByName("mysink"));
    QGlib::connect(pipeline1->bus(), "message::error", this, &Player::onBusMessage);
    pipeline1->bus()->addSignalWatch();
```

`QGst::Parse::launch()` parses the text description of a pipeline and
returns a `QGst::PipelinePtr`. In this case, the pipeline is composed
of:

  - A `filesrc` element to read the file
  - `decodebin2` to automatically examine the stream and pick the right
    decoder(s)
  - `audioconvert` and `audioresample` to convert the output of the
    `decodebin2` into the caps specified for the `appsink`
  - An `appsink` element with specific caps

Next, we tell our `MySink` class (which is a subclass
of `QGst::Utils::ApplicationSink`) what `appsink` element to use.

The second half of the pipeline is created similarly:

**Second Pipeline**

``` c
    /* sink pipeline */
    QString pipe2Descr = QString("appsrc name=\"mysrc\" caps=\"%1\" ! autoaudiosink").arg(caps);
    pipeline2 = QGst::Parse::launch(pipe2Descr).dynamicCast<QGst::Pipeline>();
    m_src.setElement(pipeline2->getElementByName("mysrc"));
    QGlib::connect(pipeline2->bus(), "message", this, &Player::onBusMessage);
    pipeline2->bus()->addSignalWatch();
```

Finally, the pipeline is started:

**Starting the pipeline**

``` c
 /* start playing */
    pipeline1->setState(QGst::StatePlaying);
    pipeline2->setState(QGst::StatePlaying);
```

Once the pipelines are started, the first one begins pushing buffers
into the `appsink` element. Our `MySink` class implements the
`newBuffer()` method, which is called by QGStreamer when a new buffer is
ready for processing:

**MySink::newBuffer()**

``` c
    virtual QGst::FlowReturn newBuffer()
    {
        m_src->pushBuffer(pullBuffer());
        return QGst::FlowOk;
    }
```

Our implementation takes the new buffer and pushes it into the
`appsrc` element, which got assigned in the `Player` constructor:

**Player::Player()**

``` c
Player::Player(int argc, char **argv)
    : QCoreApplication(argc, argv), m_sink(&m_src)
```

From there, buffers flow into the `autoaudiosink` element, which
automatically figures out a way to send it to your speakers.

## Conclusion

You should now have an understanding of how to push and pull arbitrary
data into and out of a GStreamer pipeline.

It has been a pleasure having you here, and see you soon\!
