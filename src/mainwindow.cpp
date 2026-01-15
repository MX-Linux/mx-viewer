/*****************************************************************************
 * mainwindow.cpp
 *****************************************************************************
 * Copyright (C) 2022 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
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
 ****************************************************************************/
#include "mainwindow.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QtGlobal>
#include <QListWidget>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEngineScript>
#include <QWebEngineView>

MainWindow::MainWindow(const QCommandLineParser &arg_parser, QWidget *parent)
    : QMainWindow(parent),
      downloadWidget {new DownloadWidget},
      searchBox {new QLineEdit(this)},
      progressBar {new QProgressBar(this)},
      toolBar {new QToolBar(this)},
      tabWidget {new TabWidget(this)},
      args {&arg_parser}
{
    init();
    if (arg_parser.isSet("full-screen")) {
        showFullScreen();
        toolBar->hide();
    }
    QString url;
    QString title;
    if (args && !args->positionalArguments().isEmpty()) {
        url = args->positionalArguments().at(0);
        title = (args->positionalArguments().size() > 1) ? args->positionalArguments().at(1) : url;
    }
    if (!restoredTabs) {
        displaySite(url, title);
    } else if (!url.isEmpty()) {
        addNewTab(QUrl::fromUserInput(url), true);
    }
}

MainWindow::MainWindow(const QUrl &url, QWidget *parent)
    : QMainWindow(parent),
      downloadWidget {new DownloadWidget},
      searchBox {new QLineEdit(this)},
      progressBar {new QProgressBar(this)},
      toolBar {new QToolBar(this)},
      tabWidget {new TabWidget(this)},
      args {nullptr}
{
    init();
    if (!restoredTabs) {
        displaySite(url.toString(), QString());
    }
}

void MainWindow::init()
{
    setAttribute(Qt::WA_DeleteOnClose);
    toolBar->toggleViewAction()->setVisible(false);
    connect(tabWidget, &TabWidget::currentChanged, this, [this] { tabChanged(); });
    connect(tabWidget, &TabWidget::newTabButtonClicked, this, [this] { addNewTab(); });
    if (auto *webView = currentWebView()) {
        websettings = webView->settings();
    }
    loadSettings();
    addToolbar();
    addActions();
    setConnections();

    restoredTabs = settings.value("SaveTabs", false).toBool() && restoreSavedTabs();

    auto *closeTabAction = new QAction(this);
    closeTabAction->setShortcut(QKeySequence::Close);
    connect(closeTabAction, &QAction::triggered, this, &MainWindow::closeCurrentTab);
    addAction(closeTabAction);
    auto *reopenTabAction = new QAction(this);
    reopenTabAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    connect(reopenTabAction, &QAction::triggered, this, &MainWindow::reopenClosedTab);
    addAction(reopenTabAction);
}

MainWindow::~MainWindow()
{
    settings.setValue("Geometry", saveGeometry());
    listHistory();
    saveMenuItems(history, 2);
    saveMenuItems(bookmarks, 2);
}

void MainWindow::addActions()
{
    auto *full = new QAction(tr("Full screen"));
    full->setShortcut(Qt::Key_F11);
    addAction(full);
    connect(full, &QAction::triggered, this, &MainWindow::toggleFullScreen);
}

void MainWindow::addBookmarksSubmenu()
{
    bookmarks->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bookmarks, &QMenu::customContextMenuRequested, this, [this](QPoint pos) {
        QAction *currentAction = bookmarks->actionAt(pos);
        if (!currentAction || !currentAction->property("url").isValid()) {
            return;
        }
        QPoint globalPos = bookmarks->mapToGlobal(pos);
        QMenu submenu;
        QList<QAction *> bookmarkActions;
        for (auto *action : bookmarks->actions()) {
            if (action->property("url").isValid()) {
                bookmarkActions.append(action);
            }
        }
        const int currentIndex = bookmarkActions.indexOf(currentAction);
        if (currentIndex > 0) {
            submenu.addAction(QIcon::fromTheme("arrow-up"), tr("Move up"), bookmarks, [this, pos] {
                QAction *action = bookmarks->actionAt(pos);
                if (!action || !action->property("url").isValid()) {
                    return;
                }
                QList<QAction *> actions;
                for (auto *entry : bookmarks->actions()) {
                    if (entry->property("url").isValid()) {
                        actions.append(entry);
                    }
                }
                const int index = actions.indexOf(action);
                if (index > 0) {
                    bookmarks->insertAction(actions.at(index - 1), action);
                }
            });
        }
        if (currentIndex >= 0 && currentIndex < bookmarkActions.count() - 1) {
            submenu.addAction(QIcon::fromTheme("arrow-down"), tr("Move down"), bookmarks, [this, pos] {
                QAction *action = bookmarks->actionAt(pos);
                if (!action || !action->property("url").isValid()) {
                    return;
                }
                QList<QAction *> actions;
                for (auto *entry : bookmarks->actions()) {
                    if (entry->property("url").isValid()) {
                        actions.append(entry);
                    }
                }
                const int index = actions.indexOf(action);
                if (index >= 0 && index < actions.count() - 1) {
                    QAction *insertBefore = (index + 2 < actions.count()) ? actions.at(index + 2) : nullptr;
                    bookmarks->insertAction(insertBefore, action);
                }
            });
        }
        submenu.addAction(QIcon::fromTheme("edit-symbolic"), tr("Rename"), bookmarks, [this, pos] {
            QInputDialog edit(this);
            edit.setInputMode(QInputDialog::TextInput);
            edit.setOkButtonText(tr("Save"));
            edit.setTextValue(bookmarks->actionAt(pos)->text());
            edit.setLabelText(tr("Rename bookmark:"));
            edit.resize(300, edit.height());
            if (edit.exec() == QDialog::Accepted) {
                bookmarks->actionAt(pos)->setText(edit.textValue());
            }
        });
        submenu.addAction(QIcon::fromTheme("user-trash"), tr("Delete"), bookmarks,
                          [this, pos] { bookmarks->removeAction(bookmarks->actionAt(pos)); });
        submenu.exec(globalPos);
    });
}

