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


#include "mainwindow.h"
#include "version.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QToolBar>
#include <QKeyEvent>

//#include <QDebug>

MainWindow::MainWindow(QString url, QString title, QWidget *parent)
    : QMainWindow(parent)
{
    qDebug() << "Program Version:" << VERSION;
    timer = new QTimer(this);

    searchBox = new QLineEdit(this);
    searchBox->setPlaceholderText(tr("search"));
    searchBox->setClearButtonEnabled(true);
    searchBox->setMaximumWidth(150);
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::findInPage);
    connect(searchBox, &QLineEdit::returnPressed, this, &MainWindow::findInPage);

    toolBar = new QToolBar(this);
    webview = new QWebEngineView(this);

    displaySite(url, title);
}

MainWindow::~MainWindow()
{
    QSettings settings("mx-viewer");
    settings.setValue("geometry", saveGeometry());
}

// pop up a window and display website
void MainWindow::displaySite(QString url, QString title)
{

    int width = 800;
    int height = 500;

    this->resize(width, height);

    QSettings settings("mx-viewer");
    restoreGeometry(settings.value("geometry").toByteArray());

    if (this->isMaximized()) {
        this->resize(width, height);
    }

    this->addToolBar(toolBar);

    this->setCentralWidget(webview);
    webview->load(QUrl::fromUserInput(url));
    webview->show();


    toolBar->addAction(webview->pageAction(QWebEnginePage::Back));
    toolBar->addAction(webview->pageAction(QWebEnginePage::Forward));
    toolBar->addAction(webview->pageAction(QWebEnginePage::Reload));
    toolBar->addAction(webview->pageAction(QWebEnginePage::Stop));

    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    toolBar->addWidget(spacer);

    toolBar->addWidget(searchBox);

    toolBar->show();

    loading(); // display loading progressBar

    // center main window
    QRect screenGeometry = QApplication::desktop()->screenGeometry();
    int x = (screenGeometry.width()-this->width()) / 2;
    int y = (screenGeometry.height()-this->height()) / 2;
    this->move(x, y);

    // set title
    this->setWindowTitle(title);

    // show toolbar when new page is loaded
    connect(webview, &QWebEngineView::loadStarted, toolBar, &QToolBar::show);
    connect(webview, &QWebEngineView::loadStarted, this, &MainWindow::loading);
    connect(webview, &QWebEngineView::loadFinished, this, &MainWindow::done);
}

void MainWindow::search()
{
    searchBox->setFocus();
    findInPage();
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
    if (event->matches(QKeySequence::Find) || event->key() == Qt::Key_F3) {
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
void MainWindow::resizeEvent()
{
    progressBar->move(this->geometry().width()/2 - progressBar->width()/2, this->geometry().height() - 40);
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
    disconnect(timer, &QTimer::timeout, 0, 0);
    connect(timer, &QTimer::timeout, this, &MainWindow::procTime);
}

// done loading
void MainWindow::done(bool)
{
    qDebug() << "done loading";
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

