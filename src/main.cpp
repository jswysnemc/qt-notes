#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>
#include <QMessageBox>
#include <QString>
#include <QStandardPaths>
#include <QTranslator>

#include "app/applicationcontroller.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QLocale::setDefault(QLocale(QLocale::Chinese, QLocale::China));
    QApplication::setOrganizationName(QStringLiteral("snemc"));
    QApplication::setOrganizationDomain(QStringLiteral("local.snemc"));
    QApplication::setApplicationName(QStringLiteral("qt-notes"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    QTranslator qtBaseTranslator;
    if (qtBaseTranslator.load(QLocale(QLocale::Chinese, QLocale::China),
                              QStringLiteral("qtbase"),
                              QStringLiteral("_"),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtBaseTranslator);
    }

    const QString desktopFile =
        QStandardPaths::locate(QStandardPaths::ApplicationsLocation,
                               QStringLiteral("qt-notes.desktop"));
    if (!desktopFile.isEmpty()) {
        QGuiApplication::setDesktopFileName(QFileInfo(desktopFile).baseName());
    }

    ApplicationController controller;
    QString error;
    if (!controller.initialize(&error)) {
        qCritical() << "qt-notes initialize failed:" << error;
        QMessageBox::critical(nullptr,
                              QStringLiteral("qt-notes"),
                              QStringLiteral("初始化失败：%1").arg(error));
        return 1;
    }

    controller.start();
    return app.exec();
}