void MainWindow::addHistorySubmenu()
{
    history->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(history, &QMenu::customContextMenuRequested, this, [this](QPoint pos) {
        if (history->actionAt(pos) == history->actions().at(0)) { // skip first "Clear history" action.
            return;
        }
        QPoint globalPos = history->mapToGlobal(pos);
        QMenu submenu;
        submenu.addAction(QIcon::fromTheme("user-trash"), tr("Delete"), history, [this, pos] {
            history->removeAction(history->actionAt(pos));
            saveMenuItems(history, 3); // skip "clear history", separator, and first item added at menu refresh
        });
        submenu.exec(globalPos);
    });
}

void MainWindow::addNewTab(const QUrl &url, bool makeCurrent)
{
    WebView *view = tabWidget->createTab(makeCurrent);
    if (!view) {
        return;
    }
    if (makeCurrent) {
        setConnections();
    }
    QUrl finalUrl = url;
    if (finalUrl.isEmpty() && openNewTabWithHome) {
        finalUrl = QUrl::fromUserInput(homeAddress);
    }
    if (finalUrl.isEmpty()) {
        finalUrl = QUrl("about:blank");
    }
    view->setUrl(finalUrl);
    view->show();
    if (makeCurrent) {
        QTimer::singleShot(0, this, &MainWindow::focusAddressBarIfBlank);
        QMetaObject::Connection once;
        once = connect(view, &QWebEngineView::loadFinished, this, [this, view, once](bool) mutable {
            if (view == currentWebView()) {
                focusAddressBarIfBlank();
            }
            disconnect(once);
        });
    }
}

void MainWindow::listHistory()
{
    history->clear();
    auto *deleteHistory = new QAction(QIcon::fromTheme("user-trash"), tr("&Clear history"));
    connect(deleteHistory, &QAction::triggered, this, [this] {
        history->clear();
        saveMenuItems(history, 2);
    });
    history->addAction(deleteHistory);
    history->addSeparator();
    loadHistory();
}

void MainWindow::addToolbar()
{
    addToolBar(toolBar);
    setCentralWidget(tabWidget);
    addNavigationActions();
    addHomeAction();
    setupAddressBar();
    setupSearchBox();
    addZoomActions();
    setupMenuButton();
    buildMenu();
    toolBar->show();
}

void MainWindow::addNavigationActions()
{
    auto *back = pageAction(QWebEnginePage::Back);
    auto *forward = pageAction(QWebEnginePage::Forward);
    auto *reload = pageAction(QWebEnginePage::Reload);
    auto *stop = pageAction(QWebEnginePage::Stop);
    toolBar->addAction(back);
    toolBar->addAction(forward);
    toolBar->addAction(reload);
    toolBar->addAction(stop);
    back->setShortcut(QKeySequence::Back);
    forward->setShortcut(QKeySequence::Forward);
    reload->setShortcuts(QKeySequence::Refresh);
    stop->setShortcut(QKeySequence::Cancel);
    connect(stop, &QAction::triggered, this, [this] { done(true); });
}

void MainWindow::addHomeAction()
{
    auto *home {new QAction(QIcon::fromTheme("go-home", QIcon(":/icons/go-home.svg")), tr("Home"))};
    toolBar->addAction(home);
    home->setShortcut(Qt::CTRL | Qt::Key_H);
    connect(home, &QAction::triggered, this, [this] { displaySite(); });
}

void MainWindow::setupAddressBar()
{
    addressBar = new AddressBar(this);
    addressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addressBar->setClearButtonEnabled(true);
    addBookmark = addressBar->addAction(QIcon::fromTheme("emblem-favorite", QIcon(":/icons/emblem-favorite.png")),
                                        QLineEdit::TrailingPosition);
    addBookmark->setToolTip(tr("Add bookmark"));
    connect(addressBar, &QLineEdit::returnPressed, this, [this] { displaySite(addressBar->text()); });
    toolBar->addWidget(addressBar);
}

void MainWindow::setupSearchBox()
{
    searchBox->setPlaceholderText(tr("search in page"));
    searchBox->setClearButtonEnabled(true);
    searchBox->setMaximumWidth(searchWidth);
    searchBox->addAction(QIcon::fromTheme("search", QIcon(":/icons/system-search.png")), QLineEdit::LeadingPosition);
    connect(searchBox, &QLineEdit::textChanged, this, &MainWindow::findForward);
    connect(searchBox, &QLineEdit::returnPressed, this, &MainWindow::findForward);
    toolBar->addWidget(searchBox);
}

void MainWindow::addZoomActions()
{
    auto *zoomout {new QAction(QIcon::fromTheme("zoom-out", QIcon(":/icons/zoom-out.svg")), tr("Zoom out"))};
    zoomPercentAction = new QAction("100%");
    auto *zoomin {new QAction(QIcon::fromTheme("zoom-in", QIcon(":/icons/zoom-in.svg")), tr("Zoom In"))};
    toolBar->addAction(zoomout);
    toolBar->addAction(zoomPercentAction);
    toolBar->addAction(zoomin);
    zoomin->setShortcuts({QKeySequence::ZoomIn, Qt::CTRL | Qt::Key_Equal});
    zoomout->setShortcut(QKeySequence::ZoomOut);
    zoomPercentAction->setShortcut(Qt::CTRL | Qt::Key_0);
    connect(zoomout, &QAction::triggered, this, [this] {
        setZoomPercent(zoomPercent - 10, true);
    });
    connect(zoomin, &QAction::triggered, this, [this] {
        setZoomPercent(zoomPercent + 10, true);
    });
    connect(zoomPercentAction, &QAction::triggered, this, [this] {
        setZoomPercent(100, true);
    });
    setZoomPercent(zoomPercent, false);
}

