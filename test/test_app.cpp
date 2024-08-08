// test_app.cpp
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include "singleapplication.h"

int main(int argc, char *argv[])
{
    SingleApplication app(argc, argv);

    if (app.isSecondary()) {
        qDebug() << "Secondary instance - exiting";
        return 0;
    }

    qDebug() << "Primary instance - running";
    QTimer::singleShot(10000, &app, &QCoreApplication::quit);  // Run for 10 seconds

    return app.exec();
}