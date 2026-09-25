/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2023-2026 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
 *          MX Linux <http://mxlinux.org>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include "singleinstance.h"

#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QLocalSocket>
#include <QUrl>

#include <unistd.h>

namespace
{
constexpr int connectTimeoutMs {1000};
constexpr qint64 maxRequestSize {64 * 1024};

// The socket goes only in a runtime directory owned by this user, never in a shared place like /tmp
// where another user could create it first. Empty means single-instance mode is off.
QString socketPath()
{
    QStringList candidates;
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty()) {
        candidates << runtimeDir;
    }
    candidates << QStringLiteral("/run/user/%1").arg(getuid());
    for (const QString &dir : std::as_const(candidates)) {
        const QFileInfo info(dir);
        if (info.isDir() && info.ownerId() == getuid()) {
            return QDir(dir).filePath(QStringLiteral("mx-viewer.sock"));
        }
    }
    return {};
}

void readRequest(QLocalSocket *socket, const std::function<void(const QString &)> &handler)
{
    if (!socket->canReadLine()) {
        if (socket->bytesAvailable() > maxRequestSize) {
            socket->abort();
        }
        return;
    }
    // One request per connection.
    const QString argument = QString::fromUtf8(socket->readLine(maxRequestSize)).trimmed();
    socket->disconnectFromServer();
    handler(argument);
}
} // namespace

bool SingleInstance::forward(const QString &argument)
{
    const QString path = socketPath();
    if (path.isEmpty()) {
        return false;
    }
    QLocalSocket socket;
    socket.connectToServer(path);
    if (!socket.waitForConnected(connectTimeoutMs)) {
        return false;
    }
    // The running instance has a different working directory, so send local files as absolute URLs.
    QString request = argument;
    const QFileInfo file(argument);
    if (!argument.isEmpty() && file.isFile()) {
        request = QUrl::fromLocalFile(file.absoluteFilePath()).toString(QUrl::FullyEncoded);
    }
    request.remove(QLatin1Char('\n'));
    socket.write(request.toUtf8() + '\n');
    if (!socket.waitForBytesWritten(connectTimeoutMs)) {
        return false;
    }
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState) {
        socket.waitForDisconnected(connectTimeoutMs);
    }
    return true;
}

void SingleInstance::listen(QObject *parent, const std::function<void(const QString &)> &handler)
{
    const QString path = socketPath();
    if (path.isEmpty()) {
        return;
    }
    auto *server = new QLocalServer(parent);
    server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server->listen(path)) {
        // forward() already failed to connect, so a leftover socket file is from a crashed instance.
        QLocalServer::removeServer(path);
        if (!server->listen(path)) {
            delete server;
            return;
        }
    }
    QObject::connect(server, &QLocalServer::newConnection, server, [server, handler] {
        while (QLocalSocket *socket = server->nextPendingConnection()) {
            QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QLocalSocket::readyRead, socket, [socket, handler] { readRequest(socket, handler); });
            readRequest(socket, handler);
        }
    });
}