void MainWindow::setupMenuButton()
{
    menuButton = new QAction(QIcon::fromTheme("open-menu", QIcon(":/icons/open-menu.png")), tr("Settings"));
    toolBar->addAction(menuButton);
    menuButton->setShortcut(Qt::Key_F10);
}

void MainWindow::openBrowseDialog()
{
    QString file = QFileDialog::getOpenFileName(this, tr("Select file to open"), QDir::homePath(),
                                                tr("Hypertext Files (*.htm *.html);;All Files (*.*)"));
    if (QFileInfo::exists(file)) {
        displaySite(file, file);
    }
}

// pop up a window and display website
void MainWindow::displaySite(QString url, const QString &title)
{
    if (url.isEmpty()) {
        if (tabWidget->currentIndex() == 0) {
            url = homeAddress;
        } else {
            return;
        }
    }
    if (QFile::exists(url)) {
        url = QFileInfo(url).absoluteFilePath();
    }
    QUrl qurl = QUrl::fromUserInput(url);
    currentWebView()->setUrl(qurl);
    currentWebView()->show();
    showProgress ? loading() : progressBar->hide();
    setWindowTitle(title);
}

void MainWindow::loadBookmarks()
{
    int size = settings.beginReadArray("Bookmarks");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QAction *bookmark {nullptr};
        bookmarks->addAction(bookmark
                             = new QAction(settings.value("icon").value<QIcon>(), settings.value("title").toString()));
        bookmark->setProperty("url", settings.value("url"));
        connectAddress(bookmark, bookmarks);
    }
    settings.endArray();
}

void MainWindow::loadHistory()
{
    int size = settings.beginReadArray("History");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QAction *histItem {nullptr};
        QByteArray iconByteArray = settings.value("icon").toByteArray();
        QPixmap restoredIconPixmap;
        restoredIconPixmap.loadFromData(iconByteArray);
        QIcon restoredIcon;
        restoredIcon.addPixmap(restoredIconPixmap);
        history->addAction(histItem = new QAction(restoredIcon, settings.value("title").toString()));
        histItem->setProperty("url", settings.value("url"));
        connectAddress(histItem, history);
    }
    settings.endArray();
}

void MainWindow::loadSettings()
{
    // Load first from system .conf file and then overwrite with CLI switches where available
    websettings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    websettings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
    QWebEngineProfile::defaultProfile()->setHttpAcceptLanguage(QLocale::system().name());

    homeAddress = settings.value("Home", "https://start.duckduckgo.com").toString();
    showProgress = settings.value("ShowProgressBar", false).toBool();
    openNewTabWithHome = settings.value("OpenNewTabWithHome", true).toBool();
    zoomPercent = settings.value("ZoomPercent", 100).toInt();
    cookiesEnabled = settings.value("EnableCookies", true).toBool();
    if (!settings.contains("EnableJavaScript") && settings.contains("DisableJava")) {
        settings.setValue("EnableJavaScript", !settings.value("DisableJava", false).toBool());
    }

    applyWebSettings();

    QSize size {defaultWidth, defaultHeight};
    resize(size);
    if (settings.contains("Geometry") && (!args || !args->isSet("full-screen"))) {
        restoreGeometry(settings.value("Geometry").toByteArray());
        if (isMaximized()) { // add option to resize if maximized
            resize(size);
            centerWindow();
        }
    } else {
        centerWindow();
    }
}

void MainWindow::centerWindow()
{
    QRect screenGeometry = QApplication::primaryScreen()->geometry();
    int x = (screenGeometry.width() - width()) / 2;
    int y = (screenGeometry.height() - height()) / 2;
    move(x, y);
}

void MainWindow::openQuickInfo()
{
    QMessageBox::about(this, tr("Keyboard Shortcuts"),
                       tr("Ctrl-F, or F3") + "\t - " + tr("Find") + "\n" + tr("Shift-F3") + "\t - "
                           + tr("Find previous") + "\n" + tr("Ctrl-R, or F5") + "\t - " + tr("Reload") + "\n"
                           + tr("Ctrl-O") + "\t - " + tr("Browse file to open") + "\n" + tr("Esc") + "\t - "
                           + tr("Stop loading/clear Find field") + "\n" + tr("Alt→, Alt←") + "\t - "
                           + tr("Back/Forward") + "\n" + tr("F1, or ?") + "\t - " + tr("Open this help dialog"));
}

void MainWindow::saveMenuItems(const QMenu *menu, int offset)
{
    // Offset is for skipping "Clear history" item, separator, etc.
    settings.beginWriteArray(menu->objectName());
    if (menu->objectName() == "Bookmarks") {
        int index = 0;
        for (auto *action : menu->actions()) {
            if (!action->property("url").isValid()) {
                continue;
            }
            settings.setArrayIndex(index++);
            settings.setValue("title", action->text());
            settings.setValue("url", action->property("url").toString());
            settings.setValue("icon", action->icon());
        }
    } else {
        for (int i = offset; i < menu->actions().count(); ++i) {
            settings.setArrayIndex(i - offset);
            settings.setValue("title", menu->actions().at(i)->text());
            settings.setValue("url", menu->actions().at(i)->property("url").toString());

            QPixmap iconPixmap = menu->actions().at(i)->icon().pixmap(QSize(16, 16));
            QByteArray iconByteArray;
            QBuffer buffer(&iconByteArray);
            buffer.open(QIODevice::WriteOnly);
            iconPixmap.save(&buffer, "PNG");
            settings.setValue("icon", iconByteArray);
        }
    }
    settings.endArray();
}

