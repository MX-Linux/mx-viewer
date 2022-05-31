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

#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QScreen>
#include <QStatusBar>
#include <QToolBar>
#include <QWebEngineHistory>


#include <chrono>
#include "mainwindow.h"
#include "version.h"

using namespace std::chrono_literals;

MainWindow::MainWindow(const QCommandLineParser &arg_parser, QWidget *parent)
    : QMainWindow(parent), args{arg_parser}
{
    timer = new QTimer(this);
    timerHist = new QTimer(this);
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

void MainWindow::listHistory()
{
    history->clear();
    QAction *histItem {nullptr};
    auto *hist = webview->history();

    int start = (hist->items().size() > histMaxSize) ? hist->items().size() - histMaxSize : 0;
    for (int i = start; i < hist->items().size(); ++i) {
        auto item = hist->itemAt(i);
        history->addAction(histItem = new QAction(item.title()));
        histItem->setProperty("url", item.url());
        connect(histItem, &QAction::hovered, [this, histItem]() {
            timerHist->stop();
            disconnect(timerHist);
            QString url = histItem->property("url").toString();
            if (url.isEmpty()) {
                this->statusBar()->hide();
            } else {
                timerHist->start(3s);
                this->statusBar()->show();
                this->statusBar()->showMessage(url);
                connect(timerHist, &QTimer::timeout, [this]() {this->statusBar()->hide();});
            }
        });
        connect(histItem, &QAction::triggered, [this, histItem]() {
            QString url = histItem->property("url").toString();
            displaySite(url);
        });
    }
}

void MainWindow::addToolbar()
{
    this->addToolBar(toolBar);
    this->setCentralWidget(webview);

    QAction *back {nullptr};
    QAction *forward {nullptr};
    QAction *reload {nullptr};
    QAction *stop {nullptr};
//    QAction *tab {nullptr};
    QAction *home {nullptr};

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
//    tab = webview->pageAction(QWebEnginePage::OpenLinkInNewTab);
//    tab->setShortcut(Qt::CTRL + Qt::Key_T);
    connect(stop, &QAction::triggered, this, &MainWindow::done);
    connect(home, &QAction::triggered, [this]() {displaySite();});

    searchBox->setPlaceholderText(tr("search in page"));
    searchBox->setClearButtonEnabled(true);
    searchBox->setMaximumWidth(searchWidth);
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::findForward);
    connect(searchBox, &QLineEdit::returnPressed, this, &MainWindow::findForward);
    if (!browserMode) {
        auto *spacer = new QWidget(this);
        spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        toolBar->addWidget(spacer);
    } else {
        addressBar = new AddressBar(this);
        addressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(addressBar, &QLineEdit::returnPressed, [this]() { displaySite(addressBar->text()); });
        toolBar->addWidget(addressBar);
    }
    toolBar->addWidget(searchBox);
    toolBar->addAction(menuButton = new QAction(QIcon::fromTheme(QStringLiteral("open-menu")), tr("Settings")));
    menuButton->setShortcut(Qt::Key_F10);
    buildMenu();
    toolBar->show();
}

void MainWindow::openBrowseDialog()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select file to open"),
                                                QDir::homePath(), tr("Hypertext Files (*.htm *.html);;All Files (*.*)"));
    if (QFileInfo::exists(file))
        displaySite(file, file);
}

// pop up a window and display website
void MainWindow::displaySite(QString url, const QString &title)
{
    if (url.isEmpty())
        url = homeAddress;
    disconnect(conn);
    conn = connect(webview, &QWebEngineView::loadFinished, [url](bool ok) { if (!ok) qDebug() << "Error :" << url; });
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

    homeAddress = settings.value(QStringLiteral("Home"), QStringLiteral("https://duckduckgo.com")).toString();
    browserMode = settings.value(QStringLiteral("BrowserMode"), false).toBool();
    showProgress = settings.value(QStringLiteral("ShowProgressBar"), false).toBool();

    websettings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, settings.value(QStringLiteral("SpatialNavigation"), false).toBool());
    websettings->setAttribute(QWebEngineSettings::JavascriptEnabled, !settings.value(QStringLiteral("DisableJava"), false).toBool());
    websettings->setAttribute(QWebEngineSettings::AutoLoadImages, settings.value(QStringLiteral("LoadImages"), true).toBool());

    if (args.isSet(QStringLiteral("enable-spatial-navigation")))
        websettings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, true);
    if (args.isSet(QStringLiteral("disable-js")))
        websettings->setAttribute(QWebEngineSettings::JavascriptEnabled, false);
    if (args.isSet(QStringLiteral("disable-images")))
        websettings->setAttribute(QWebEngineSettings::AutoLoadImages, false);
    if (args.isSet(QStringLiteral("browser-mode")))
        browserMode = true;

    QSize size {defaultWidth, defaultHeight};
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
    if (ok && !url.isEmpty())
        displaySite(url, url);
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
    if (showProgress)
        connect(webview, &QWebEngineView::loadStarted, this, &MainWindow::loading);
    connect(webview, &QWebEngineView::loadFinished, this, &MainWindow::done);
    connect(webview->page(), &QWebEnginePage::linkHovered, [this](const QString &url) {
        if (url.isEmpty()) {
            this->statusBar()->hide();
        } else {
            this->statusBar()->show();
            this->statusBar()->showMessage(url);
        }
    });
}

