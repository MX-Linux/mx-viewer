/*****************************************************************************
 * mxview.h
 *****************************************************************************
 * Copyright (C) 2022 MX Authors
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

#ifndef MXVIEW_H
#define MXVIEW_H

#include <QCommandLineParser>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressBar>
#include <QSettings>
#include <QTimer>
#include <QtWebEngineWidgets/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>

class MainWindow : public QMainWindow
{
    Q_OBJECT

protected:
    QTimer *timer;
    void keyPressEvent(QKeyEvent *event);
    void resizeEvent(QResizeEvent *event);

public:
    MainWindow(const QCommandLineParser &arg_parser, QWidget *parent = nullptr);
    ~MainWindow();
    void addActions();
    void addToolbar();
    void centerWindow();
    void displaySite(QString url = {}, const QString &title = {});
    void loadSettings();
    void openBrowseDialog();
    void openDialog();
    void openQuickInfo();
    void setConnections();
    void toggleFullScreen();
    void updateUrl();

public slots:
    void findBackward();
    void findForward();
    void loading();
    void done();
    void procTime();

private:
    QLineEdit *searchBox;
    QMetaObject::Connection conn;
    QProgressBar *progressBar;
    QSettings settings;
    QString homeAddress;
    QToolBar *toolBar;
    QWebEngineSettings *websettings;
    QWebEngineView *webview;
    bool browserMode{};
    bool showProgress{};
    const QCommandLineParser &args;
};


#endif // MXVIEW_H
