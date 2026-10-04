/*****************************************************************************
 * main.cpp
 *****************************************************************************
 * Copyright (C) 2022-2026 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
 *          MX Linux <http://mxlinux.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MX Viewer is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MX Viewer.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/

#include "historystore.h"
#include "mainwindow.h"
#include "singleinstance.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QGuiApplication>
#include <QDir>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QProcess>
#include <QStandardPaths>
#include <QTranslator>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#ifndef VERSION
    #define VERSION "?.?.?.?"
#endif

// Avoid the cost of Qt Quick's OpenGL rendering on software drivers.
bool openGLIsSoftwareRendered()
{
    QOpenGLContext context;
    QOffscreenSurface surface;
    surface.create();
    if (!context.create() || !context.makeCurrent(&surface)) {
        return false;
    }
    const auto *renderer = reinterpret_cast<const char *>(context.functions()->glGetString(GL_RENDERER));
    const QString name = QString::fromLatin1(renderer ? renderer : "");
    context.doneCurrent();
    return name.contains("llvmpipe") || name.contains("softpipe") || name.contains("SwiftShader")
           || name.contains("Software Rasterizer");
}

QPair<uint, uint> getUserIDs()
{
    QPair<uint, uint> id;
    QProcess proc;
    proc.start("logname", {}, QIODevice::ReadOnly);
    proc.waitForFinished();
    QString logname = QString::fromLatin1(proc.readAllStandardOutput().trimmed());
    if (proc.exitCode() != 0 || logname.isEmpty()) {
        qDebug() << "Failed to get logname, dropping privileges to nobody";
        return {0, 0};
    }
    proc.start("id", {"-u", logname}, QIODevice::ReadOnly);
    proc.waitForFinished();
    if (proc.exitCode() != 0) {
        qDebug() << "Failed to get uid for" << logname << ", dropping privileges to nobody";
        return {0, 0};
    }
    id.first = proc.readAllStandardOutput().trimmed().toUInt();
    proc.start("id", {"-g", logname}, QIODevice::ReadOnly);
    proc.waitForFinished();
    if (proc.exitCode() != 0) {
        qDebug() << "Failed to get gid for" << logname << ", dropping privileges to nobody";
        return {0, 0};
    }
    id.second = proc.readAllStandardOutput().trimmed().toUInt();
    return id;
}

// Drop rights of the program to regular user or 'nobody' if logname return root id or gid
// Used to drop rights to 'nobody', but normal user rights might be needed to write cache and cookies.
bool dropElevatedPrivileges(bool force_nobody)
{
    if (getuid() != 0 && geteuid() != 0) {
        return true;
    }

    // ref:
    // https://www.safaribooksonline.com/library/view/secure-programming-cookbook/0596003943/ch01s03.html#secureprgckbk-CHP-1-SECT-3.3
    auto [id, gid] = getUserIDs();
    constexpr int nobody = 65534; // nobody (uid 65534), nogroup (gid 65534)
    if (id == 0 || gid == 0 || force_nobody) {
        id = gid = nobody;
    }

    // Replace root's supplementary groups with the target user's groups (or none for 'nobody')
    // before changing the primary gid, while we still have the privilege to do so.
    const passwd *pw = (id == nobody) ? nullptr : getpwuid(id);
    if (pw ? initgroups(pw->pw_name, gid) != 0 : setgroups(0, nullptr) != 0) {
        return false;
    }
    if (setgid(gid) != 0) {
        return false;
    }
    if (setuid(id) != 0) {
        return false;
    }

    // On systems with defined _POSIX_SAVED_IDS in the unistd.h file, it should be
    // impossible to regain elevated privs after the setuid() call, above.  Test, try to regain elev priv:
    if (setuid(0) != -1 || seteuid(0) != -1 || setgid(0) != -1 || setegid(0) != -1) {
        return false; // and the calling fn should EXIT/abort the program
    }

    // Change working directory to /tmp for security and compatibility reasons.
    // After dropping privileges, the original cwd might not be accessible to the new user,
    // potentially causing issues with file operations or signals. /tmp is world-writable
    // and a standard location for temporary operations.
    if (chdir("/tmp") != 0) {
        qDebug() << "Can't change working directory to /tmp";
        return false;
    }
    return true;
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
        && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qWarning("mx-viewer: no display available (DISPLAY and WAYLAND_DISPLAY are both unset); "
                "a graphical session is required to run this program.");
        return EXIT_FAILURE;
    }

    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    QGuiApplication::setQuitOnLastWindowClosed(true);
    // Set Qt platform to XCB (X11) if not already set and we're in X11 environment
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        if (!qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")) {
            qputenv("QT_QPA_PLATFORM", "xcb");
        }
    }

    // Debian's hunspell-* packages ship Chromium-format dictionaries here; QtWebEngine
    // only looks next to the binary or in the Qt data path unless told otherwise.
    if (qEnvironmentVariableIsEmpty("QTWEBENGINE_DICTIONARIES_PATH") && QDir("/usr/share/hunspell-bdic").exists()) {
        qputenv("QTWEBENGINE_DICTIONARIES_PATH", "/usr/share/hunspell-bdic");
    }

    QApplication app(argc, argv);
    QApplication::setWindowIcon(QIcon::fromTheme(QApplication::applicationName()));
    QApplication::setApplicationVersion(VERSION);
    QApplication::setOrganizationName("MX-Linux");

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QObject::tr("This tool will display the URL content in a window, window title is optional"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"f", "full-screen"}, QObject::tr("Start program in full-screen mode")});
    parser.addOption({{"i", "disable-images"}, QObject::tr("Disable load images automatically from websites")});
    parser.addOption({{"j", "disable-js"}, QObject::tr("Disable JavaScript")});
    if (getuid() == 0 || geteuid() == 0) {
        parser.addOption(
            {{"n", "force-nobody"},
             QObject::tr("Drop program's rights to 'nobody'. By default, if run as root, the rights are "
                         "dropped to normal user. This option might provide additional protection, but the program "
                         "would not be able to write its cache and cookies to the user directory, so it might break "
                         "some functionality.")});
    }
    parser.addOption({{"s", "enable-spatial-navigation"}, QObject::tr("Enable spatial navigation with keyboard")});
    parser.addPositionalArgument(QObject::tr("URL"),
                                 QObject::tr("URL of the page you want to load")
                                     + "\ne.g., https://google.com, google.com, file:///home/user/file.html");
    parser.addPositionalArgument(QObject::tr("Title"), QObject::tr("Window title for the viewer"), "[title]");
    parser.process(app);

    const bool startedAsRoot = getuid() == 0 || geteuid() == 0;
    bool force_nobody = startedAsRoot ? parser.isSet("force-nobody") : false;
    if (!dropElevatedPrivileges(force_nobody)) {
        qDebug() << "Could not drop elevated privileges";
        exit(EXIT_FAILURE);
    }

    // Probe after dropping privileges and before the first Qt Quick window is created.
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND") && openGLIsSoftwareRendered()) {
        qputenv("QT_QUICK_BACKEND", "software");
    }

    QTranslator qtTran;
    if (qtTran.load(QLocale::system(), "qt", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtTran);
    }

    QTranslator qtBaseTran;
    if (qtBaseTran.load("qtbase_" + QLocale::system().name(), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(&qtBaseTran);
    }

    QTranslator appTran;
    QString localePath = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation).at(0) + "/" + QApplication::applicationName() + "/locale";
    if (appTran.load(QApplication::applicationName() + "_" + QLocale::system().name(), localePath)) {
        QApplication::installTranslator(&appTran);
    }

    // A plain launch (how links from other applications arrive) opens in the running browser.
    // Help-viewer style calls with a title or options always get their own window, and so does a
    // launch as root: after the privilege drop its environment (HOME, XDG_RUNTIME_DIR) is still root's.
    const bool plainLaunch
        = !startedAsRoot && parser.optionNames().isEmpty() && parser.positionalArguments().size() <= 1;
    if (plainLaunch) {
        if (SingleInstance::forward(parser.positionalArguments().value(0))) {
            return EXIT_SUCCESS;
        }
        SingleInstance::listen(&app, &MainWindow::openFromOtherInstance);
    }

    auto *window = new MainWindow(parser);
    window->show();

    // Ensure proper cleanup on application exit
    // Close every browser window (including private ones) so each saves its state
    QObject::connect(&app, &QApplication::aboutToQuit, [] {
        MainWindow::setQuitting();
        const auto widgets = QApplication::topLevelWidgets();
        for (auto *widget : widgets) {
            if (qobject_cast<MainWindow *>(widget) && !widget->isHidden()) {
                widget->close();
            }
        }
    });

    const int result = QApplication::exec();
    MainWindow::releaseSharedProfile();
    HistoryStore::close();
    return result;
}
