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

#include "base/net/aria2/httpdownloadmanager.h"

#include <QFile>
#include <QTimer>
#include <QUuid>

#include "base/net/aria2/downloader.h"
#include "base/net/aria2/httpdownload.h"

namespace Net::Aria2
{
    namespace
    {
        constexpr int statePollIntervalMs = 500;
    }

    HttpDownloadManager *HttpDownloadManager::m_instance = nullptr;

    void HttpDownloadManager::initInstance()
    {
        if (!m_instance)
            m_instance = new HttpDownloadManager;
    }

    void HttpDownloadManager::freeInstance()
    {
        delete m_instance;
        m_instance = nullptr;
    }

    HttpDownloadManager *HttpDownloadManager::instance()
    {
        return m_instance;
    }

    HttpDownloadManager::HttpDownloadManager(QObject *parent)
        : QObject(parent)
    {
        auto *stateTimer = new QTimer(this);
        stateTimer->setInterval(statePollIntervalMs);
        connect(stateTimer, &QTimer::timeout, this, [this]
        {
            for (HttpDownload *download : m_downloads)
            {
                if (download->state() == HttpDownload::State::Downloading)
                    download->notifyProgress();
            }
        });
        stateTimer->start();
    }

    HttpDownload *HttpDownloadManager::addDownload(QString url, Path savePath, const int maxConnections)
    {
        auto *downloader = new Downloader(url, savePath, maxConnections, this);
        auto *download = new HttpDownload(QUuid::createUuid().toString(QUuid::WithoutBraces)
                , url, savePath, downloader, this);
        m_downloads.append(download);

        connect(downloader, &Downloader::finished, this, [download](const QString &, const Path &)
        {
            download->setState(HttpDownload::State::Completed);
        });
        connect(downloader, &Downloader::failed, this, [download](const QString &, const QString &errorString)
        {
            download->setState(HttpDownload::State::Failed, errorString);
        });

        downloader->start();
        emit downloadAdded(download);
        return download;
    }

    void HttpDownloadManager::removeDownload(HttpDownload *download, const bool deleteFiles)
    {
        if (!download || !m_downloads.contains(download))
            return;

        m_downloads.removeAll(download);

        if (deleteFiles)
            QFile::remove(download->savePath().toString());

        if (Downloader *downloader = download->m_downloader)
        {
            downloader->cancel();
            downloader->deleteLater();
        }
        download->m_downloader = nullptr;
        download->deleteLater();

        emit downloadRemoved(download);
    }

    QList<HttpDownload *> HttpDownloadManager::downloads() const
    {
        return m_downloads;
    }

    HttpDownload *HttpDownloadManager::findById(const QString &id) const
    {
        for (HttpDownload *download : m_downloads)
        {
            if (download->id() == id)
                return download;
        }
        return nullptr;
    }
}
