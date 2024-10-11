// SPDX-License-Identifier: BSD-2-Clause
//
// Copyright (C) 2015, Matthew Waters <matthew@centricular.com>
// Copyright (C) 2021, Dmitry Shusharin <pmdvsh@gmail.com>
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// a) Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// b) Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in
//    the documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
// THE POSSIBILITY OF SUCH DAMAGE.

#ifndef VIDEOITEM_H
#define VIDEOITEM_H

#include <QQuickItem>

struct VideoItemPrivate;

class VideoItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QRect rect READ rect NOTIFY rectChanged)
    Q_PROPERTY(QSize resolution READ resolution NOTIFY resolutionChanged)

public:
    enum State {
        STATE_VOID_PENDING = 0,
        STATE_NULL = 1,
        STATE_READY = 2,
        STATE_PAUSED = 3,
        STATE_PLAYING = 4
    };
    Q_ENUM(State);

    explicit VideoItem(QQuickItem *parent = nullptr);
    ~VideoItem();

    bool hasVideo() const;

    QString source() const;
    void setSource(const QString &source);

    State state() const;
    void setState(State state);

    QRect rect() const;

    QSize resolution() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();

signals:
    void hasVideoChanged(bool hasVideo);
    void stateChanged(VideoItem::State state);
    void sourceChanged(const QString &source);
    void rectChanged(const QRect &rect);
    void resolutionChanged(const QSize &resolution);

    void errorOccurred(const QString &error);

protected:
    void componentComplete() override;
    void releaseResources() override;

private:
    void updateRect();
    void setRect(const QRect &rect);
    void setResolution(const QSize &resolution);

private:
    QSharedPointer<VideoItemPrivate> _priv;
};

#endif // VIDEOITEM_H
