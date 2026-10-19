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

#include "base/net/aria2/httpdownload.h"

#include <utility>

#include <QDateTime>

#include "base/net/aria2/downloader.h"

namespace Net::Aria2
{
    HttpDownload::HttpDownload(QString id, QString url, Path savePath, Downloader *downloader, QObject *parent)
        : QObject(parent)
        , m_id {std::move(id)}
        , m_url {std::move(url)}
        , m_savePath {std::move(savePath)}
        , m_addedOn {QDateTime::currentSecsSinceEpoch()}
        , m_downloader {downloader}
    {
    }

    qint64 HttpDownload::downloaded() const
    {
        return m_downloader ? m_downloader->downloaded() : 0;
    }

    qint64 HttpDownload::total() const
    {
        return m_downloader ? m_downloader->total() : 0;
    }

    qint64 HttpDownload::speed() const
    {
        return m_downloader ? m_downloader->speed() : 0;
    }

    void HttpDownload::pause()
    {
        if (m_state != State::Downloading)
            return;

        m_downloader->pause();
        setState(State::Paused);
    }

    void HttpDownload::resume()
    {
        if (m_state != State::Paused)
            return;

        m_downloader->resume();
        setState(State::Downloading);
    }

    void HttpDownload::notifyProgress()
    {
        emit stateChanged(this);
    }

    void HttpDownload::setState(const State state, const QString &errorString)
    {
        m_state = state;
        m_errorString = errorString;
        emit stateChanged(this);
    }
}