void MainWindow::setConnections()
{
    if (!currentWebView()) {
        return;
    }
    websettings = currentWebView()->settings();
    applyWebSettings();
    if (loadStartedConn) {
        disconnect(loadStartedConn);
    }
    loadStartedConn = connect(currentWebView(), &QWebEngineView::loadStarted, toolBar, &QToolBar::show);
    if (loadingConn) {
        disconnect(loadingConn);
    }
    if (showProgress) {
        loadingConn = connect(currentWebView(), &QWebEngineView::loadStarted, this, &MainWindow::loading);
    }
    if (urlChangedConn) {
        disconnect(urlChangedConn);
    }
    urlChangedConn = connect(currentWebView(), &QWebEngineView::urlChanged, this, &MainWindow::updateUrl);
    connect(QWebEngineProfile::defaultProfile(), &QWebEngineProfile::downloadRequested, downloadWidget,
            &DownloadWidget::downloadRequested, Qt::UniqueConnection);
    if (loadFinishedConn) {
        disconnect(loadFinishedConn);
    }
    loadFinishedConn = connect(currentWebView(), &QWebEngineView::loadFinished, this, &MainWindow::done);
    if (linkHoveredConn) {
        disconnect(linkHoveredConn);
    }
    linkHoveredConn = connect(currentWebView()->page(), &QWebEnginePage::linkHovered, this, [this](const QString &url) {
        if (url.isEmpty()) {
            statusBar()->hide();
        } else {
            statusBar()->show();
            statusBar()->showMessage(url);
        }
    });
    setZoomPercent(zoomPercent, false);
}

