// serverthread.h
#ifndef SERVERTHREAD_H
#define SERVERTHREAD_H

#include <QThread>
#include <QLocalServer>
#include <QMutex>
#include <QWaitCondition>

class ServerThread : public QThread
{
    Q_OBJECT

public:
    explicit ServerThread(const QString &serverName, QObject *parent = nullptr);
    ~ServerThread() override;

    void run() override;
    void stop();

signals:
    void newConnection(QLocalSocket *socket);
    void error(const QString &errorString);

private:
    QString m_serverName;
    QLocalServer *m_server;
    QMutex m_mutex;
    QWaitCondition m_condition;
    bool m_quit;
};

#endif // SERVERTHREAD_H