/*
 * Bittorrent Client using Qt and libtorrent.
 * Copyright (C) 2018-2025  Vladimir Golovnev <glassez@yandex.ru>
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

#include "serialize_httpdownload.h"

#include <QDateTime>
#include <QFileInfo>

#include "base/global.h"
#include "base/net/aria2/httpdownload.h"
#include "serialize_torrent.h"

QVariantMap serialize(const Net::Aria2::HttpDownload &download)
{
    QVariantMap map;

    const qint64 total = download.total();
    const qint64 downloaded = download.downloaded();
    const double progress = (total > 0) ? (static_cast<double>(downloaded) / total) : 0;
    const qint64 eta = ((total > 0) && (download.speed() > 0))
            ? ((total - downloaded) / download.speed()) : 8640000;

    const QFileInfo fileInfo {download.savePath().toString()};

    map.insert(KEY_TORRENT_ID, download.id());
    map.insert(KEY_TORRENT_NAME, fileInfo.fileName());
    map.insert(KEY_TORRENT_SIZE, total);
    map.insert(KEY_TORRENT_TOTAL_SIZE, total);
    map.insert(KEY_TORRENT_PROGRESS, progress);
    map.insert(KEY_TORRENT_DLSPEED, download.speed());
    map.insert(KEY_TORRENT_UPSPEED, 0);
    map.insert(KEY_TORRENT_ETA, eta);
    map.insert(KEY_TORRENT_SAVE_PATH, download.savePath().parentPath().toString());
    map.insert(KEY_TORRENT_DOWNLOAD_PATH, download.savePath().parentPath().toString());
    map.insert(KEY_TORRENT_CONTENT_PATH, download.savePath().toString());
    map.insert(KEY_TORRENT_ROOT_PATH, download.savePath().parentPath().toString());
    map.insert(KEY_TORRENT_ADDED_ON, download.addedOn());
    map.insert(KEY_TORRENT_COMPLETION_ON, (download.state() == Net::Aria2::HttpDownload::State::Completed)
            ? QDateTime::currentSecsSinceEpoch() : -1);
    map.insert(KEY_TORRENT_AMOUNT_DOWNLOADED, downloaded);
    map.insert(KEY_TORRENT_AMOUNT_UPLOADED, 0);
    map.insert(KEY_TORRENT_AMOUNT_DOWNLOADED_SESSION, downloaded);
    map.insert(KEY_TORRENT_AMOUNT_UPLOADED_SESSION, 0);
    map.insert(KEY_TORRENT_AMOUNT_LEFT, (total > 0) ? (total - downloaded) : 0);
    map.insert(KEY_TORRENT_AMOUNT_COMPLETED, downloaded);
    map.insert(KEY_TORRENT_QUEUE_POSITION, 0);
    map.insert(KEY_TORRENT_SEEDS, 0);
    map.insert(KEY_TORRENT_NUM_COMPLETE, 0);
    map.insert(KEY_TORRENT_LEECHS, 0);
    map.insert(KEY_TORRENT_NUM_INCOMPLETE, 0);
    map.insert(KEY_TORRENT_RATIO, 0);
    map.insert(KEY_TORRENT_POPULARITY, 0);
    map.insert(KEY_TORRENT_CATEGORY, QString());
    map.insert(KEY_TORRENT_TAGS, QStringList());
    map.insert(KEY_TORRENT_TRACKER, QString());
    map.insert(KEY_TORRENT_TRACKERS_COUNT, 0);
    map.insert(KEY_TORRENT_DL_LIMIT, -1);
    map.insert(KEY_TORRENT_UP_LIMIT, -1);
    map.insert(KEY_TORRENT_MAX_RATIO, -1);
    map.insert(KEY_TORRENT_MAX_SEEDING_TIME, -1);
    map.insert(KEY_TORRENT_MAX_INACTIVE_SEEDING_TIME, -1);
    map.insert(KEY_TORRENT_SHARE_LIMIT_ACTION, 0);
    map.insert(KEY_TORRENT_AVAILABILITY, -1);
    map.insert(KEY_TORRENT_HAS_METADATA, true);
    map.insert(KEY_TORRENT_PRIVATE, false);
    map.insert(KEY_TORRENT_AUTO_TORRENT_MANAGEMENT, false);
    map.insert(KEY_TORRENT_CONNECTIONS_COUNT, 0);
    map.insert(KEY_TORRENT_CONNECTIONS_LIMIT, -1);
    map.insert(KEY_TORRENT_TOTAL_WASTED, 0);
    map.insert(KEY_TORRENT_PIECES_NUM, 0);
    map.insert(KEY_TORRENT_PIECE_SIZE, 0);
    map.insert(KEY_TORRENT_PIECES_HAVE, 0);
    map.insert(KEY_TORRENT_TIME_ACTIVE, 0);
    map.insert(KEY_TORRENT_SEEDING_TIME, 0);
    map.insert(KEY_TORRENT_SEQUENTIAL_DOWNLOAD, false);
    map.insert(KEY_TORRENT_FIRST_LAST_PIECE_PRIO, false);
    map.insert(KEY_TORRENT_SUPER_SEEDING, false);
    map.insert(KEY_TORRENT_FORCE_START, false);
    map.insert(KEY_TORRENT_MAGNET_URI, QString());
    map.insert(KEY_TORRENT_COMMENT, QString());
    map.insert(KEY_TORRENT_CREATED_BY, QString());
    map.insert(KEY_TORRENT_CREATION_DATE, 0);
    map.insert(KEY_TORRENT_INFOHASHV1, QString());
    map.insert(KEY_TORRENT_INFOHASHV2, QString());

    switch (download.state())
    {
    case Net::Aria2::HttpDownload::State::Downloading:
        map.insert(KEY_TORRENT_STATE, u"downloading"_s);
        break;
    case Net::Aria2::HttpDownload::State::Paused:
        map.insert(KEY_TORRENT_STATE, u"pausedDL"_s);
        break;
    case Net::Aria2::HttpDownload::State::Completed:
        map.insert(KEY_TORRENT_STATE, u"stoppedUP"_s);
        break;
    case Net::Aria2::HttpDownload::State::Failed:
        map.insert(KEY_TORRENT_STATE, u"error"_s);
        break;
    }

    return map;
}
