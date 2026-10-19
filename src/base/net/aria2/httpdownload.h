/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2006  Christophe Dumez <chris@qbittorrent.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <QObject>
#include <QString>

#include "base/path.h"

namespace Net::Aria2
{
    class Downloader;

    // One HTTP(S) file download task driven by the aria2 engine.
    // These tasks are surfaced in the existing transfer list UI.
    class HttpDownload final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(HttpDownload)

    public:
        enum class State
        {
            Downloading,
            Paused,
            Completed,
            Failed
        };
        Q_ENUM(State)

        const QString &id() const { return m_id; }
        const QString &url() const { return m_url; }
        const Path &savePath() const { return m_savePath; }
        State state() const { return m_state; }
        const QString &errorString() const { return m_errorString; }

        qint64 downloaded() const;
        qint64 total() const;
        qint64 speed() const;

        void pause();
        void resume();

        // Polling hook used by HttpDownloadManager to refresh UI progress.
        void notifyProgress();

    signals:
        void stateChanged(Net::Aria2::HttpDownload *download);

    private:
        friend class HttpDownloadManager;
        HttpDownload(QString id, QString url, Path savePath, Downloader *downloader, QObject *parent = nullptr);

        void setState(State state, const QString &errorString = {});

        QString m_id;
        QString m_url;
        Path m_savePath;
        State m_state = State::Downloading;
        QString m_errorString;
        Downloader *m_downloader = nullptr;
    };
}