void MainWindow::showFullScreenNotification()
{
    constexpr int distance_top = 100;
    constexpr int duration_ms = 800;
    constexpr double start = 0;
    constexpr double end = 0.85;
    auto *label = new QLabel(this);
    auto *effect = new QGraphicsOpacityEffect;
    label->setGraphicsEffect(effect);
    label->setStyleSheet("padding: 15px; background-color:#787878; color:white");
    label->setText(tr("Press [F11] to exit full screen"));
    label->adjustSize();
    label->move(QApplication::primaryScreen()->geometry().width() / 2 - label->width() / 2, distance_top);
    auto *a = new QPropertyAnimation(effect, "opacity");
    a->setDuration(duration_ms);
    a->setStartValue(start);
    a->setEndValue(end);
    a->setEasingCurve(QEasingCurve::InBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
    label->show();
    QTimer::singleShot(4000, this, [label, effect, end, start] {
        auto *a = new QPropertyAnimation(effect, "opacity");
        a->setDuration(duration_ms);
        a->setStartValue(end);
        a->setEndValue(start);
        a->setEasingCurve(QEasingCurve::OutBack);
        a->start(QPropertyAnimation::DeleteWhenStopped);
        connect(a, &QPropertyAnimation::finished, label, &QLabel::deleteLater);
    });
}

void MainWindow::tabChanged()
{
    if (!currentWebView()) return;
    auto *back = pageAction(QWebEnginePage::Back);
    auto *forward = pageAction(QWebEnginePage::Forward);
    auto *reload = pageAction(QWebEnginePage::Reload);
    auto *stop = pageAction(QWebEnginePage::Stop);
    QMap<QString, QAction *> actionMap = {{"Back", back}, {"Forward", forward}, {"Reload", reload}, {"Stop", stop}};
    auto action_list = toolBar->actions();
    toolBar->setUpdatesEnabled(false);
    for (int i = 0; i < action_list.size() - 1; ++i) {
        auto *currentAction = action_list.at(i);
        auto *nextAction = action_list.at(i + 1);
        auto it = actionMap.find(currentAction->text());
        if (it != actionMap.end()) {
            QAction *replacementAction = it.value();
            toolBar->removeAction(currentAction);
            toolBar->insertAction(nextAction, replacementAction);
        }
    }
    toolBar->setUpdatesEnabled(true);
    addressBar->setText(currentWebView()->url().toString());
    if (addressBar->text().isEmpty()) {
        addressBar->setFocus();
    }
    setWindowTitle(currentWebView()->title());
    setConnections();
    if (devToolsWindow && devToolsView) {
        currentWebView()->page()->setDevToolsPage(devToolsView->page());
    }
}

// Show the address in the toolbar and also connect it to launch it
void MainWindow::connectAddress(const QAction *action, const QMenu *menu)
{
    connect(action, &QAction::hovered, this, [this, action] {
        QString url = action->property("url").toString();
        if (url.isEmpty()) {
            statusBar()->hide();
        } else {
            statusBar()->show();
            statusBar()->showMessage(url);
        }
    });
    connect(action, &QAction::triggered, this, [this, action] {
        QString url = action->property("url").toString();
        displaySite(url);
    });
    connect(menu, &QMenu::aboutToHide, statusBar(), &QStatusBar::hide);
}

void MainWindow::buildMenu()
{
    auto *menu = new QMenu(this);
    history = new QMenu(menu);
    bookmarks = new QMenu(menu);
    bookmarks->setObjectName("Bookmarks");
    history->setStyleSheet("QMenu { menu-scrollable: 1; }");
    history->setObjectName("History");
    bookmarks->setStyleSheet("QMenu { menu-scrollable: 1; }");
    menuButton->setMenu(menu);

    addFileMenuActions(menu);
    addViewMenuActions(menu);
    addHelpMenuActions(menu);

    loadBookmarks();
    addBookmarksSubmenu();
    addHistorySubmenu();

    setupMenuConnections(menu);
}

void MainWindow::addFileMenuActions(QMenu *menu)
{
    QAction *newTab {nullptr};
    menu->addAction(newTab = new QAction(QIcon::fromTheme("tab-new"), tr("&New tab")));
    newTab->setShortcut(Qt::CTRL | Qt::Key_T);
    connect(newTab, &QAction::triggered, this, [this] { addNewTab(); });
}

void MainWindow::addViewMenuActions(QMenu *menu)
{
    QAction *fullScreen {nullptr};
    QAction *devTools {nullptr};
    QAction *historyAction {nullptr};
    QAction *downloadAction {nullptr};
    QAction *bookmarkAction {nullptr};
    QAction *manageBookmarks {nullptr};
    menu->addAction(fullScreen = new QAction(QIcon::fromTheme("view-fullscreen"), tr("&Full screen")));
    menu->addSeparator();
    menu->addAction(devTools = new QAction(QIcon::fromTheme("applications-development"), tr("&Developer Tools")));
    devTools->setShortcut(Qt::Key_F12);
    menu->addAction(historyAction = new QAction(QIcon::fromTheme("history"), tr("H&istory")));
    historyAction->setMenu(history);
    menu->addAction(downloadAction = new QAction(QIcon::fromTheme("folder-download"), tr("&Downloads")));
    downloadAction->setShortcut(Qt::CTRL | Qt::Key_J);
    menu->addAction(bookmarkAction = new QAction(QIcon::fromTheme("emblem-favorite"), tr("&Bookmarks")));
    bookmarkAction->setMenu(bookmarks);
    bookmarks->addAction(addBookmark);
    addBookmark->setText(tr("Bookmark current address"));
    addBookmark->setShortcut(Qt::CTRL | Qt::Key_D);
    bookmarks->addAction(manageBookmarks = new QAction(QIcon::fromTheme("document-edit"), tr("Manage &bookmarks")));
    manageBookmarks->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    bookmarks->addSeparator();
    connect(fullScreen, &QAction::triggered, this, &MainWindow::toggleFullScreen);
    connect(devTools, &QAction::triggered, this, &MainWindow::openDevTools);
    connect(downloadAction, &QAction::triggered, downloadWidget, &QWidget::show);
    connect(manageBookmarks, &QAction::triggered, this, &MainWindow::openBookmarksEditor);
    connect(addBookmark, &QAction::triggered, this, [this] {
        QAction *bookmark {nullptr};
        bookmarks->addAction(bookmark = new QAction(currentWebView()->icon(), currentWebView()->title()));
        bookmark->setProperty("url", currentWebView()->url());
        connectAddress(bookmark, bookmarks);
    });
}

void MainWindow::addHelpMenuActions(QMenu *menu)
{
    QAction *settingsAction {nullptr};
    QAction *help {nullptr};
    QAction *about {nullptr};
    QAction *quit {nullptr};
    menu->addSeparator();
    menu->addAction(settingsAction = new QAction(QIcon::fromTheme("preferences-system"), tr("&Settings")));
    settingsAction->setShortcut(QKeySequence::Preferences);
    menu->addSeparator();
    menu->addAction(help = new QAction(QIcon::fromTheme("help-contents"), tr("&Help")));
    menu->addAction(about = new QAction(QIcon::fromTheme("help-about"), tr("&About")));
    menu->addSeparator();
    menu->addAction(quit = new QAction(QIcon::fromTheme("window-close"), tr("&Exit")));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::openSettings);
    connect(help, &QAction::triggered, this, &MainWindow::openQuickInfo);
    connect(quit, &QAction::triggered, this, &MainWindow::close);
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About MX Viewer"),
                           tr("This is a VERY basic browser based on Qt WebEngine.\n\n"
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
                              "along with MX Viewer.  If not, see <http://www.gnu.org/licenses/>."));
    });
}

void MainWindow::setupMenuConnections(QMenu *menu)
{
    connect(menuButton, &QAction::triggered, this, [this, menu] {
        QPoint pos = mapToParent(toolBar->widgetForAction(menuButton)->pos());
        pos.setY(pos.y() + toolBar->widgetForAction(menuButton)->size().height());
        menu->popup(pos);
        listHistory();
    });
}

void MainWindow::openDevTools()
{
    if (!currentWebView()) {
        return;
    }
    if (!devToolsWindow) {
        devToolsWindow = new QMainWindow(this);
        devToolsWindow->setAttribute(Qt::WA_DeleteOnClose);
        devToolsWindow->setWindowTitle(tr("Developer Tools"));
        devToolsView = new QWebEngineView(devToolsWindow);
        devToolsWindow->setCentralWidget(devToolsView);
        devToolsWindow->resize(900, 700);
        connect(devToolsWindow.data(), &QObject::destroyed, this, [this] {
            devToolsWindow = nullptr;
            devToolsView = nullptr;
        });
    }
    currentWebView()->page()->setDevToolsPage(devToolsView->page());
    devToolsWindow->show();
    devToolsWindow->raise();
    devToolsWindow->activateWindow();
}

