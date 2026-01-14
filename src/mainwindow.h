/*****************************************************************************
 * mainwindow.h
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
#pragma once

#include "addressbar.h"
#include "downloadwidget.h"
#include "tabwidget.h"
#include "webview.h"

#include <QPointer>

class QWebEngineSettings;
class QWebEngineScript;
class QWebEngineView;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QCommandLineParser &arg_parser, QWidget *parent = nullptr);
    explicit MainWindow(const QUrl &url, QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    void listHistory();
    void findBackward();
    void findForward();
    void loading();
    void done(bool ok);
    void closeCurrentTab();
    void reopenClosedTab();
    void openLinkInNewTab(const QUrl &url);
    void openDevTools();
    void openSettings();
    bool restoreSavedTabs();

protected:
    void closeEvent(QCloseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    AddressBar *addressBar {};
    DownloadWidget *downloadWidget {};
    QAction *addBookmark {};
    QAction *menuButton {};
    QAction *zoomPercentAction {};
    QLineEdit *searchBox {};
    QMenu *bookmarks {};
    QMenu *history {};
    QProgressBar *progressBar {};
    QSettings settings;
    QString homeAddress;
    QToolBar *toolBar {};
    QWebEngineSettings *websettings {};
    TabWidget *tabWidget {};
    bool showProgress {};
    bool openNewTabWithHome {};
    int zoomPercent {100};
    bool cookiesEnabled {true};
    bool restoredTabs {};
    const QCommandLineParser *args;
    QList<QUrl> closedTabs;
    QPointer<QMainWindow> devToolsWindow;
    QPointer<QWebEngineView> devToolsView;
    QWebEngineScript cookieScript;
    QMetaObject::Connection loadStartedConn;
    QMetaObject::Connection loadingConn;
    QMetaObject::Connection loadFinishedConn;
    QMetaObject::Connection urlChangedConn;
    QMetaObject::Connection linkHoveredConn;
    static constexpr int defaultHeight {600};
    static constexpr int defaultWidth {800};
    static constexpr int progBarVerticalAdj {40};
    static constexpr int progBarWidth {20};
    static constexpr int searchWidth {150};

    void init();
    QAction *pageAction(QWebEnginePage::WebAction webAction);
    WebView *currentWebView();
    void addActions();
    void addBookmarksSubmenu();
    void addHistorySubmenu();
    void addNavigationActions();
    void addHomeAction();
    void addNewTab(const QUrl &url = QUrl(), bool makeCurrent = true);
    void addToolbar();
    void addZoomActions();
    void setupAddressBar();
    void setupMenuButton();
    void setupSearchBox();
    void addFileMenuActions(QMenu *menu);
    void addViewMenuActions(QMenu *menu);
    void addHelpMenuActions(QMenu *menu);
    void setupMenuConnections(QMenu *menu);
    void buildMenu();
    void centerWindow();
    void connectAddress(const QAction *action, const QMenu *menu);
    void displaySite(QString url = {}, const QString &title = {});
    void focusAddressBar();
    void focusAddressBarIfBlank();
    void applyWebSettings();
    void setZoomPercent(int percent, bool persist);
    void loadBookmarks();
    void loadHistory();
    void loadSettings();
    void openBrowseDialog();
    void openQuickInfo();
    void openBookmarksEditor();
    void saveMenuItems(const QMenu *menu, int offset);
    void setConnections();
    void showFullScreenNotification();
    void tabChanged();
    void toggleFullScreen();
    void updateUrl();
};
