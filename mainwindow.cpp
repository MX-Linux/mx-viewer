/*****************************************************************************
 * mxview.cpp
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

#include "mainwindow.h"
#include "version.h"

#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMessageBox>
#include <QScreen>
#include <QToolBar>

#include <addressbar.h>

MainWindow::MainWindow(const QCommandLineParser &arg_parser, QWidget *parent)
    : QMainWindow(parent), args{arg_parser}
{
    timer = new QTimer(this);
    toolBar = new QToolBar(this);
    webview = new QWebEngineView(this);
    progressBar = new QProgressBar(this);
    searchBox = new QLineEdit(this);
    websettings = webview->settings();

    loadSettings();
    addToolbar();
    addActions();
    setConnections();

    if (arg_parser.isSet(QStringLiteral("full-screen"))) {
        this->showFullScreen();
        toolBar->hide();
    }
    QString url;
    QString title;
    if (!args.positionalArguments().isEmpty()) {
        url = args.positionalArguments().at(0);
        title = (args.positionalArguments().size() > 1) ? args.positionalArguments().at(1) : url;
    }
    displaySite(url, title);
}

MainWindow::~MainWindow()
{
    settings.setValue(QStringLiteral("geometry"), saveGeometry());
}

void MainWindow::addActions()
{
    auto *full = new QAction(tr("Full screen"));
    full->setShortcut(Qt::Key_F11);
    this->addAction(full);
    connect(full, &QAction::triggered, this, &MainWindow::toggleFullScreen);
}

void MainWindow::addToolbar()
{
    this->addToolBar(toolBar);
    this->setCentralWidget(webview);

    QAction *back = nullptr;
    QAction *forward = nullptr;
    QAction *reload = nullptr;
    QAction *stop = nullptr;
    QAction *tab = nullptr;
    QAction *home = nullptr;

    toolBar->addAction(back = webview->pageAction(QWebEnginePage::Back));
    toolBar->addAction(forward = webview->pageAction(QWebEnginePage::Forward));
    toolBar->addAction(reload = webview->pageAction(QWebEnginePage::Reload));
    toolBar->addAction(stop = webview->pageAction(QWebEnginePage::Stop));
    toolBar->addAction(home = new QAction(QIcon::fromTheme(QStringLiteral("go-home")), tr("Home")));
    back->setShortcut(QKeySequence::Back);
    forward->setShortcut(QKeySequence::Forward);
    home->setShortcut(Qt::CTRL + Qt::Key_H);
    reload->setShortcuts(QKeySequence::Refresh);
    stop->setShortcut(QKeySequence::Cancel);
    tab = webview->pageAction(QWebEnginePage::OpenLinkInNewTab);
    tab->setShortcut(Qt::CTRL + Qt::Key_T);
    connect(stop, &QAction::triggered, this, &MainWindow::done);
    connect(home, &QAction::triggered, [this]() {displaySite();});

    searchBox->setPlaceholderText(tr("search in page"));
    searchBox->setClearButtonEnabled(true);
    searchBox->setMaximumWidth(150);
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::findForward);
    connect(searchBox, &QLineEdit::returnPressed, this, &MainWindow::findForward);
    if (!browserMode) {
        auto *spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toolBar->addWidget(spacer);
    } else {
        auto *addressBar = new AddressBar(this);
        addressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(addressBar, &QLineEdit::returnPressed, [this, addressBar]() { displaySite(addressBar->text()); });
        toolBar->addWidget(addressBar);
    }
    toolBar->addWidget(searchBox);
    toolBar->show();
}

void MainWindow::openBrowseDialog()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select file to open"),
                                                QDir::homePath(), tr("Hypertext Files (*.htm *.html);;All Files (*.*)"));
    if (QFileInfo::exists(file)) {
        displaySite(file, file);
    }
}

// pop up a window and display website
void MainWindow::displaySite(QString url, const QString &title)
{
    if (url.isEmpty()) {
        url = homeAddress;
    }
    disconnect(conn);
    conn = connect(webview, &QWebEngineView::loadFinished, [url](bool ok) { if (!ok) { qDebug() << "Error :" << url;
        }});
    QUrl qurl = QUrl::fromUserInput(url);
    webview->setUrl(qurl);
    webview->load(qurl);
    webview->show();
    showProgress ? loading() : progressBar->hide();
    this->setWindowTitle(title);
}

void MainWindow::loadSettings()
{
    // Load first from system .conf file and then overwrite with CLI switches where available
    websettings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);

    homeAddress = settings.value(QStringLiteral("Home"), "https://duckduckgo.com").toString();
    browserMode = settings.value(QStringLiteral("BrowserMode"), false).toBool();
    showProgress = settings.value(QStringLiteral("ShowProgressBar"), false).toBool();

    websettings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, settings.value(QStringLiteral("SpatialNavigation"), false).toBool());
    websettings->setAttribute(QWebEngineSettings::JavascriptEnabled, !settings.value(QStringLiteral("DisableJava"), false).toBool());
    websettings->setAttribute(QWebEngineSettings::AutoLoadImages, settings.value(QStringLiteral("LoadImages"), true).toBool());

    if (args.isSet(QStringLiteral("enable-spatial-navigation"))) {
        websettings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, true);
    }
    if (args.isSet(QStringLiteral("disable-js"))) {
        websettings->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    }
    if (args.isSet(QStringLiteral("disable-images"))) {
        websettings->setAttribute(QWebEngineSettings::AutoLoadImages, false);
    }
    if (args.isSet(QStringLiteral("browser-mode"))) {
        browserMode = true;
    }

    QSize size {800, 500};
    this->resize(size);
    if (settings.contains(QStringLiteral("geometry")) && !args.isSet(QStringLiteral("full-screen"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry")).toByteArray());
        if (this->isMaximized()) { // add option to resize if maximized
            this->resize(size);
            centerWindow();
        }
    } else {
        centerWindow();
    }
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
    bool ok = false;
    QString url  = QInputDialog::getText(this, tr("Open"),
                                         tr("Enter site or file URL:"), QLineEdit::Normal, QString(), &ok);
    if (ok && !url.isEmpty()) {
        displaySite(url, url);
    }
}

void MainWindow::openQuickInfo()
{
    QMessageBox::about(this, tr("Keyboard Shortcuts"),
                       tr("Ctrl-F, or F3") + "\t - " + tr("Find") + "\n" +
                       tr("Shift-F3") + "\t - " + tr("Find previous") + "\n" +
                       tr("Ctrl-R, or F5") + "\t - " + tr("Reload") + "\n" +
                       tr("Ctrl-O") + "\t - " + tr("Open URL") + "\n" +
                       tr("Ctrl-B") + "\t - " + tr("Browse file to open") + "\n" +
                       tr("Esc") + "\t - " + tr("Stop loading/clear Find field") + "\n" +
                       tr("Alt-LeftArrow, Alt-RightArrow") + " - " + tr("Back/Forward") + "\n" +
                       tr("F1, or ?") + "\t - " + tr("Open this help dialog"));
}

void MainWindow::setConnections()
{
    connect(webview, &QWebEngineView::loadStarted, toolBar, &QToolBar::show); // show toolbar when loading a new page
    connect(webview, &QWebEngineView::urlChanged, this, &MainWindow::updateUrl);
    if (showProgress) {
        connect(webview, &QWebEngineView::loadStarted, this, &MainWindow::loading);
    }
    connect(webview, &QWebEngineView::loadFinished, this, &MainWindow::done);
}

void MainWindow::toggleFullScreen()
{
    if (this->isFullScreen()) {
        this->showNormal();
        toolBar->show();
    } else {
        this->showFullScreen();
        toolBar->hide();
    }
}

void MainWindow::updateUrl()
{
    if (browserMode) {
        auto *addressBar = toolBar->findChild<QLineEdit *>();
        addressBar->show();
        addressBar->setText(webview->url().toDisplayString());
    }
}

void MainWindow::findBackward()
{
    searchBox->setFocus();
    webview->findText(searchBox->text(), QWebEnginePage::FindBackward);
}

void MainWindow::findForward()
{
    searchBox->setFocus();
    webview->findText(searchBox->text());
}

// process keystrokes
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    const auto step = 0.1;
    auto zoom = webview->zoomFactor();
    if (event->matches(QKeySequence::FindNext) || event->matches(QKeySequence::Find) || event->key() == Qt::Key_Slash) {
        findForward();
    }
    if (event->matches(QKeySequence::FindPrevious)) {
        findBackward();
    } else if (event->key() == Qt::Key_Plus) {
        webview->setZoomFactor(zoom + step);
    } else if (event->key() == Qt::Key_Minus) {
        webview->setZoomFactor(zoom - step);
    } else if (event->key() == Qt::Key_0) {
        webview->setZoomFactor(1);
    } else if (event->matches(QKeySequence::Open)) {
        openDialog();
    } else if (event->key() == Qt::Key_B && QApplication::keyboardModifiers() & Qt::ControlModifier) {
        openBrowseDialog();
    } else if (event->key() == Qt::Key_Question || event->matches(QKeySequence::HelpContents)) {
        openQuickInfo();
    } else if (event->matches(QKeySequence::Cancel) && !searchBox->text().isEmpty() && searchBox->hasFocus()) {
        searchBox->clear();
    } else if (event->matches(QKeySequence::Cancel) && searchBox->text().isEmpty()) {
        webview->setFocus();
    } else if (event->key() == Qt::Key_L && QApplication::keyboardModifiers() & Qt::ControlModifier) {
        toolBar->findChild<QLineEdit *>()->setFocus();
    }
}

// resize event
void MainWindow::resizeEvent(QResizeEvent* /*event*/)
{
    if (showProgress) {
        progressBar->move(this->geometry().width() / 2 - progressBar->width() / 2, this->geometry().height() - 40);
    }
}

// display progressbar while loading page
void MainWindow::loading()
{
    progressBar->setFixedHeight(20);
    progressBar->setTextVisible(false);
    progressBar->move(this->geometry().width() / 2 - progressBar->width() / 2, this->geometry().height() - 40);
    progressBar->setFocus();
    progressBar->show();
    timer->start(progressBar->maximum());
    connect(timer, &QTimer::timeout, this, &MainWindow::procTime);
}

// done loading
void MainWindow::done()
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
    const int step = 5;
    progressBar->setValue((progressBar->value() + step) % progressBar->maximum());
}

