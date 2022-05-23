/*****************************************************************************
 * main.cpp
 *****************************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
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
 **********************************************************************/

#include <QApplication>
#include <QCommandLineParser>
#include <QLibraryInfo>
#include <QMessageBox>
#include <QTranslator>

#include "mainwindow.h"
#include <unistd.h>
#include <version.h>

static bool dropElevatedPrivileges()
{
    if (getuid() != 0 && geteuid() != 0) return true;

    //    initgroups()
    // ref:  https://www.safaribooksonline.com/library/view/secure-programming-cookbook/0596003943/ch01s03.html#secureprgckbk-CHP-1-SECT-3.3

    // change guid + uid   ~~  nobody (uid 65534), nogroup (gid 65534), users (gid 100)
    if (setgid(65534) != 0) EXIT_FAILURE;
    if (setuid(65534) != 0) EXIT_FAILURE;

    // On systems with defined _POSIX_SAVED_IDS in the unistd.h file, it should be
    // impossible to regain elevated privs after the setuid() call, above.  Test, try to regain elev priv:
    if (setuid(0) != -1) return false;   // and the calling fn should EXIT/abort the program
    // COULD also confirm that setregid() fails

    // change cwd, for good measure (if unable to, treat as overall failure)
    if (chdir("/tmp") != 0) return false;  // consider fprint()ing or logging the failure reason

    return true;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    //app.setWindowIcon(QIcon("/usr/share/pixmaps/mx-viewer.png"));
    app.setWindowIcon(QIcon::fromTheme(app.applicationName()));
    app.setApplicationVersion(VERSION);
    app.setOrganizationName("MX-Linux");

    QCommandLineParser parser;
    parser.setApplicationDescription(QObject::tr("This tool will display the URL content in a window, window title is optional."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"j", "disable-js"}, QObject::tr("Disable JavaScript")});
    parser.addOption({{"q", "disable-quirks"}, QObject::tr("Disable workaround for broken sites")});
    parser.addOption({{"s", "enable-spatial-navigation"}, QObject::tr("Enable spatial navigation with keyboard")});
    parser.addOption({{"3", "enable-cookies"}, QObject::tr("Enable 3rd party cookies")});
    parser.addPositionalArgument("URL", QObject::tr("URL of the page you want to load") + "\ne.g., https://google.com, google.com, file:///home/user/file.html");
    parser.addPositionalArgument("Title", QObject::tr("Window title for the viewer"), "[title]");
    parser.process(app);

    if (!dropElevatedPrivileges()) {
        qDebug() << "Could not drop elevated privileges";
        exit(EXIT_FAILURE);
    }

    QTranslator qtTran;
    if (qtTran.load(QLocale::system(), "qt", "_", QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTran);

    QTranslator qtBaseTran;
    if (qtBaseTran.load("qtbase_" + QLocale::system().name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtBaseTran);

    QTranslator appTran;
    if (appTran.load(app.applicationName() + "_" + QLocale::system().name(), "/usr/share/" + app.applicationName() + "/locale"))
        app.installTranslator(&appTran);

    MainWindow w(parser);
    w.show();

    return app.exec();
}
