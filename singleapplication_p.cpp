// Copyright (c) Itay Grudev 2023
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// Permission is not granted to use this software or any of the associated files
// as sample data for the purposes of building machine learning models.
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

//
//  W A R N I N G !!!
//  -----------------
//
// This file is not part of the SingleApplication API. It is used purely as an
// implementation detail. This header file may change from version to
// version without notice, or may even be removed.
//

#include <cstdlib>
#include <cstddef>

#include <QtCore/QDir>
#include <QtCore/QThread>
#include <QtCore/QByteArray>
#include <QtCore/QDataStream>
#include <QtCore/QElapsedTimer>
#include <QtCore/QCryptographicHash>
#include <QtNetwork/QLocalServer>
#include <QtNetwork/QLocalSocket>

#include "message_coder.h"

#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
#include <QtCore/QRandomGenerator>
#else
#include <QtCore/QDateTime>
#endif

#include "singleapplication.h"
#include "singleapplication_p.h"

#ifdef Q_OS_UNIX
    #include <unistd.h>
    #include <sys/types.h>
    #include <pwd.h>
#endif

#ifdef Q_OS_WIN
    #ifndef NOMINMAX
        #define NOMINMAX 1
    #endif
    #include <windows.h>
#endif

SingleApplicationPrivate::SingleApplicationPrivate( SingleApplication *q_ptr )
    : q_ptr( q_ptr ), server( nullptr ), socket( nullptr ), instanceNumber( 0 ), connectionMap(), serverThread( nullptr )
{
}

SingleApplicationPrivate::~SingleApplicationPrivate()
{
    if (serverThread) {
        serverThread->stop();
        serverThread->wait();
        delete serverThread;
    }

    if( socket != nullptr ){
        socket->close();
        delete socket;
    }
}

QString SingleApplicationPrivate::getUsername()
{
#ifdef Q_OS_WIN
      wchar_t username[UNLEN + 1];
      // Specifies size of the buffer on input
      DWORD usernameLength = UNLEN + 1;
      if( GetUserNameW( username, &usernameLength ) )
          return QString::fromWCharArray( username );
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
      return QString::fromLocal8Bit( qgetenv( "USERNAME" ) );
#else
      return qEnvironmentVariable( "USERNAME" );
#endif
#endif
#ifdef Q_OS_UNIX
      QString username;
      uid_t uid = geteuid();
      struct passwd *pw = getpwuid( uid );
      if( pw )
          username = QString::fromLocal8Bit( pw->pw_name );
      if ( username.isEmpty() ){
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
          username = QString::fromLocal8Bit( qgetenv( "USER" ) );
#else
          username = qEnvironmentVariable( "USER" );
#endif
      }
      return username;
#endif
}

void SingleApplicationPrivate::genBlockServerName()
{
#ifdef Q_OS_MACOS
    // Maximum key size on macOS is PSHMNAMLEN (31).
    QCryptographicHash appData( QCryptographicHash::Md5 );
#else
    QCryptographicHash appData( QCryptographicHash::Sha256 );
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    appData.addData( "SingleApplication", 17 );
#else
    appData.addData( QByteArrayView{"SingleApplication"} );
#endif
    appData.addData( SingleApplication::app_t::applicationName().toUtf8() );
    appData.addData( SingleApplication::app_t::organizationName().toUtf8() );
    appData.addData( SingleApplication::app_t::organizationDomain().toUtf8() );

    if ( ! appDataList.isEmpty() )
        appData.addData( appDataList.join(QString()).toUtf8() );

    if( ! (options & SingleApplication::Mode::ExcludeAppVersion) ){
        appData.addData( SingleApplication::app_t::applicationVersion().toUtf8() );
    }

    if( ! (options & SingleApplication::Mode::ExcludeAppPath) ){
#if defined(Q_OS_WIN)
        appData.addData( SingleApplication::app_t::applicationFilePath().toLower().toUtf8() );
#elif defined(Q_OS_LINUX)
        // If the application is running as an AppImage then the APPIMAGE env var should be used
        // instead of applicationPath() as each instance is launched with its own executable path
        const QByteArray appImagePath = qgetenv( "APPIMAGE" );
        if( appImagePath.isEmpty() ){ // Not running as AppImage: use path to executable file
            appData.addData( SingleApplication::app_t::applicationFilePath().toUtf8() );
        } else { // Running as AppImage: Use absolute path to AppImage file
            appData.addData( appImagePath );
        };
#else
        appData.addData( SingleApplication::app_t::applicationFilePath().toUtf8() );
#endif
    }

    // User level block requires a user specific data in the hash
    if( options & SingleApplication::Mode::User ){
        appData.addData( getUsername().toUtf8() );
    }

    // Replace the backslash in RFC 2045 Base64 [a-zA-Z0-9+/=] to comply with
    // server naming requirements.
    blockServerName = QString::fromUtf8(appData.result().toBase64().replace("/", "_"));
}