void MainWindow::openSettings()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Settings"));
    auto *layout = new QFormLayout(&dialog);
    auto *homeInput = new QLineEdit(homeAddress, &dialog);
    auto *openNewTabCheck = new QCheckBox(tr("Open new tabs with home page"), &dialog);
    openNewTabCheck->setChecked(openNewTabWithHome);
    auto *showProgressCheck = new QCheckBox(tr("Show progress bar"), &dialog);
    showProgressCheck->setChecked(showProgress);
    auto *zoomSpin = new QSpinBox(&dialog);
    zoomSpin->setRange(25, 500);
    zoomSpin->setSuffix("%");
    zoomSpin->setValue(zoomPercent);
    auto *spatialNavCheck = new QCheckBox(tr("Enable spatial navigation"), &dialog);
    spatialNavCheck->setChecked(settings.value("SpatialNavigation", false).toBool());
    auto *enableJsCheck = new QCheckBox(tr("Enable JavaScript"), &dialog);
    enableJsCheck->setChecked(settings.value("EnableJavaScript", true).toBool());
    auto *loadImagesCheck = new QCheckBox(tr("Load images"), &dialog);
    loadImagesCheck->setChecked(settings.value("LoadImages", true).toBool());
    auto *cookiesCheck = new QCheckBox(tr("Enable cookies"), &dialog);
    cookiesCheck->setChecked(settings.value("EnableCookies", true).toBool());
    auto *thirdPartyCookiesCheck = new QCheckBox(tr("Enable third-party cookies"), &dialog);
    thirdPartyCookiesCheck->setChecked(settings.value("EnableThirdPartyCookies", true).toBool());
    if (!cookiesCheck->isChecked()) {
        thirdPartyCookiesCheck->setChecked(false);
    }
    thirdPartyCookiesCheck->setEnabled(cookiesCheck->isChecked());
    auto *thirdPartyRow = new QWidget(&dialog);
    auto *thirdPartyLayout = new QHBoxLayout(thirdPartyRow);
    thirdPartyLayout->setContentsMargins(20, 0, 0, 0);
    thirdPartyLayout->addWidget(thirdPartyCookiesCheck);
    auto *popupCheck = new QCheckBox(tr("Allow pop-up windows"), &dialog);
    popupCheck->setChecked(settings.value("AllowPopups", true).toBool());
    auto *saveTabsCheck = new QCheckBox(tr("Save tabs on closing"), &dialog);
    saveTabsCheck->setChecked(settings.value("SaveTabs", false).toBool());

    layout->addRow(tr("Home address"), homeInput);
    layout->addRow(QString(), openNewTabCheck);
    layout->addRow(QString(), showProgressCheck);
    layout->addRow(tr("Zoom level"), zoomSpin);
    layout->addRow(QString(), spatialNavCheck);
    layout->addRow(QString(), enableJsCheck);
    layout->addRow(QString(), loadImagesCheck);
    layout->addRow(QString(), cookiesCheck);
    layout->addRow(QString(), thirdPartyRow);
    layout->addRow(QString(), popupCheck);
    layout->addRow(QString(), saveTabsCheck);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(cookiesCheck, &QCheckBox::toggled, [thirdPartyCookiesCheck](bool checked) {
        thirdPartyCookiesCheck->setEnabled(checked);
        if (!checked) {
            thirdPartyCookiesCheck->setChecked(false);
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() == QDialog::Accepted) {
        homeAddress = homeInput->text().trimmed();
        openNewTabWithHome = openNewTabCheck->isChecked();
        settings.setValue("Home", homeAddress);
        settings.setValue("OpenNewTabWithHome", openNewTabWithHome);
        showProgress = showProgressCheck->isChecked();
        settings.setValue("ShowProgressBar", showProgress);
        setZoomPercent(zoomSpin->value(), true);
        settings.setValue("SpatialNavigation", spatialNavCheck->isChecked());
        settings.setValue("EnableJavaScript", enableJsCheck->isChecked());
        settings.setValue("LoadImages", loadImagesCheck->isChecked());
        settings.setValue("EnableCookies", cookiesCheck->isChecked());
        settings.setValue("EnableThirdPartyCookies", thirdPartyCookiesCheck->isChecked());
        settings.setValue("AllowPopups", popupCheck->isChecked());
        settings.setValue("SaveTabs", saveTabsCheck->isChecked());

        applyWebSettings();
    }
}

void MainWindow::applyWebSettings()
{
    bool spatialNav = settings.value("SpatialNavigation", false).toBool();
    bool enableJs = settings.value("EnableJavaScript", true).toBool();
    bool loadImages = settings.value("LoadImages", true).toBool();
    bool enableCookies = settings.value("EnableCookies", true).toBool();
    bool enableThirdPartyCookies = settings.value("EnableThirdPartyCookies", true).toBool();
    bool allowPopups = settings.value("AllowPopups", true).toBool();

    if (args && args->isSet("enable-spatial-navigation")) {
        spatialNav = true;
    }
    if (args && args->isSet("disable-js")) {
        enableJs = false;
    }
    if (args && args->isSet("disable-images")) {
        loadImages = false;
    }

    websettings->setAttribute(QWebEngineSettings::SpatialNavigationEnabled, spatialNav);
    websettings->setAttribute(QWebEngineSettings::JavascriptEnabled, enableJs);
    websettings->setAttribute(QWebEngineSettings::AutoLoadImages, loadImages);
    websettings->setAttribute(QWebEngineSettings::LocalStorageEnabled, enableCookies);
    websettings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, allowPopups);

    auto *profile = QWebEngineProfile::defaultProfile();
    profile->setHttpAcceptLanguage(QLocale::system().name());

    if (cookiesEnabled && !enableCookies) {
        profile->cookieStore()->deleteAllCookies();
    }
    cookiesEnabled = enableCookies;

    profile->setPersistentCookiesPolicy(enableCookies ? QWebEngineProfile::ForcePersistentCookies
                                                     : QWebEngineProfile::NoPersistentCookies);

    if (!enableCookies) {
        profile->cookieStore()->setCookieFilter([](const QWebEngineCookieStore::FilterRequest &) {
            return false;
        });

        QString jsCode = R"(
            Object.defineProperty(navigator, 'cookieEnabled', {
                value: false,
                configurable: true
            });
        )";
        cookieScript.setName("cookieDisabled");
        cookieScript.setSourceCode(jsCode);
        cookieScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
        cookieScript.setRunsOnSubFrames(true);
        cookieScript.setWorldId(QWebEngineScript::MainWorld);
    } else {
        if (!enableThirdPartyCookies) {
            profile->cookieStore()->setCookieFilter([](const QWebEngineCookieStore::FilterRequest &request) {
                return !request.thirdParty;
            });
        } else {
            profile->cookieStore()->setCookieFilter(nullptr);
        }

        QString jsCode = R"(
            Object.defineProperty(navigator, 'cookieEnabled', {
                value: true,
                configurable: true
            });
        )";
        cookieScript.setName("cookieEnabled");
        cookieScript.setSourceCode(jsCode);
        cookieScript.setInjectionPoint(QWebEngineScript::DocumentCreation);
        cookieScript.setRunsOnSubFrames(true);
        cookieScript.setWorldId(QWebEngineScript::MainWorld);
    }

    for (int i = 0; i < tabWidget->count(); ++i) {
        if (auto *view = qobject_cast<WebView *>(tabWidget->widget(i))) {
            view->page()->scripts().clear();
            view->page()->scripts().insert(cookieScript);
        }
    }
}

