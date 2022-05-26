/*****************************************************************************
 * mxview.h
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

#ifndef MXVIEW_H
#define MXVIEW_H

#include <QCommandLineParser>
#include <QLineEdit>
#include <QMainWindow>
#include <QProgressBar>
#include <QSettings>
#include <QTimer>
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
    void addToolbar();
    void centerWindow();
    void displaySite(QString url, QString title);
    void openBrowseDialog();
    void openDialog();
    void openQuickInfo();

public slots:
    void search();
    void findInPage();
    void loading();
    void done(bool);
    void procTime();

private:
    QLineEdit *searchBox;
    QProgressBar *progressBar;
    QSettings settings;
    QString word;
    QToolBar *toolBar;
    QWebEngineView *webview;
    QMetaObject::Connection conn;

};


#endif // MXVIEW_H
