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

#include <QList>
#include <QObject>
#include <QString>

#include "base/path.h"

namespace Net::Aria2
{
    class HttpDownload;

    // Owns HTTP(S) file download tasks (aria2 engine). Tasks are surfaced in the
    // existing transfer list UI (GUI and WebUI) through downloads().
    class HttpDownloadManager final : public QObject
    {
        Q_OBJECT
        Q_DISABLE_COPY_MOVE(HttpDownloadManager)

    public:
        static void initInstance();
        static void freeInstance();
        static HttpDownloadManager *instance();

        // `savePath` is the full destination file path.
        HttpDownload *addDownload(QString url, Path savePath, int maxConnections);
        // Removes the task; deletes the partially downloaded file if `deleteFiles`.
        void removeDownload(HttpDownload *download, bool deleteFiles);
        QList<HttpDownload *> downloads() const;
        HttpDownload *findById(const QString &id) const;

    signals:
        void downloadAdded(Net::Aria2::HttpDownload *download);
        void downloadRemoved(Net::Aria2::HttpDownload *download);

    private:
        explicit HttpDownloadManager(QObject *parent = nullptr);

        static HttpDownloadManager *m_instance;
        QList<HttpDownload *> m_downloads;
    };
}
