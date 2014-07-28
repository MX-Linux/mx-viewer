/*****************************************************************************
 * mxview.cpp
 *****************************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MEPIS Community <http://forum.mepiscommunity.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/


#include "mxview.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QWebView>
#include <QToolBar>

mxview::mxview(QString url, QString title, QWidget *parent)
    : QMainWindow(parent)
{
    displaySite(url, title);
}

mxview::~mxview()
{

}

// pop up a window and display website
void mxview::displaySite(QString url, QString title) {
    int width = 800;
    int height = 500;

    // set toolbar
    QToolBar *toolBar = new QToolBar();
    this->addToolBar(toolBar);

    // set webview
    QWebView *webview = new QWebView(this);
    this->setCentralWidget(webview);
    webview->load(QUrl(url));
    webview->show();

    toolBar->addAction(webview->pageAction(QWebPage::Back));
    toolBar->addAction(webview->pageAction(QWebPage::Forward));
    toolBar->addAction(webview->pageAction(QWebPage::Reload));

    toolBar->hide();

    // resize main window
    this->resize(width, height);

    // center main window
    QRect screenGeometry = QApplication::desktop()->screenGeometry();
    int x = (screenGeometry.width()-this->width()) / 2;
    int y = (screenGeometry.height()-this->height()) / 2;
    this->move(x, y);

    // set title
    this->setWindowTitle(title);

    // connect buttons
    connect(webview, SIGNAL(loadStarted()), toolBar, SLOT(show()));
}