bool SingleApplicationPrivate::startPrimary( uint timeout )
{
    QLocalServer::removeServer(blockServerName);
    serverThread = new ServerThread(blockServerName, this);

    connect(serverThread, &ServerThread::newConnection,
            this, &SingleApplicationPrivate::slotConnectionEstablished);

    serverThread->start();

    // Wait for the server to start (with timeout)
    QElapsedTimer timer;
    timer.start();
    while (!serverThread->isRunning() && timer.elapsed() < timeout) {
        QThread::msleep(10);
    }

    return serverThread->isRunning();
}

bool SingleApplicationPrivate::connectToPrimary(uint timeout) {
    if (socket == nullptr)
        socket = new QLocalSocket(this);

    if (socket->state() == QLocalSocket::ConnectedState)
        return true;

    if (socket->state() != QLocalSocket::ConnectingState)
        socket->connectToServer(blockServerName);

    return socket->waitForConnected(timeout);
}

void SingleApplicationPrivate::notifySecondaryStart(uint timeout)
{
    sendApplicationMessage(SingleApplication::MessageType::NewInstance, QByteArray(), timeout);
}

bool SingleApplicationPrivate::sendApplicationMessage( SingleApplication::MessageType messageType, QByteArray content, uint timeout )
{
    QElapsedTimer elapsedTime;
    elapsedTime.start();

    if( ! connectToPrimary( timeout * 2 / 3 ))
        return false;

    MessageCoder coder( socket );
    coder.sendMessage( messageType, instanceNumber, content );

    socket->flush();
    if (!socket->waitForBytesWritten( qMax(timeout - elapsedTime.elapsed(), 1) ))
        return false;

    // Wait for an ACK message
    if( socket->waitForReadyRead( timeout )){
        QByteArray responseBytes = socket->readAll();
        SingleApplicationMessage response = SingleApplicationMessage( responseBytes );

        // The response message is invalid
        if( response.invalid )
            return false;

        // The response message didn't contain the primary instance id
        if( response.instanceId != 0 )
            return false;

        // This isn't an acknowledge message
        if( response.type != SingleApplication::MessageType::Acknowledge )
            return false;

        return true;
    }

    return false;
}

qint64 SingleApplicationPrivate::primaryPid() const
{
    if (!connectToPrimary(1000)) {
        return -1;
    }

    MessageCoder coder(socket);
    coder.sendMessage(SingleApplication::MessageType::PrimaryPidRequest, instanceNumber, QByteArray());

    if (!socket->waitForReadyRead(1000)) {
        return -1;
    }

    QByteArray response = socket->readAll();
    QDataStream stream(response);
    qint64 pid;
    stream >> pid;

    return pid;
}

QString SingleApplicationPrivate::primaryUser() const
{
    if (!connectToPrimary(1000)) {
        return QString();
    }

    MessageCoder coder(socket);
    coder.sendMessage(SingleApplication::MessageType::PrimaryUserRequest, instanceNumber, QByteArray());

    if (!socket->waitForReadyRead(1000)) {
        return QString();
    }

    QByteArray response = socket->readAll();
    QDataStream stream(response);
    QString user;
    stream >> user;

    return user;
}

/**
 * @brief Executed when a connection has been made to the LocalServer
 */
void SingleApplicationPrivate::slotConnectionEstablished()
{
    QLocalSocket *nextConnSocket = serverThread->nextPendingConnection();
    if (!nextConnSocket) {
        qWarning() << "Failed to get next pending connection";
        return;
    }

    connectionMap.insert(nextConnSocket, ConnectionInfo());
    connectionMap[nextConnSocket].coder = new MessageCoder(nextConnSocket);

    QObject::connect(nextConnSocket, &QLocalSocket::disconnected, nextConnSocket, &QLocalSocket::deleteLater);

    QObject::connect(nextConnSocket, &QLocalSocket::destroyed, this,
        [nextConnSocket, this]() {
            connectionMap.remove(nextConnSocket);
        }
    );

    // Handle incoming messages
    QObject::connect(nextConnSocket, &QLocalSocket::readyRead, this,
        [nextConnSocket, this]() {
            connectionMap[nextConnSocket].coder->receiveMessage();
        }
    );
}

void SingleApplicationPrivate::addAppData(const QString &data)
{
    appDataList.push_back(data);
}

QStringList SingleApplicationPrivate::appData() const
{
    return appDataList;
}