void MainWindow::setZoomPercent(int percent, bool persist)
{
    zoomPercent = qBound(25, percent, 500);
    if (zoomPercentAction) {
        zoomPercentAction->setText(QString::number(zoomPercent) + "%");
    }
    if (auto *view = currentWebView()) {
        view->setZoomFactor(zoomPercent / 100.0);
    }
    if (persist) {
        settings.setValue("ZoomPercent", zoomPercent);
    }
}

void MainWindow::openBookmarksEditor()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Manage bookmarks"));
    dialog.resize(520, 420);

    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(list);

    auto *formLayout = new QFormLayout;
    auto *titleEdit = new QLineEdit(&dialog);
    auto *urlEdit = new QLineEdit(&dialog);
    formLayout->addRow(tr("Title"), titleEdit);
    formLayout->addRow(tr("URL"), urlEdit);
    layout->addLayout(formLayout);

    auto *controlsLayout = new QHBoxLayout;
    auto *moveUpButton = new QPushButton(QIcon::fromTheme("arrow-up"), tr("Move up"), &dialog);
    auto *moveDownButton = new QPushButton(QIcon::fromTheme("arrow-down"), tr("Move down"), &dialog);
    auto *removeButton = new QPushButton(QIcon::fromTheme("user-trash"), tr("Remove"), &dialog);
    controlsLayout->addWidget(moveUpButton);
    controlsLayout->addWidget(moveDownButton);
    controlsLayout->addWidget(removeButton);
    controlsLayout->addStretch();
    layout->addLayout(controlsLayout);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget(buttons);

    for (auto *action : bookmarks->actions()) {
        if (!action->property("url").isValid()) {
            continue;
        }
        auto *item = new QListWidgetItem(action->icon(), action->text(), list);
        item->setData(Qt::UserRole, action->property("url").toString());
    }

    auto syncEditors = [list, titleEdit, urlEdit, moveUpButton, moveDownButton, removeButton] {
        auto *item = list->currentItem();
        const bool hasItem = (item != nullptr);
        titleEdit->setEnabled(hasItem);
        urlEdit->setEnabled(hasItem);
        moveUpButton->setEnabled(hasItem && list->currentRow() > 0);
        moveDownButton->setEnabled(hasItem && list->currentRow() < list->count() - 1);
        removeButton->setEnabled(hasItem);
        if (!hasItem) {
            titleEdit->clear();
            urlEdit->clear();
            return;
        }
        titleEdit->setText(item->text());
        urlEdit->setText(item->data(Qt::UserRole).toString());
    };

    connect(list, &QListWidget::currentRowChanged, &dialog, [syncEditors] { syncEditors(); });
    connect(titleEdit, &QLineEdit::textEdited, &dialog, [list](const QString &text) {
        if (auto *item = list->currentItem()) {
            item->setText(text);
        }
    });
    connect(urlEdit, &QLineEdit::textEdited, &dialog, [list](const QString &text) {
        if (auto *item = list->currentItem()) {
            item->setData(Qt::UserRole, text);
        }
    });
    connect(moveUpButton, &QPushButton::clicked, &dialog, [list] {
        const int row = list->currentRow();
        if (row > 0) {
            auto *item = list->takeItem(row);
            list->insertItem(row - 1, item);
            list->setCurrentItem(item);
        }
    });
    connect(moveDownButton, &QPushButton::clicked, &dialog, [list] {
        const int row = list->currentRow();
        if (row >= 0 && row < list->count() - 1) {
            auto *item = list->takeItem(row);
            list->insertItem(row + 1, item);
            list->setCurrentItem(item);
        }
    });
    connect(removeButton, &QPushButton::clicked, &dialog, [list, syncEditors] {
        const int row = list->currentRow();
        if (row >= 0) {
            delete list->takeItem(row);
            syncEditors();
        }
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (list->count() > 0) {
        list->setCurrentRow(0);
    } else {
        syncEditors();
    }

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    auto actions = bookmarks->actions();
    for (auto *action : actions) {
        if (!action->property("url").isValid()) {
            continue;
        }
        bookmarks->removeAction(action);
        action->deleteLater();
    }
    for (int i = 0; i < list->count(); ++i) {
        auto *item = list->item(i);
        auto *bookmark = new QAction(item->icon(), item->text(), bookmarks);
        bookmark->setProperty("url", item->data(Qt::UserRole).toString());
        bookmarks->addAction(bookmark);
        connectAddress(bookmark, bookmarks);
    }
    saveMenuItems(bookmarks, 2);
}

void MainWindow::closeCurrentTab()
{
    if (tabWidget->count() > 1) {
        closedTabs.append(currentWebView()->url());
        tabWidget->removeTab(tabWidget->currentIndex());
    } else {
        close();
    }
}

void MainWindow::reopenClosedTab()
{
    if (!closedTabs.isEmpty()) {
        addNewTab(closedTabs.takeLast());
    }
}

void MainWindow::openLinkInNewTab(const QUrl &url)
{
    addNewTab(url, false);
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreen()) {
        showNormal();
        toolBar->show();
    } else {
        showFullScreen();
        toolBar->hide();
        showFullScreenNotification();
    }
}

