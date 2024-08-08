// serverthread.cpp
#include "serverthread.h"

ServerThread::ServerThread(const QString &serverName, QObject *parent)
    : QThread(parent), m_serverName(serverName), m_server(nullptr), m_quit(false)
{
}

ServerThread::~ServerThread()
{
    stop();
    wait();
}

void ServerThread::run()
{
    m_server = new QLocalServer(nullptr);
    
    if (!m_server->listen(m_serverName)) {
        emit error(m_server->errorString());
        delete m_server;
        return;
    }
    while (!m_quit) {
        if (m_server->waitForNewConnection(100)) {
            QLocalSocket *socket = m_server->nextPendingConnection();
            if (socket) {
                emit newConnection(socket);
            } else {
                emit error(m_server->errorString());
            }
        }
        
        m_mutex.lock();
        if (m_quit) {
            m_mutex.unlock();
            break;
        }
        m_mutex.unlock();
    }

    m_server->close();
    delete m_server;
}
            break;
        }
        m_mutex.unlock();
    }

    m_server->close();
    delete m_server;
}

void ServerThread::stop()
{
    QMutexLocker locker(&m_mutex);
    m_quit = true;
    m_condition.wakeOne();
}