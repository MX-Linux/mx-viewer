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
#include <QDebug>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QScreen>
#include <QStyle>
#include <QToolBar>
#include <QtWebEngineWidgets/QWebEngineSettings>

MainWindow::MainWindow(const QCommandLineParser &arg_parser, QWidget *parent)
    : QMainWindow(parent)
{
    timer = new QTimer(this);
    toolBar = new QToolBar(this);
    webview = new QWebEngineView(this);
    progressBar = new QProgressBar(this);
    searchBox = new QLineEdit(this);

    addToolbar();

    QWebEngineSettings *websettings = webview->settings();
    websettings->setAttribute(QWebEngineSettings::JavascriptEnabled, !arg_parser.isSet("disable-js"));
    websettings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, arg_parser.isSet("enable-spatial-navigation"));


    QEvent palevent(QEvent::PaletteChange);
    qApp->sendEvent(this, &palevent);

    QString url, title;
    if (!arg_parser.positionalArguments().isEmpty()) {
        url = arg_parser.positionalArguments().at(0);
        title = (arg_parser.positionalArguments().size() > 1) ? arg_parser.positionalArguments().at(1) : url;
    } else {
       url = "https://duckduckgo.com";
       title = "DuckDuckGo";
    }

    displaySite(url, title);
}

MainWindow::~MainWindow()
{
    settings.setValue("geometry", saveGeometry());
}

void MainWindow::addToolbar()
{
    this->addToolBar(toolBar);
    this->setCentralWidget(webview);

    QAction *back;
    QAction *forward;
    QAction *reload;
    QAction *stop;

    toolBar->addAction(back = webview->pageAction(QWebEnginePage::Back));
    back->setShortcut(QKeySequence::Back);
    toolBar->addAction(forward = webview->pageAction(QWebEnginePage::Forward));
    forward->setShortcut(QKeySequence::Forward);
    toolBar->addAction(reload = webview->pageAction(QWebEnginePage::Reload));
    reload->setShortcuts(QList<QKeySequence>({QKeySequence::Refresh, QKeySequence(tr("Ctrl+R"))}));
    toolBar->addAction(stop = webview->pageAction(QWebEnginePage::Stop));
    stop->setShortcut(Qt::Key_Escape);

    searchBox->setPlaceholderText(tr("search"));
    searchBox->setClearButtonEnabled(true);
    searchBox->setMaximumWidth(150);
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::findInPage);
    connect(searchBox, &QLineEdit::returnPressed, this, &MainWindow::findInPage);

    QWidget* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    toolBar->addWidget(spacer);
    toolBar->addWidget(searchBox);
    toolBar->show();

    // show toolbar when new page is loaded
    connect(webview, &QWebEngineView::loadStarted, toolBar, &QToolBar::show);
    connect(webview, &QWebEngineView::loadStarted, this, &MainWindow::loading);
    connect(webview, &QWebEngineView::loadFinished, this, &MainWindow::done);
}

void MainWindow::openBrowseDialog()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select file to open"),
                                                QDir::homePath(), tr("Hypertext Files (*.htm *.html);;All Files (*.*)"));
    if (QFileInfo::exists(file)) displaySite(file, file);
}

// pop up a window and display website
void MainWindow::displaySite(QString url, QString title)
{
    QSize size {800, 500};
    this->resize(size);

    if (settings.contains("geometry")) {
        restoreGeometry(settings.value("geometry").toByteArray());
        if (this->isMaximized()) { // add option to resize if maximized
            this->resize(size);
            centerWindow();
        }
    } else {
        centerWindow();
    }

    disconnect(conn);
    conn = connect(webview, &QWebEngineView::loadFinished, [url](bool ok) { if (!ok) qDebug() << "Error loading:" << url; });
    webview->load(QUrl::fromUserInput(url));
    webview->show();

    loading(); // display loading progressBar
    this->setWindowTitle(title);
}

