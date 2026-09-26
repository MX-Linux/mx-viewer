/*****************************************************************************
 * mainwindow.h
 *****************************************************************************
 * Copyright (C) 2022-2026 MX Authors
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
#include "bookmarkbar.h"
#include "downloadwidget.h"
#include "tabwidget.h"
#include "webview.h"

#include <QHash>
#include <QPointer>
#include <QWebEngineFullScreenRequest>

class QWebEngineSettings;
class QWebEngineScript;
class QWebEngineView;
class QCompleter;
class QStringListModel;
class FindBar;
class QLabel;
class QToolButton;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(const QCommandLineParser &argParser, QWidget *parent = nullptr);
    // A private window uses an off-the-record profile and writes nothing to disk.
    // restoreTabs is false for a window opened next to others, which must not take over the saved tabs.
    explicit MainWindow(const QUrl &url, bool privateMode, bool restoreTabs = true, QWidget *parent = nullptr);
    ~MainWindow() override;
    // Opens a URL passed on by a later "mx-viewer [URL]" launch; empty opens a new tab.
    static void openFromOtherInstance(const QString &argument);
    // Set once the application is quitting, when closing a window can no longer be refused.
    static void setQuitting();

public slots:
    void listHistory();
    void openHistoryPage();
    bool handleHistoryRequest(const QUrl &url);
    void findBackward();
    void findForward();
    void loading();
    void done(bool ok);
    void closeCurrentTab();
    void reopenClosedTab();
    void openLinkInNewTab(const QUrl &url);
    void searchInNewTab(const QString &text);
    void handleFullScreenRequest(QWebEngineFullScreenRequest request, WebView *view);
    void printPage(WebView *view);
    void openDevTools();
    void openSettings();
    bool handleSettingsRequest(const QUrl &url);
    bool restoreSavedTabs();

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    AddressBar *addressBar {};
    DownloadWidget *downloadWidget {};
    QAction *addBookmark {};
    QAction *clearDataAction {};
    QAction *menuButton {};
    QAction *reloadAction {};
    QAction *homeAction {};
    QAction *backAction {};
    QAction *forwardAction {};
    QAction *stopAction {};
    QAction *zoomPercentAction {};
    FindBar *findBar {};
    QPointer<WebView> findView;
    QMenu *bookmarks {};
    BookmarkBar *bookmarkBar {};
    QAction *bookmarkBarAction {};
    QAction *siteIconAction {};
    QPointer<QWidget> siteIconButton;
    QPoint siteIconPressPos;
    bool toolbarsVisible {true};
    // Tabs replace the system title bar, in a frameless window with its own window buttons.
    bool tabsInTitleBar {true};
    QWidget *titleBar {};
    QList<QWidget *> resizeGrips;
    QMenu *history {};
    QCompleter *historyCompleter {};
    QStringListModel *historyCompletionModel {};
    QStringList historyCompletionHosts;
    QProgressBar *progressBar {};
    QString searchEngine;
    QString searchEngineCustom;
    QString lastAddressInput;
    QUrl lastAddressUrl;
    bool lastAddressMaySearch {};
    bool lastAddressExplicitScheme {};
    bool completingHistory {};
    int lastAddressEditLength {};
    bool lastAddressEditWasDeletion {};
    QByteArray normalGeometry;
    QSettings settings;
    QString homeAddress;
    QToolBar *toolBar {};
    QWebEngineProfile *webProfile {};
    QWebEngineSettings *websettings {};
    TabWidget *tabWidget {};
    bool showProgress {};
    bool openNewTabWithHome {};
    int zoomPercent {100};
    bool cookiesEnabled {true};
    bool clearCookiesAtExit {false};
    bool restoredTabs {};
    bool restoreTabsOnOpen {true};
    bool privateWindow {};
    static QPointer<MainWindow> lastActiveWindow;
    static inline bool quitting {false};
    QHash<QString, int> privateZoom;
    bool pageFullScreen {};
    bool fullScreenBeforePage {};
    QPointer<WebView> pageFullScreenView;
    QPointer<WebView> printingView;
    bool clearingCache {};
    const QCommandLineParser *args;
    QList<QPair<QUrl, QIcon>> closedTabs;
    QPointer<QMainWindow> devToolsWindow;
    QPointer<QWebEngineView> devToolsView;
    QWebEngineScript cookieScript;
    QMetaObject::Connection loadStartedConn;
    QMetaObject::Connection loadingConn;
    QMetaObject::Connection loadFinishedConn;
    QMetaObject::Connection urlChangedConn;
    QMetaObject::Connection linkHoveredConn;
    QMetaObject::Connection iconChangedConn;
    static constexpr int defaultHeight {600};
    static constexpr int defaultWidth {800};
    static constexpr int progBarVerticalAdj {40};
    static constexpr int progBarWidth {20};
    static constexpr int gripSize {4};
    static constexpr int minZoom {25};
    static constexpr int maxZoom {500};

    void init();
    QAction *pageAction(QWebEnginePage::WebAction webAction);
    WebView *currentWebView();
    void reloadCurrentView();
    void addActions();
    void addBookmarksSubmenu();
    void showBookmarkMenu(QAction *bookmark, QPoint globalPos, bool fromBar = false);
    void showBookmarkBarMenu(QPoint globalPos);
    void setupBookmarkBar();
    void updateBookmarkBar();
    void setToolbarsVisible(bool visible);
    [[nodiscard]] QList<QAction *> bookmarkList() const;
    void insertBookmark(QAction *bookmark, int index);
    void moveBookmark(int from, int to);
    void addDroppedBookmark(const QUrl &url, const QString &title, int index);
    bool editBookmark(QAction *bookmark);
    void bookmarksChanged();
    void reloadBookmarks();
    void openInNewWindow(const QUrl &url, bool privateMode);
    void updateSiteIcon();
    void updateTitleBar();
    void placeResizeGrips();
    void startAddressDrag();
    void addNavigationActions();
    void addHomeAction();
    void addNewTab(const QUrl &url = QUrl(), bool makeCurrent = true);
    void addToolbar();
    void addZoomActions();
    void setupAddressBar();
    void setupMenuButton();
    void setupFindBar();
    void addFileMenuActions(QMenu *menu);
    void openPrivateWindow();
    void addViewMenuActions(QMenu *menu);
    void addHelpMenuActions(QMenu *menu);
    void setupMenuConnections(QMenu *menu);
    void buildMenu();
    void adaptIcons();
    void centerWindow();
    void clearHistoryEntries();
    void connectAddress(const QAction *action, const QMenu *menu);
    void displaySite(QString url = {}, const QString &title = {});
    void displaySearchResults(const QString &query);
    void openFromAddressBarText(const QString &input);
    QString buildSettingsPageHtml();
    QString buildHistoryPageHtml();
    QString buildNewTabPageHtml();
    void renderNewTabPage(WebView *view);
    void findInPage(QWebEnginePage::FindFlags flags);
    void openFindBar();
    void closeFindBar();
    void focusAddressBar();
    void focusAddressBarIfBlank();
    void applyWebSettings();
    void setupSpellCheck();
    void setZoomPercent(int percent, bool persist);
    void setSiteZoom(int percent);
    void applyZoom();
    [[nodiscard]] int currentZoomPercent() const;
    static QString zoomKey(const QUrl &url);
    void loadBookmarks();
    void loadSettings();
    void openBrowseDialog();
    void openQuickInfo();
    void openAbout();
    void cycleTab(int step);
    void toggleReaderMode();
    void openClearDataDialog();
    void openBookmarksEditor();
    void openFromAddressBar();
    bool isLocalHostInput(const QString &input) const;
    void openSettingsPage();
    void openSavedTab(const QUrl &url, bool makeCurrent);
    void removeHistoryEntry(qint64 id);
    void refreshHistoryCompleter();
    void renderHistoryPage(WebView *view);
    void renderSettingsPage(WebView *view);
    QString searchUrlForQuery(const QString &query) const;
    void saveMenuItems(const QMenu *menu, int offset);
    void setConnections();
    void showFullScreenNotification();
    void tabChanged();
    void toggleFullScreen();
    void exitPageFullScreen();
    void restoreFromPageFullScreen();
    void updateUrl();
};
