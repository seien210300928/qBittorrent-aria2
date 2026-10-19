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

/*
 * Multi-connection HTTP download engine adapted from aria2-next
 * (https://github.com/AnInsomniacy/aria2-next),
 * Copyright (C) 2026 aria2-next contributors. GPL-2.0-or-later.
 */

#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QNetworkReply>
#include <QObject>
#include <QSet>

#include "base/net/aria2/segmentplanner.h"
#include "base/path.h"

class QFile;
class QNetworkAccessManager;
class QTimer;

namespace Net::Aria2
{
    // Downloads one URL over multiple Range connections, planning segments with
    // SegmentPlanner. Supports resuming from a sidecar ".qba" control file.
    class Downloader final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(Downloader)

    public:
        enum class State
        {
            Idle,
            Probing,
            Downloading,
            Finished,
            Failed
        };

        Downloader(const QString &url, const Path &destFilePath, int maxConnections, QObject *parent = nullptr);
        ~Downloader() override;

        void start();
        void cancel();

    signals:
        void finished(const QString &url, const Path &filePath);
        void failed(const QString &url, const QString &errorString);
        // The downloaded content is actually a .torrent file.
        void torrentFileDetected(const QString &url, const Path &filePath);

    private:
        enum class Mode
        {
            MultiConnection,
            SingleConnection
        };

        void probe();
        void probeViaHead();
        void probeViaGet();
        void onProbeReplyFinished();
        void startWorkers();
        void assignWorker(int id);
        void sendWorkerRequest(int id, int64_t begin, int64_t end);
        void startSingleConnection();
        void onReplyReadyRead(QNetworkReply *reply);
        void onReplyFinished(QNetworkReply *reply);
        void onReplyError(QNetworkReply *reply, QNetworkReply::NetworkError error);
        void scheduleWorkerRetry(int id);
        void resetWorker(int id);
        void sampleSpeeds();
        void saveControlFile();
        bool tryResume();
        void finalize();
        void fail(const QString &errorString);

        bool isTorrentUrl() const;
        bool isRetryableError(QNetworkReply::NetworkError error) const;

        const QString m_url;
        const Path m_destFilePath;
        const int m_maxConnections;
        Mode m_mode = Mode::MultiConnection;
        State m_state = State::Idle;
        bool m_cancelled = false;
        bool m_controlDirty = false;

        QNetworkAccessManager *m_nam = nullptr;
        QNetworkReply *m_probeReply = nullptr;
        SegmentPlanner m_planner;
        QFile *m_file = nullptr;
        QHash<int, QNetworkReply *> m_replies;       // worker id -> reply
        QHash<QNetworkReply *, int> m_replyIds;      // reply -> worker id
        QHash<int, qint64> m_workerBytes;            // worker id -> bytes received
        QSet<int> m_activeWorkers;                   // workers with an in-flight request
        int m_nextWorkerId = 0;
        int m_networkErrorRetries = 0;
        QTimer *m_speedTimer = nullptr;
        QTimer *m_controlTimer = nullptr;
        QElapsedTimer m_startupTimer;
        bool m_startupMeasured = false;
    };
}