// center main window
void MainWindow::centerWindow()
{
    QRect screenGeometry = QApplication::primaryScreen()->geometry();
    int x = (screenGeometry.width() - this->width()) / 2;
    int y = (screenGeometry.height() - this->height()) / 2;
    this->move(x, y);
}

void MainWindow::openDialog()
{
    bool ok;
    QString url  = QInputDialog::getText(this, tr("Open"),
                                         tr("Enter site or file URL:"), QLineEdit::Normal, QString(), &ok);
    if (ok && !url.isEmpty()) displaySite(url, url);
}

void MainWindow::openQuickInfo()
{
    QMessageBox::about(this, tr("Keyboard Shortcuts"),
                       tr("Ctrl-F, or F3") + " - " + tr("Find") + "\n" +
                       tr("Ctrl-R, or F5") + " - " + tr("Reload") + "\n" +
                       tr("Ctrl-O") + " - " + tr("Open URL") + "\n" +
                       tr("Ctrl-B") + " - " + tr("Browse file to open") + "\n" +
                       tr("Esc - Stop loading/clear Find field") + "\n" +
                       tr("Alt-LeftArrow, Alt-RightArrow") + " - " + tr("Back/Forward") + "\n" +
                       tr("F1, or ?") + " - " + tr("Open this help dialog"));
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
    auto zoom = webview->zoomFactor();
    if (event->matches(QKeySequence::Find) || event->key() == Qt::Key_F3)
        search();
    else if (event->key() == Qt::Key_Plus)
        webview->setZoomFactor(zoom + 0.1);
    else if (event->key() == Qt::Key_Minus)
        webview->setZoomFactor(zoom - 0.1);
    else if (event->key() == Qt::Key_0)
        webview->setZoomFactor(1);
    else if (event->matches(QKeySequence::Open))
        openDialog();
    else if (event->key() == Qt::Key_B && (QApplication::keyboardModifiers() & Qt::ControlModifier))
        openBrowseDialog();
    else if (event->key() == Qt::Key_Question || event->matches(QKeySequence::HelpContents))
        openQuickInfo();
}

// resize event
void MainWindow::resizeEvent(QResizeEvent*)
{
    progressBar->move(this->geometry().width() / 2 - progressBar->width() / 2, this->geometry().height() - 40);
}

// for colour scheme changes
//void MainWindow::changeEvent(QEvent *event)
//{
//    const QEvent::Type etype = event->type();
//    if (etype == QEvent::ApplicationPaletteChange || etype == QEvent::PaletteChange || etype == QEvent::StyleChange) {
//        const QPalette &pal = webview->style()->standardPalette();
//        QString css("body{background-color:" + pal.color(QPalette::Base).name()
//                    + ";color:" + pal.color(QPalette::WindowText).name() + "}"
//                    "a{color:" + pal.color(QPalette::Link).name() + "}"
//                    "a:visited{color:" + pal.color(QPalette::LinkVisited).name() + "}");
//        QUrl cssdata("data:text/css;charset=utf-8;base64,"
//                     + css.toUtf8().toBase64(), QUrl::StrictMode);
//        webview->settings()->setUserStyleSheetUrl(cssdata);
//    }
//}

// display progressbar while loading page
void MainWindow::loading()
{
    progressBar->setFixedHeight(20);
    progressBar->setTextVisible(false);
    progressBar->move(this->geometry().width() / 2 - progressBar->width() / 2, this->geometry().height() - 40);
    progressBar->setFocus();
    progressBar->show();
    timer->start(100);
    connect(timer, &QTimer::timeout, this, &MainWindow::procTime);
}

// done loading
void MainWindow::done(bool)
{
    timer->stop();
    timer->disconnect();
    webview->stop();
    webview->setFocus();
    searchBox->clear();
    progressBar->hide();
    this->setWindowTitle(webview->title());
}

// advance progressbar
void MainWindow::procTime()
{
    progressBar->setValue((progressBar->value() + 5) % 100);
}

