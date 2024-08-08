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

#include "message_coder.h"
#include <QDebug>
#include <QIODevice>

MessageCoder::MessageCoder(QLocalSocket *socket)
    : socket(socket), dataStream(socket)
{
    connect(socket, &QLocalSocket::readyRead, this, &MessageCoder::slotDataAvailable);
    connect(socket, &QLocalSocket::aboutToClose, this, [socket, this]() {
        if (socket->bytesAvailable() > 0)
            slotDataAvailable();
    });
}

void MessageCoder::slotDataAvailable()
{
    qDebug() << "slotDataAvailable()";
    struct {
        quint8 magicNumber0;
        quint8 magicNumber1;
        quint8 magicNumber2;
        quint8 magicNumber3;
        quint32 protocolVersion;
        SingleApplication::MessageType type;
        quint16 instanceId;
        qsizetype length;
        QByteArray content;
        quint16 checksum;
    } msg;

    while (socket->bytesAvailable() > 0) {
        dataStream.startTransaction();

        dataStream >> msg.magicNumber0;
        if (msg.magicNumber0 != 0x00) {
            dataStream.abortTransaction();
            continue;
        }
        dataStream >> msg.magicNumber1;
        if (msg.magicNumber1 != 0x01) {
            dataStream.abortTransaction();
            continue;
        }
        dataStream >> msg.magicNumber2;
        if (msg.magicNumber2 != 0x00) {
            dataStream.abortTransaction();
            continue;
        }
        dataStream >> msg.magicNumber3;
        if (msg.magicNumber3 != 0x02) {
            dataStream.abortTransaction();
            continue;
        }

        dataStream >> msg.protocolVersion;
        if (msg.protocolVersion > 0x00000001) {
            dataStream.abortTransaction();
            continue;
        }

        dataStream >> msg.type;
        switch (msg.type) {
            case SingleApplication::MessageType::Acknowledge:
            case SingleApplication::MessageType::NewInstance:
            case SingleApplication::MessageType::InstanceMessage:
                break;
            default:
                dataStream.abortTransaction();
                continue;
        }

        dataStream >> msg.instanceId;
        dataStream >> msg.length;
        if (msg.length > 1024 * 1024) {
            dataStream.abortTransaction();
            continue;
        }
        msg.content = QByteArray(msg.length, Qt::Uninitialized);
        int bytesRead = dataStream.readRawData(msg.content.data(), msg.length);
        if (bytesRead == -1) {
            switch (dataStream.status()) {
                case QDataStream::ReadPastEnd:
                    dataStream.rollbackTransaction();
                    break;
                case QDataStream::ReadCorruptData:
                    dataStream.abortTransaction();
                    break;
                default:
                    qWarning() << "Unexpected QDataStream status after readRawData:" << dataStream.status();
                    dataStream.abortTransaction();
                    break;
            }
            continue;
        } else if (bytesRead != msg.length) {
            switch (dataStream.status()) {
                case QDataStream::Ok:
                    dataStream.abortTransaction();
                    break;
                case QDataStream::ReadPastEnd:
                    dataStream.rollbackTransaction();
                    break;
                case QDataStream::ReadCorruptData:
                    dataStream.abortTransaction();
                    break;
                default:
                    qWarning() << "Unexpected QDataStream status in message length validation:" << dataStream.status();
                    dataStream.abortTransaction();
                    break;
            }
            continue;
        }

        dataStream >> msg.checksum;
        switch (dataStream.status()) {
            case QDataStream::Ok:
                break;
            case QDataStream::ReadPastEnd:
                dataStream.rollbackTransaction();
                break;
            case QDataStream::ReadCorruptData:
                dataStream.abortTransaction();
                break;
            default:
                qWarning() << "Unexpected QDataStream status:" << dataStream.status();
                dataStream.abortTransaction();
                break;
        }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const quint16 computedChecksum = qChecksum(QByteArray(msg.content.constData(), static_cast<quint32>(msg.content.length())));
#else
        const quint16 computedChecksum = qChecksum(msg.content.constData(), static_cast<quint32>(msg.content.length()));
#endif

        if (msg.checksum != computedChecksum) {
            dataStream.abortTransaction();
            continue;
        }

        if (dataStream.commitTransaction()) {
            qDebug() << "Message received:" << msg.type << msg.instanceId << msg.content;
            emit messageReceived(SingleApplication::Message{
                .type = msg.type,
                .instanceId = msg.instanceId,
                .content = QByteArray(msg.content)
            });
        }
    }
}

bool MessageCoder::sendMessage(SingleApplication::MessageType type, quint16 instanceId, QByteArray content)
{
    qDebug() << "sendMessage()";
    if (content.size() > 1024 * 1024) {
        qWarning() << "Message content size exceeds maximum allowed size of 1MiB";
        return false;
    }

#if (QT_VERSION >= QT_VERSION_CHECK(6, 6, 0))
    dataStream.setVersion(QDataStream::Qt_6_6);
#elif (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    dataStream.setVersion(QDataStream::Qt_6_0);
#else
    dataStream.setVersion(QDataStream::QDataStream::Qt_5_15);
#endif

    dataStream << 0x00010002; // Magic number
    dataStream << (quint32)0x00000001; // Protocol version
    dataStream << static_cast<quint8>(type); // Message type
    dataStream << instanceId; // Instance ID
    dataStream << (qsizetype)content.size();
    dataStream.writeRawData(content.constData(), content.length());
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    quint16 checksum = qChecksum(QByteArray(content.constData(), static_cast<quint32>(content.length())));
#else
    quint16 checksum = qChecksum(content.constData(), static_cast<quint32>(content.length()));
#endif
    dataStream << checksum;

    return dataStream.status() == QDataStream::Ok;
}