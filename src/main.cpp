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
    QApplication::setOrganizationName(QStringLiteral("snemc"));
    QApplication::setOrganizationDomain(QStringLiteral("local.snemc"));
    QApplication::setApplicationName(QStringLiteral("qt-notes"));
    QApplication::setStyle(QStringLiteral("Fusion"));

    const QLocale locale = QLocale::system();

    QTranslator qtBaseTranslator;
    if (qtBaseTranslator.load(locale,
                              QStringLiteral("qtbase"),
                              QStringLiteral("_"),
                              QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        app.installTranslator(&qtBaseTranslator);
    }

    QTranslator appTranslator;
    const QStringList searchPaths = {
        QStandardPaths::locate(QStandardPaths::AppDataLocation,
                               QStringLiteral("translations"),
                               QStandardPaths::LocateDirectory),
        QStringLiteral(":/i18n"),
    };
    for (const QString &path : searchPaths) {
        if (appTranslator.load(locale, QStringLiteral("qt-notes"), QStringLiteral("_"), path)) {
            app.installTranslator(&appTranslator);
            break;
        }
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
                              QCoreApplication::translate("main", "Initialization failed: %1").arg(error));
        return 1;
    }

    controller.start();
    return app.exec();
}
