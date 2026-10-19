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

#include "base/net/aria2/downloader.h"

#include <algorithm>

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslError>
#include <QTimer>
#include <QUrl>
#include <utility>

#include "base/global.h"
#include "base/logger.h"
#include "base/net/proxyconfigurationmanager.h"
#include "base/preferences.h"

namespace Net::Aria2
{
    namespace
    {
        constexpr int64_t defaultMinSplit = 1 << 20; // 1 MiB
        constexpr int maxNetworkErrorRetries = 30;
        constexpr int retryBaseDelayMs = 500;
        constexpr int speedSampleIntervalMs = 1000;
        constexpr int controlSaveIntervalMs = 1000;

        Path controlFilePath(const Path &destFilePath)
        {
            return Path(destFilePath.toString() + u".qba");
        }

        // Follows qBittorrent's global proxy settings for general-purpose downloads.
        QNetworkProxy generalProxy()
        {
            if (!Preferences::instance()->useProxyForGeneralPurposes())
                return QNetworkProxy(QNetworkProxy::NoProxy);

            const auto *proxyManager = ProxyConfigurationManager::instance();
            const ProxyConfiguration proxyConfig = proxyManager->proxyConfiguration();
            switch (proxyConfig.type)
            {
            case ProxyType::None:
            case ProxyType::SOCKS4:
                return QNetworkProxy(QNetworkProxy::NoProxy);
            case ProxyType::HTTP:
            {
                QNetworkProxy proxy {QNetworkProxy::HttpProxy, proxyConfig.ip, proxyConfig.port
                        , (proxyConfig.authEnabled ? proxyConfig.username : QString())
                        , (proxyConfig.authEnabled ? proxyConfig.password : QString())};
                proxy.setCapabilities(proxyConfig.hostnameLookupEnabled
                        ? (proxy.capabilities() | QNetworkProxy::HostNameLookupCapability)
                        : (proxy.capabilities() & ~QNetworkProxy::HostNameLookupCapability));
                return proxy;
            }
            case ProxyType::SOCKS5:
            {
                QNetworkProxy proxy {QNetworkProxy::Socks5Proxy, proxyConfig.ip, proxyConfig.port
                        , (proxyConfig.authEnabled ? proxyConfig.username : QString())
                        , (proxyConfig.authEnabled ? proxyConfig.password : QString())};
                proxy.setCapabilities(proxyConfig.hostnameLookupEnabled
                        ? (proxy.capabilities() | QNetworkProxy::HostNameLookupCapability)
                        : (proxy.capabilities() & ~QNetworkProxy::HostNameLookupCapability));
                return proxy;
            }
            }
            return QNetworkProxy(QNetworkProxy::NoProxy);
        }
    }

    Downloader::Downloader(const QString &url, const Path &destFilePath, const int maxConnections, QObject *parent)
        : QObject(parent)
        , m_url {url}
        , m_destFilePath {destFilePath}
        , m_maxConnections {std::clamp(maxConnections, 1, 64)}
        , m_planner {defaultMinSplit}
    {
        m_nam = new QNetworkAccessManager(this);
        m_nam->setProxy(generalProxy());
        connect(m_nam, &QNetworkAccessManager::sslErrors, this, [](QNetworkReply *reply, const QList<QSslError> &errors)
        {
            if (Preferences::instance()->isIgnoreSSLErrors())
            {
                reply->ignoreSslErrors();
            }
            else
            {
                QStringList errorList;
                for (const QSslError &error : errors)
                    errorList += error.errorString();
                LogMsg(tr("SSL error, URL: \"%1\", errors: \"%2\"").arg(reply->url().toString(), errorList.join(u". ")), Log::WARNING);
            }
        });
        m_file = new QFile(m_destFilePath.toString(), this);
        m_speedTimer = new QTimer(this);
        m_speedTimer->setInterval(speedSampleIntervalMs);
        connect(m_speedTimer, &QTimer::timeout, this, &Downloader::sampleSpeeds);
        m_controlTimer = new QTimer(this);
        m_controlTimer->setInterval(controlSaveIntervalMs);
        connect(m_controlTimer, &QTimer::timeout, this, &Downloader::saveControlFile);
    }

    Downloader::~Downloader()
    {
        cancel();
    }

    void Downloader::start()
    {
        if (m_state != State::Idle)
            return;

        if (tryResume())
        {
            m_state = State::Downloading;
            m_speedTimer->start();
            m_controlTimer->start();
            startWorkers();
            return;
        }

        probe();
    }