void MainWindow::updateUrl()
{
    addressBar->show();
    addressBar->setText(currentWebView()->url().toDisplayString());
}

bool MainWindow::restoreSavedTabs()
{
    int size = settings.beginReadArray("SavedTabs");
    if (size == 0) {
        settings.endArray();
        return false;
    }

    QList<QUrl> savedUrls;
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString url = settings.value("url").toString();
        if (!url.isEmpty()) {
            savedUrls.append(QUrl::fromUserInput(url));
        }
    }
    settings.endArray();
    settings.remove("SavedTabs");

    if (!savedUrls.isEmpty()) {
        tabWidget->removeTab(0);
        addNewTab(savedUrls.first(), true);
    }

    for (int i = 1; i < savedUrls.size(); ++i) {
        addNewTab(savedUrls.at(i), false);
    }
    return !savedUrls.isEmpty();
}

void MainWindow::focusAddressBar()
{
    if (addressBar) {
        addressBar->setFocus();
    }
}

void MainWindow::focusAddressBarIfBlank()
{
    if (!addressBar) {
        return;
    }
    const QString text = addressBar->text().trimmed();
    if (text.isEmpty() || text == "about:blank") {
        focusAddressBar();
    }
}

void MainWindow::findBackward()
{
    searchBox->setFocus();
    currentWebView()->findText(searchBox->text(), QWebEnginePage::FindBackward);
}

void MainWindow::findForward()
{
    searchBox->setFocus();
    currentWebView()->findText(searchBox->text());
}

// process keystrokes
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->matches(QKeySequence::FindNext) || event->matches(QKeySequence::Find) || event->key() == Qt::Key_Slash) {
        findForward();
        return;
    }
    if (event->matches(QKeySequence::FindPrevious)) {
        findBackward();
        return;
    }
    if (event->matches(QKeySequence::Open)) {
        openBrowseDialog();
        return;
    }
    if (event->matches(QKeySequence::HelpContents) || event->key() == Qt::Key_Question) {
        openQuickInfo();
        return;
    }
    if (event->matches(QKeySequence::Cancel) && !searchBox->text().isEmpty() && searchBox->hasFocus()) {
        searchBox->clear();
        return;
    }
    if (event->matches(QKeySequence::Cancel) && searchBox->text().isEmpty()) {
        currentWebView()->setFocus();
        return;
    }
    if (event->key() == Qt::Key_L && event->modifiers() == Qt::ControlModifier) {
        focusAddressBar();
        return;
    }
    if (event->key() == Qt::Key_R && event->modifiers() == Qt::ControlModifier) {
        if (auto *view = currentWebView()) {
            view->reload();
        }
        return;
    }
    if (event->key() == Qt::Key_Left && event->modifiers() == Qt::AltModifier) {
        if (auto *view = currentWebView()) {
            view->back();
        }
        return;
    }
    if (event->key() == Qt::Key_Right && event->modifiers() == Qt::AltModifier) {
        if (auto *view = currentWebView()) {
            view->forward();
        }
        return;
    }
}

// resize event
void MainWindow::resizeEvent(QResizeEvent * /*event*/)
{
    if (showProgress) {
        progressBar->move(geometry().width() / 2 - progressBar->width() / 2, geometry().height() - progBarVerticalAdj);
    }
}

void MainWindow::closeEvent(QCloseEvent * /*event*/)
{
    downloadWidget->close();

    if (settings.value("SaveTabs", false).toBool()) {
        settings.beginWriteArray("SavedTabs");
        for (int i = 0; i < tabWidget->count(); ++i) {
            settings.setArrayIndex(i);
            auto *webView = qobject_cast<WebView *>(tabWidget->widget(i));
            if (webView) {
                settings.setValue("url", webView->url().toString());
            }
        }
        settings.endArray();
    }
}

QAction *MainWindow::pageAction(QWebEnginePage::WebAction webAction)
{
    return currentWebView()->pageAction(webAction);
}

WebView *MainWindow::currentWebView()
{
    return tabWidget->currentWebView();
}

// display progressbar while loading page
void MainWindow::loading()
{
    progressBar->setFixedHeight(progBarWidth);
    progressBar->setTextVisible(false);
    progressBar->move(geometry().width() / 2 - progressBar->width() / 2, geometry().height() - progBarVerticalAdj);
    progressBar->setFocus();
    progressBar->show();
    progressBar->setRange(0, 0);
}

// done loading
void MainWindow::done(bool ok)
{
    if (!ok) {
        qDebug() << "Error loading:" << currentWebView()->url().toString();
    }
    currentWebView()->stop();
    currentWebView()->setFocus();
    searchBox->clear();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->hide();
    tabWidget->setTabText(tabWidget->currentIndex(), currentWebView()->title());
    setWindowTitle(currentWebView()->title());
}
