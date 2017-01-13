/*****************************************************************************
 * mxview.cpp
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


#include "mxview.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QToolBar>
#include <QKeyEvent>

//#include <QWebSettings>
//#include <QDebug>

MainWindow::MainWindow(QString url, QString title, QWidget *parent)
    : QMainWindow(parent)
{
    timer = new QTimer(this);

    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText(tr("search"));
    searchBox->setClearButtonEnabled(true);
    searchBox->setMaximumWidth(130);

    toolBar = new QToolBar(this);
    webview = new QWebView(this);

    displaySite(url, title);
}

MainWindow::~MainWindow()
{

}

// pop up a window and display website
void MainWindow::displaySite(QString url, QString title)
{
    int width = 800;
    int height = 500;

    this->addToolBar(toolBar);

//    QWebSettings::globalSettings()->setAttribute(QWebSettings::PluginsEnabled, true);
//    webview->settings()->setAttribute(QWebSettings::PluginsEnabled, true);

    this->setCentralWidget(webview);
    webview->load(QUrl::fromUserInput(url));
    webview->show();


    toolBar->addAction(webview->pageAction(QWebPage::Back));
    toolBar->addAction(webview->pageAction(QWebPage::Forward));
    toolBar->addAction(webview->pageAction(QWebPage::Reload));
    toolBar->addAction(webview->pageAction(QWebPage::Stop));

    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    toolBar->addWidget(spacer);

    toolBar->addWidget(searchBox);

    toolBar->show();

    // resize main window
    this->resize(width, height);
    loading(); // display loading progressBar

    // center main window
    QRect screenGeometry = QApplication::desktop()->screenGeometry();
    int x = (screenGeometry.width()-this->width()) / 2;
    int y = (screenGeometry.height()-this->height()) / 2;
    this->move(x, y);

    // set title
    this->setWindowTitle(title);

    // show toolbar when new page is loaded
    connect(webview, SIGNAL(loadStarted()), toolBar, SLOT(show()));
    connect(webview, SIGNAL(loadStarted()), SLOT(loading()));
    connect(webview, SIGNAL(loadFinished(bool)), SLOT(done(bool)));
}

void MainWindow::search()
{
    //searchBox = new QLineEdit(this);
    //searchBox->move(this->geometry().width() - 120,this->geometry().height() - 40);
    searchBox->setFocus();
    searchBox->show();    
    connect(searchBox,SIGNAL(textChanged(QString)),this, SLOT(findInPage()));
    connect(searchBox,SIGNAL(returnPressed()),this, SLOT(findInPage()));
}

void MainWindow::findInPage()
{
    word = searchBox->text();
    webview->findText(word);
}

// process keystrokes
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    qreal zoom = webview->zoomFactor();
    if (event->matches(QKeySequence::Find)) {
        search();
    }
    if (event->key() == Qt::Key_Escape) {
        searchBox->clear();
        webview->setFocus();
    }
    if (event->key() == Qt::Key_Plus) {
        webview->setZoomFactor(zoom + 0.1);
    }
    if (event->key() == Qt::Key_Minus) {
        webview->setZoomFactor(zoom - 0.1);
    }
    if (event->key() == Qt::Key_0) {
        webview->setZoomFactor(1);
    }
}

// resize event
void MainWindow::resizeEvent(QResizeEvent *event)
{
    progressBar->move(this->geometry().width()/2 - progressBar->width()/2, this->geometry().height() - 40);
    //searchBox->move(this->geometry().width() - 120,this->geometry().height() - 40);
}

// display progressbar while loading page
void MainWindow::loading()
{
    progressBar = new QProgressBar(this);
    progressBar->setFixedHeight(20);
    progressBar->setTextVisible(false);
    progressBar->move(this->geometry().width()/2 - progressBar->width()/2, this->geometry().height() - 40);
    progressBar->setFocus();
    progressBar->show();
    timer->start(100);
    disconnect(timer, SIGNAL(timeout()), 0, 0);
    connect(timer, SIGNAL(timeout()), SLOT(procTime()));
}

// done loading
void MainWindow::done(bool)
{
    progressBar->hide();
    timer->stop();
}

// advance progressbar
void MainWindow::procTime()
{
    int i = progressBar->value() + 5;
    if (i > 100) {
        i = 0;
    }
    progressBar->setValue(i);
}

