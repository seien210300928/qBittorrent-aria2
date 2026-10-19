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

#include "base/net/aria2/downloadmanager.h"

#include "base/net/aria2/downloader.h"

namespace Net::Aria2
{
    DownloadManager *DownloadManager::m_instance = nullptr;

    void DownloadManager::initInstance()
    {
        if (!m_instance)
            m_instance = new DownloadManager;
    }

    void DownloadManager::freeInstance()
    {
        delete m_instance;
        m_instance = nullptr;
    }

    DownloadManager *DownloadManager::instance()
    {
        return m_instance;
    }

    DownloadManager::DownloadManager(QObject *parent)
        : QObject(parent)
    {
    }

    void DownloadManager::download(const QString &source, const Path &destFilePath, const int maxConnections)
    {
        if (m_downloads.contains(source))
            return;

        auto *downloader = new Downloader(source, destFilePath, maxConnections, this);
        m_downloads[source] = downloader;

        connect(downloader, &Downloader::finished, this, [this, source, downloader](const QString &, const Path &filePath)
        {
            m_downloads.remove(source);
            downloader->deleteLater();
            emit downloadFinished(source, filePath);
        });
        connect(downloader, &Downloader::torrentFileDetected, this, [this, source, downloader](const QString &, const Path &filePath)
        {
            m_downloads.remove(source);
            downloader->deleteLater();
            emit torrentFileDetected(source, filePath);
        });
        connect(downloader, &Downloader::failed, this, [this, source, downloader](const QString &, const QString &errorString)
        {
            m_downloads.remove(source);
            downloader->deleteLater();
            emit downloadFailed(source, errorString);
        });

        downloader->start();
    }

    bool DownloadManager::isDownloading(const QString &source) const
    {
        return m_downloads.contains(source);
    }
}