    void Downloader::cancel()
    {
        m_cancelled = true;
        if (m_probeReply)
            m_probeReply->abort();
        for (QNetworkReply *reply : m_replies)
            reply->abort();
        m_replies.clear();
        m_replyIds.clear();
        m_speedTimer->stop();
        m_controlTimer->stop();
        if (m_file->isOpen())
            m_file->close();
    }

    void Downloader::pause()
    {
        if ((m_state != State::Downloading) && (m_state != State::Probing))
            return;

        // Keep the control file so resume() can continue where we left off.
        saveControlFile();

        m_cancelled = true;
        m_state = State::Paused;
        m_speedTimer->stop();
        m_controlTimer->stop();
        if (m_probeReply)
            m_probeReply->abort();
        for (QNetworkReply *reply : m_replies)
            reply->abort();
        m_replies.clear();
        m_replyIds.clear();
        m_activeWorkers.clear();
        if (m_file->isOpen())
            m_file->close();
    }

    void Downloader::resume()
    {
        if (m_state != State::Paused)
            return;

        m_cancelled = false;
        m_state = State::Downloading;
        m_speedTimer->start();
        m_controlTimer->start();

        if (tryResume())
        {
            startWorkers();
            return;
        }

        // No usable control file: probe and start over.
        probe();
    }

    qint64 Downloader::downloaded() const
    {
        return m_planner.received().total();
    }

    qint64 Downloader::total() const
    {
        return m_planner.length();
    }

    qint64 Downloader::speed() const
    {
        return m_currentSpeed;
    }

    Downloader::State Downloader::state() const
    {
        return m_state;
    }

    void Downloader::probe()
    {
        m_state = State::Probing;
        if (isTorrentUrl())
        {
            // Content is known to be a .torrent file: single connection, no probing.
            m_mode = Mode::SingleConnection;
            m_state = State::Downloading;
            m_planner.setLength(-1); // unknown length, grows until reply ends
            m_speedTimer->start();
            m_controlTimer->start();
            startSingleConnection();
            return;
        }
        probeViaHead();
    }

    bool Downloader::isTorrentUrl() const
    {
        return m_url.contains(u".torrent", Qt::CaseInsensitive);
    }

