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

#include <QHash>
#include <QObject>

#include "base/path.h"

namespace Net::Aria2
{
    class Downloader;

    // Owns the multi-connection downloaders used for the aria2 engine.
    class DownloadManager final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(DownloadManager)

    public:
        static void initInstance();
        static void freeInstance();
        static DownloadManager *instance();

        // Downloads `source` into `destFilePath` using the aria2 engine.
        // The caller is notified via downloadFinished / torrentFileDetected / downloadFailed.
        void download(QString source, const Path &destFilePath, int maxConnections);
        bool isDownloading(const QString &source) const;

    signals:
        void downloadFinished(const QString &source, const Path &filePath);
        void torrentFileDetected(const QString &source, const Path &filePath);
        void downloadFailed(const QString &source, const QString &errorString);

    private:
        explicit DownloadManager(QObject *parent = nullptr);

        static DownloadManager *m_instance;
        QHash<QString, Downloader *> m_downloads; // source -> downloader
    };
}
