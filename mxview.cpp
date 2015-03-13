/*****************************************************************************
 * mxview.cpp
 *****************************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MEPIS Community <http://forum.mepiscommunity.org>
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

//#include <QDebug>

mxview::mxview(QString url, QString title, QWidget *parent)
    : QMainWindow(parent)
{
    timer = new QTimer(this);
    searchBox = new QLineEdit(this);
    displaySite(url, title);
}

mxview::~mxview()
{

}

// pop up a window and display website
void mxview::displaySite(QString url, QString title)
{
    int width = 800;
    int height = 500;

    // set toolbar
    QToolBar *toolBar = new QToolBar();
    this->addToolBar(toolBar);

    // set webview
    webview = new QWebView(this);
    this->setCentralWidget(webview);
    webview->load(QUrl::fromUserInput(url));
    webview->show();

    toolBar->addAction(webview->pageAction(QWebPage::Back));
    toolBar->addAction(webview->pageAction(QWebPage::Forward));
    toolBar->addAction(webview->pageAction(QWebPage::Reload));
    toolBar->addAction(webview->pageAction(QWebPage::Stop));

    toolBar->hide();

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

void mxview::search()
{
    searchBox = new QLineEdit(this);
    searchBox->move(this->geometry().width() - 120,this->geometry().height() - 40);
    searchBox->setFocus();
    searchBox->show();
    connect(searchBox,SIGNAL(textChanged(QString)),this, SLOT(findInPage()));
    connect(searchBox,SIGNAL(returnPressed()),this, SLOT(findInPage()));
}

void mxview::findInPage()
{
    word = searchBox->text();
    webview->findText(word);
}

// process keystrokes
void mxview::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::Find))
        search();
    if (event->key() == Qt::Key_Escape)
        if (searchBox->isVisible())
            searchBox->hide();
}

// resize event
void mxview::resizeEvent(QResizeEvent *event)
{
    progressBar->move(this->geometry().width()/2 - progressBar->width()/2, this->geometry().height() - 40);
    searchBox->move(this->geometry().width() - 120,this->geometry().height() - 40);
}

// display progressbar while loading page
void mxview::loading()
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
void mxview::done(bool)
{
    progressBar->hide();
    timer->stop();
}

// advance progressbar
void mxview::procTime()
{
    int i = progressBar->value() + 5;
    if (i > 100) {
        i = 0;
    }
    progressBar->setValue(i);
}