    void Downloader::probeViaHead()
    {
        QNetworkRequest request {QUrl(m_url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        m_probeReply = m_nam->head(request);
        connect(m_probeReply, &QNetworkReply::finished, this, &Downloader::onProbeReplyFinished);
        connect(m_probeReply, &QNetworkReply::errorOccurred, this, [this](const QNetworkReply::NetworkError error)
        {
            // HEAD may be rejected (403/405): retry probing with a ranged GET.
            if ((error == QNetworkReply::ContentAccessDenied)
                    || (error == QNetworkReply::ContentOperationNotPermittedError)
                    || (error == QNetworkReply::ProtocolInvalidOperationError))
            {
                if (m_probeReply)
                {
                    m_probeReply->abort();
                    m_probeReply->deleteLater();
                    m_probeReply = nullptr;
                }
                probeViaGet();
            }
        });
    }

    void Downloader::probeViaGet()
    {
        QNetworkRequest request {QUrl(m_url)};
        request.setRawHeader("Range", "bytes=0-0");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        m_probeReply = m_nam->get(request);
        connect(m_probeReply, &QNetworkReply::finished, this, &Downloader::onProbeReplyFinished);
    }

    void Downloader::onProbeReplyFinished()
    {
        if (m_cancelled)
            return;

        QNetworkReply *reply = m_probeReply;
        if (!reply)
            return; // probing was restarted via GET fallback

        m_probeReply = nullptr;
        reply->deleteLater();

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError)
        {
            fail(reply->errorString());
            return;
        }

        m_networkErrorRetries = 0;

        const bool acceptRanges = reply->hasRawHeader("Accept-Ranges")
                && (QString::fromLatin1(reply->rawHeader("Accept-Ranges")).contains(u"bytes", Qt::CaseInsensitive));
        const bool contentRange = reply->hasRawHeader("Content-Range");
        const int64_t contentLength = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        const QByteArray contentType = reply->rawHeader("Content-Type");

        if (contentType.contains("bittorrent"))
        {
            // The URL serves a .torrent file.
            m_mode = Mode::SingleConnection;
            m_state = State::Downloading;
            m_planner.setLength(-1);
            m_speedTimer->start();
            m_controlTimer->start();
            startSingleConnection();
            return;
        }

        const bool ranged = (status == 206) || acceptRanges || contentRange;
        if (!ranged || (contentLength <= 0))
        {
            // Server does not support Range requests (or length unknown):
            // fall back to a single connection.
            m_mode = Mode::SingleConnection;
            m_state = State::Downloading;
            m_planner.setLength(contentLength > 0 ? contentLength : -1);
            m_speedTimer->start();
            m_controlTimer->start();
            startSingleConnection();
            return;
        }

        // Multi-connection mode.
        m_mode = Mode::MultiConnection;
        m_state = State::Downloading;
        m_planner.setLength(contentLength);
        if (!m_file->open(QIODevice::WriteOnly | QIODevice::Unbuffered))
        {
            fail(tr("Cannot open destination file: %1").arg(m_destFilePath.toString()));
            return;
        }
        m_file->resize(contentLength); // pre-allocate
        m_speedTimer->start();
        m_controlTimer->start();
        startWorkers();
    }

    void Downloader::startWorkers()
    {
        for (int i = 0; i < m_maxConnections; ++i)
            assignWorker(m_nextWorkerId++);
    }

    void Downloader::assignWorker(const int id)
    {
        if (m_cancelled || (m_state != State::Downloading))
            return;

        const auto range = m_planner.assign(id);
        if (!range)
        {
            m_activeWorkers.remove(id);
            return;
        }

        m_workerBytes[id] = 0;
        m_activeWorkers.insert(id);

        QNetworkRequest request {QUrl(m_url)};
        request.setRawHeader("Range", (QByteArrayLiteral("bytes=") + QByteArray::number(range->first) + "-" + QByteArray::number(range->second - 1)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply *reply = m_nam->get(request);
        m_replies[id] = reply;
        m_replyIds[reply] = id;
        m_startupTimer.start();
        m_startupMeasured = false;
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { onReplyReadyRead(reply); });
        connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply); });
        connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](const QNetworkReply::NetworkError error)
        {
            onReplyError(reply, error);
        });
    }

    void Downloader::sendWorkerRequest(const int id, const int64_t begin, const int64_t end)
    {
        m_workerBytes[id] = 0;
        m_activeWorkers.insert(id);

        QNetworkRequest request {QUrl(m_url)};
        request.setRawHeader("Range", (QByteArrayLiteral("bytes=") + QByteArray::number(begin) + "-" + QByteArray::number(end - 1)));
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

        QNetworkReply *reply = m_nam->get(request);
        m_replies[id] = reply;
        m_replyIds[reply] = id;
        m_startupTimer.start();
        m_startupMeasured = false;
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { onReplyReadyRead(reply); });
        connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply); });
        connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](const QNetworkReply::NetworkError error)
        {
            onReplyError(reply, error);
        });
    }

    void Downloader::startSingleConnection()
    {
        const int id = m_nextWorkerId++;
        m_workerBytes[id] = 0;
        m_activeWorkers.insert(id);

        QNetworkRequest request {QUrl(m_url)};
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        if (m_planner.length() > 0)
        {
            m_planner.place(id, 0, m_planner.length());
            request.setRawHeader("Range", (QByteArrayLiteral("bytes=0-") + QByteArray::number(m_planner.length() - 1)));
        }
        else
        {
            m_planner.place(id, 0, unbounded);
        }

        QNetworkReply *reply = m_nam->get(request);
        m_replies[id] = reply;
        m_replyIds[reply] = id;
        connect(reply, &QNetworkReply::readyRead, this, [this, reply] { onReplyReadyRead(reply); });
        connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply); });
        connect(reply, &QNetworkReply::errorOccurred, this, [this, reply](const QNetworkReply::NetworkError error)
        {
            onReplyError(reply, error);
        });

        if (!m_file->isOpen())
        {
            if (!m_file->open(QIODevice::WriteOnly | QIODevice::Unbuffered))
            {
                fail(tr("Cannot open destination file: %1").arg(m_destFilePath.toString()));
                return;
            }
            if (m_planner.length() > 0)
                m_file->resize(m_planner.length());
        }
    }

    void Downloader::onReplyReadyRead(QNetworkReply *reply)
    {
        if (m_cancelled || (m_state != State::Downloading))
        {
            reply->abort();
            return;
        }

        const int id = m_replyIds.value(reply, -1);
        if (id < 0)
            return;

        if (!m_startupMeasured)
        {
            m_planner.noteStartup(m_startupTimer.elapsed() / 1000.0);
            m_startupMeasured = true;
        }

        const QByteArray data = reply->readAll();
        const auto *worker = m_planner.worker(id);
        if (!worker)
        {
            // The worker was cancelled (e.g. by extend()); discard payload.
            return;
        }
        const int64_t writePos = worker->position;
        const int64_t usable = m_planner.receive(id, data.size());
        if (usable > 0)
        {
            m_file->seek(writePos);
            m_file->write(data.constData(), static_cast<qint64>(usable));
            m_workerBytes[id] += usable;
            m_controlDirty = true;
        }
    }

    void Downloader::onReplyFinished(QNetworkReply *reply)
    {
        if (m_cancelled)
            return;

        const int id = m_replyIds.value(reply, -1);
        if (id < 0)
            return;

        reply->deleteLater();
        m_replies.remove(id);
        m_replyIds.remove(reply);
        m_activeWorkers.remove(id);

        if (reply->error() != QNetworkReply::NoError)
            return; // handled by onReplyError()

        if (m_mode == Mode::SingleConnection)
        {
            m_planner.release(id);
            if (m_planner.complete() || (m_planner.length() < 0))
            {
                finalize();
                return;
            }
            // Server ended early or with an error status; treat as failure.
            fail(tr("Connection closed before the download completed"));
            return;
        }

        // Multi-connection mode: extend into the next range or fetch new work.
        if (m_planner.complete())
        {
            finalize();
            return;
        }

        std::optional<int> cancelled;
        if (m_planner.extend(id, cancelled))
        {
            if (cancelled)
            {
                // The next worker's range was taken over; cancel its request.
                if (QNetworkReply *otherReply = m_replies.value(*cancelled))
                {
                    otherReply->abort();
                    otherReply->deleteLater();
                    m_replies.remove(*cancelled);
                    m_replyIds.remove(otherReply);
                    m_activeWorkers.remove(*cancelled);
                }
            }
            // Continue fetching the extended range [position, end).
            const auto *worker = m_planner.worker(id);
            if (worker && (worker->position < worker->end))
                sendWorkerRequest(id, worker->position, worker->end);
            return;
        }

        assignWorker(id); // may return nothing if no work is left
    }

    void Downloader::onReplyError(QNetworkReply *reply, const QNetworkReply::NetworkError error)
    {
        if (m_cancelled)
            return;

        const int id = m_replyIds.value(reply, -1);
        if (id < 0)
            return;

        reply->deleteLater();
        m_replies.remove(id);
        m_replyIds.remove(reply);
        m_activeWorkers.remove(id);

        if (error == QNetworkReply::OperationCanceledError)
            return;

        // The server may reject the range request (416) because it does not
        // support Range at all; fall back to a single connection.
        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus == 416)
        {
            if (m_mode == Mode::MultiConnection)
            {
                m_mode = Mode::SingleConnection;
                m_planner.reset();
                m_planner.setLength(-1);
                // Take a snapshot first: abort() may synchronously invoke our
                // error handlers which modify the maps being iterated.
                const QList<QNetworkReply *> pendingReplies = m_replies.values();
                m_replies.clear();
                m_replyIds.clear();
                m_activeWorkers.clear();
                for (QNetworkReply *other : pendingReplies)
                {
                    other->abort();
                    other->deleteLater();
                }
                m_file->close();
                QFile::remove(m_destFilePath.toString());
                startSingleConnection();
            }
            return;
        }

        if (!isRetryableError(error))
        {
            fail(reply->errorString());
            return;
        }

        ++m_networkErrorRetries;
        if (m_networkErrorRetries > maxNetworkErrorRetries)
        {
            fail(tr("Too many network errors: %1").arg(reply->errorString()));
            return;
        }

        scheduleWorkerRetry(id);
    }

    void Downloader::scheduleWorkerRetry(const int id)
    {
        const int delayMs = std::min(retryBaseDelayMs * (1 << std::min(m_networkErrorRetries, 6)), 30000);
        QTimer::singleShot(delayMs, this, [this, id]
        {
            if (m_cancelled || (m_state != State::Downloading))
                return;

            const auto *worker = m_planner.worker(id);
            if (!worker)
                return;

            // Re-request the worker's current range.
            const int64_t begin = worker->position;
            const int64_t end = worker->end;
            if (begin >= end)
            {
                assignWorker(id);
                return;
            }

            sendWorkerRequest(id, begin, end);
        });
    }

    void Downloader::resetWorker(const int id)
    {
        if (QNetworkReply *reply = m_replies.take(id))
        {
            reply->abort();
            reply->deleteLater();
            m_replyIds.remove(reply);
            m_activeWorkers.remove(id);
        }
    }

    void Downloader::sampleSpeeds()
    {
        if (m_state != State::Downloading)
            return;

        qint64 totalBytes = 0;
        for (auto it = m_workerBytes.cbegin(); it != m_workerBytes.cend(); ++it)
        {
            const int id = it.key();
            const qint64 bytes = it.value();
            totalBytes += bytes;
            const double speed = bytes / (speedSampleIntervalMs / 1000.0);
            m_planner.updateSpeed(id, speed, (speedSampleIntervalMs / 1000.0));
        }
        m_currentSpeed = (totalBytes * 1000) / speedSampleIntervalMs;
        m_workerBytes.clear();
    }

    void Downloader::saveControlFile()
    {
        if (!m_controlDirty || (m_state != State::Downloading))
            return;

        m_controlDirty = false;

        QJsonArray ranges;
        for (const auto &range : m_planner.received().ranges())
        {
            QJsonArray item;
            item.append(range.first);
            item.append(range.second);
            ranges.append(item);
        }

        QJsonObject root;
        root.insert(u"url"_s, m_url);
        root.insert(u"length"_s, static_cast<double>(m_planner.length()));
        root.insert(u"ranges"_s, ranges);

        QFile controlFile(controlFilePath(m_destFilePath).toString());
        if (controlFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            controlFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
            controlFile.close();
        }
    }

    bool Downloader::tryResume()
    {
        const Path controlPath = controlFilePath(m_destFilePath);
        if (!controlPath.exists())
            return false;

        QFile controlFile(controlPath.toString());
        if (!controlFile.open(QIODevice::ReadOnly))
            return false;

        const QJsonDocument doc = QJsonDocument::fromJson(controlFile.readAll());
        controlFile.close();
        if (!doc.isObject())
            return false;

        const QJsonObject root = doc.object();
        if (root.value(u"url"_s).toString() != m_url)
            return false;

        const int64_t length = static_cast<int64_t>(root.value(u"length"_s).toDouble());
        if (length <= 0)
            return false;

        std::vector<Range> ranges;
        const QJsonArray rangesArray = root.value(u"ranges"_s).toArray();
        for (const auto &value : rangesArray)
        {
            const QJsonArray item = value.toArray();
            if (item.size() != 2)
                continue;
            ranges.emplace_back(static_cast<int64_t>(item.at(0).toDouble())
                    , static_cast<int64_t>(item.at(1).toDouble()));
        }

        if (!m_file->open(QIODevice::WriteOnly | QIODevice::Unbuffered))
            return false;
        m_file->resize(length);

        m_planner.setLength(length);
        m_planner.restore(ranges);
        return true;
    }

    void Downloader::finalize()
    {
        if (m_state != State::Downloading)
            return;

        // All data is on disk; stop timers, clean up and report.
        m_state = State::Finished;
        m_speedTimer->stop();
        m_controlTimer->stop();
        m_file->flush();
        m_file->close();
        QFile::remove(controlFilePath(m_destFilePath).toString());

        emit finished(m_url, m_destFilePath);
    }

    void Downloader::fail(const QString &errorString)
    {
        if (m_state == State::Failed)
            return;

        m_state = State::Failed;
        m_speedTimer->stop();
        m_controlTimer->stop();
        if (m_file->isOpen())
            m_file->close();

        // Keep the control file and partial data for a later resume.
        if (m_controlDirty)
        {
            m_controlDirty = false;
            QJsonArray ranges;
            for (const auto &range : m_planner.received().ranges())
            {
                QJsonArray item;
                item.append(range.first);
                item.append(range.second);
                ranges.append(item);
            }
            QJsonObject root;
            root.insert(u"url"_s, m_url);
            root.insert(u"length"_s, static_cast<double>(m_planner.length()));
            root.insert(u"ranges"_s, ranges);
            QFile controlFile(controlFilePath(m_destFilePath).toString());
            if (controlFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                controlFile.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
                controlFile.close();
            }
        }

        LogMsg(tr("Aria2 engine download failed. URL: \"%1\". Reason: \"%2\"").arg(m_url, errorString), Log::WARNING);
        emit failed(m_url, errorString);
    }

    bool Downloader::isRetryableError(const QNetworkReply::NetworkError error) const
    {
        switch (error)
        {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::ProtocolUnknownError:
        case QNetworkReply::TooManyRedirectsError:
            return true;
        default:
            return false;
        }
    }
}