void MainWindow::buildMenu()
{
    auto *menu = new QMenu(this);
    QAction *browsermode {nullptr};
    QAction *readermode {nullptr};
    QAction *fullscreen {nullptr};
    QAction *about {nullptr};
    QAction *quit {nullptr};
    QAction *help {nullptr};
    QAction *historyAction {nullptr};
    history = new QMenu(menu);

    menuButton->setMenu(menu);
    menu->addAction(readermode = new QAction(QIcon::fromTheme(QStringLiteral("text-editor-symbolic")), tr("&Reader mode")));
    menu->addAction(browsermode = new QAction(QIcon::fromTheme(QStringLiteral("web-browser")), tr("&Browser mode")));
    menu->addAction(fullscreen = new QAction(QIcon::fromTheme(QStringLiteral("view-fullscreen")), tr("&Full screen")));
    browsermode->setVisible(!browserMode);
    readermode->setVisible(browserMode);
    fullscreen->setVisible(!this->isFullScreen());
    menu->addAction(historyAction = new QAction(QIcon::fromTheme(QStringLiteral("history")), tr("H&istory")));
    historyAction->setMenu(history);
    menu->addSeparator();
    menu->addAction(help = new QAction(QIcon::fromTheme(QStringLiteral("help-contents")), tr("&Help")));
    menu->addAction(about = new QAction(QIcon::fromTheme(QStringLiteral("help-about")), tr("&About...")));
    menu->addSeparator();
    menu->addAction(quit  = new QAction(QIcon::fromTheme(QStringLiteral("window-close")), tr("&Exit")));

    connect(menuButton, &QAction::triggered, [this, menu]() {
        QPoint pos = mapToParent(toolBar->widgetForAction(menuButton)->pos());
        pos.setY(pos.y() + toolBar->widgetForAction(menuButton)->size().height());
        menu->popup(pos);
        listHistory();
    });

    connect(fullscreen, &QAction::triggered, this, &MainWindow::toggleFullScreen);
    connect(browsermode, &QAction::triggered, [this, browsermode, readermode]() {
        toolBar->findChild<QWidget *>()->hide();
        browserMode = !browserMode;
        browsermode->setVisible(!browserMode);
        readermode->setVisible(browserMode);
    });
    connect(readermode, &QAction::triggered, [this, browsermode, readermode]() {
        toolBar->findChild<QWidget *>()->hide();
        browserMode = !browserMode;
        browsermode->setVisible(!browserMode);
        readermode->setVisible(browserMode);
    });


    connect(help, &QAction::triggered, this, &MainWindow::openQuickInfo);
    connect(quit, &QAction::triggered, this, &MainWindow::close);
    connect(about, &QAction::triggered, [this]() {
        QMessageBox::about(this, tr("About MX Viewer"), tr(
"This is a VERY basic browser based on Qt WebEngine.\n\n"

"The main purpose is to provide a basic document viewer for MX documentation. "
"It could be used for LIMITED internet browsing, but it's not recommended to be "
"used for anything important or secure because it's not a fully featured browser "
"and its security/privacy features were not tested.\n\n"

"This program is free software: you can redistribute it and/or modify "
"it under the terms of the GNU General Public License as published by "
"the Free Software Foundation, either version 3 of the License, or "
"(at your option) any later version.\n\n"

"MX Viewer is distributed in the hope that it will be useful, "
"but WITHOUT ANY WARRANTY; without even the implied warranty of "
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the "
"GNU General Public License for more details.\n\n"

"You should have received a copy of the GNU General Public License "
"along with MX Viewer.  If not, see <http://www.gnu.org/licenses/>.")); });
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
    if (event->matches(QKeySequence::FindNext) || event->matches(QKeySequence::Find) || event->key() == Qt::Key_Slash)
        findForward();
    if (event->matches(QKeySequence::FindPrevious))
        findBackward();
    else if (event->matches(QKeySequence::ZoomIn) || (event->key() == Qt::Key_Equal && event->modifiers() == Qt::ControlModifier)) {
        webview->setZoomFactor(zoom + step);
    } else if (event->matches(QKeySequence::ZoomOut))
        webview->setZoomFactor(zoom - step);
    else if (event->key() == Qt::Key_0)
        webview->setZoomFactor(1);
    else if (event->matches(QKeySequence::Open))
        openDialog();
    else if (event->key() == Qt::Key_B && event->modifiers() == Qt::ControlModifier)
        openBrowseDialog();
    else if (event->matches(QKeySequence::HelpContents) || event->key() == Qt::Key_Question)
        openQuickInfo();
    else if (event->matches(QKeySequence::Cancel) && !searchBox->text().isEmpty() && searchBox->hasFocus())
        searchBox->clear();
    else if (event->matches(QKeySequence::Cancel) && searchBox->text().isEmpty())
        webview->setFocus();
    else if (event->key() == Qt::Key_L && event->modifiers() == Qt::ControlModifier)
        addressBar->setFocus();
}

// resize event
void MainWindow::resizeEvent(QResizeEvent* /*event*/)
{
    if (showProgress)
        progressBar->move(this->geometry().width() / 2 - progressBar->width() / 2, this->geometry().height() - progBarVerticalAdj);
}

// display progressbar while loading page
void MainWindow::loading()
{
    progressBar->setFixedHeight(progBarWidth);
    progressBar->setTextVisible(false);
    progressBar->move(this->geometry().width() / 2 - progressBar->width() / 2, this->geometry().height() - progBarVerticalAdj);
    progressBar->setFocus();
    progressBar->show();
    timer->start(100ms);
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

