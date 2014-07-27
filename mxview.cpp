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
#include <QToolButton>
#include <QStyle>


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
    QToolBar * myToolBar = new QToolBar();
    this->addToolBar(myToolBar);

    QToolButton *backButton = new QToolButton;
    QToolButton *fwdButton = new QToolButton;
    QToolButton *reloadButton = new QToolButton;

    backButton->setIcon(style()->standardIcon(QStyle::SP_ArrowLeft));
    backButton->setToolTip("Go Back");
    myToolBar->addWidget(backButton);

    fwdButton->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    fwdButton->setToolTip("Go Forward");
    myToolBar->addWidget(fwdButton);

    reloadButton->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    reloadButton->setToolTip("Reload");
    myToolBar->addWidget(reloadButton);

    myToolBar->hide();

    // resize main window
    this->resize(width, height);

    // center main window
    QRect screenGeometry = QApplication::desktop()->screenGeometry();
    int x = (screenGeometry.width()-this->width()) / 2;
    int y = (screenGeometry.height()-this->height()) / 2;
    this->move(x, y);

    // set title
    this->setWindowTitle(title);

    // set webview
    QWebView *webview = new QWebView(this);    
    this->setCentralWidget(webview);
    webview->load(QUrl(url));
    webview->show();

    // connect buttons
    connect(webview, SIGNAL(loadStarted()), myToolBar, SLOT(show()));
    connect(backButton, SIGNAL(clicked()), webview, SLOT(back()));
    connect(fwdButton, SIGNAL(clicked()), webview, SLOT(forward()));
    connect(reloadButton, SIGNAL(clicked()), webview, SLOT(reload()));
}
