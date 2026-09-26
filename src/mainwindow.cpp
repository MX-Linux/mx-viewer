/*****************************************************************************
 * mainwindow.cpp
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
#include "mainwindow.h"

#include <algorithm>
#include <cstdlib>

#include <QAbstractItemView>
#include <QBuffer>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCompleter>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRegularExpression>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QtGlobal>
#include <memory>
#include <QListWidget>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QUrlQuery>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWebEngineFindTextResult>
#include <QWebEngineHistory>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineView>
#include <QStandardPaths>

namespace {
qint64 directorySize(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) {
        return -1;
    }
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

qint64 totalDirectorySize(const QStringList &paths)
{
    qint64 total = 0;
    bool found = false;
    for (const auto &path : paths) {
        if (!QDir(path).exists()) {
            continue;
        }
        const qint64 size = directorySize(path);
        if (size >= 0) {
            total += size;
            found = true;
        }
    }
    return found ? total : -1;
}

QStringList collectCachePaths(const QWebEngineProfile *profile)
{
    if (!profile) return {};
    const QString cachePath = profile->cachePath();
    if (cachePath.isEmpty()) return {};
    QDir dir(cachePath);
    QStringList subdirs;
    for (const QString &entry : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        subdirs << cachePath + "/" + entry;
    }
    return subdirs;
}

bool removeCachePath(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists()) {
        return false;
    }
    if (info.isFile() || info.isSymLink()) {
        return QFile::remove(path);
    }
    QDir dir(path);
    return dir.removeRecursively();
}

// Single-grey icons are drawn for one kind of panel (dark on light, like most themes and the bundled
// backups), so they vanish on the other; repaint those in the panel's text color. Colored icons are
// left alone.
QIcon iconForBackground(const QIcon &icon, const QColor &background, const QColor &foreground)
{
    if (icon.isNull()) {
        return icon;
    }
    const QImage sample = icon.pixmap(32).toImage().convertToFormat(QImage::Format_ARGB32);
    int darkest {255};
    int lightest {0};
    for (int y = 0; y < sample.height(); ++y) {
        for (int x = 0; x < sample.width(); ++x) {
            const QColor color = QColor::fromRgba(sample.pixel(x, y));
            if (color.alpha() < 64) {
                continue;
            }
            if (color.hsvSaturation() > 60 && color.value() > 40) {
                return icon;
            }
            darkest = std::min(darkest, color.lightness());
            lightest = std::max(lightest, color.lightness());
        }
    }
    // Nothing drawn, or a glyph over a filled shape of another grey, which a single color would erase.
    if (darkest > lightest || lightest - darkest > 80) {
        return icon;
    }
    const int mean = (darkest + lightest) / 2;
    if (std::abs(mean - background.lightness()) >= std::abs(mean - foreground.lightness())) {
        return icon;
    }
    QIcon tinted;
    for (const int size : {16, 22, 24, 32, 48, 64}) {
        QPixmap pixmap = icon.pixmap(size);
        if (pixmap.isNull()) {
            continue;
        }
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), foreground);
        painter.end();
        tinted.addPixmap(pixmap);
    }
    return tinted;
}
} // namespace

namespace
{
struct HistoryRecord {
    QString title;
    QString url;
    QByteArray icon;
    qint64 time {}; // seconds since epoch; 0 for entries logged before timestamps were recorded
};

QList<HistoryRecord> readHistory(QSettings &settings)
{
    QList<HistoryRecord> entries;
    const int size = settings.beginReadArray("History");
    entries.reserve(size);
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        const QString url = settings.value("url").toString();
        if (url.isEmpty()) {
            continue;
        }
        entries.append({settings.value("title").toString(), url, settings.value("icon").toByteArray(),
                        settings.value("time").toLongLong()});
    }
    settings.endArray();
    return entries;
}

void writeHistory(QSettings &settings, const QList<HistoryRecord> &entries)
{
    // Clear the old array first; otherwise an entry without an icon would keep the icon of the entry
    // previously stored at its index.
    settings.remove("History");
    settings.beginWriteArray("History");
    for (int i = 0; i < entries.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("title", entries.at(i).title);
        settings.setValue("url", entries.at(i).url);
        if (!entries.at(i).icon.isEmpty()) {
            settings.setValue("icon", entries.at(i).icon);
        }
        if (entries.at(i).time > 0) {
            settings.setValue("time", entries.at(i).time);
        }
    }
    settings.endArray();
    settings.setValue("History/size", entries.size());
}
} // namespace

QPointer<MainWindow> MainWindow::lastActiveWindow;

MainWindow::MainWindow(const QCommandLineParser &argParser, QWidget *parent)
    : QMainWindow(parent),
      downloadWidget {new DownloadWidget},
      searchBox {new QLineEdit(this)},
      progressBar {new QProgressBar(this)},
      toolBar {new QToolBar(this)},
      webProfile {new QWebEngineProfile("mx-viewer", this)},
      tabWidget {new TabWidget(webProfile, this)},
      args {&argParser}
{
    init();
    if (argParser.isSet("full-screen")) {
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

MainWindow::MainWindow(const QUrl &url, bool privateMode, QWidget *parent)
    : QMainWindow(parent),
      downloadWidget {new DownloadWidget},
      searchBox {new QLineEdit(this)},
      progressBar {new QProgressBar(this)},
      toolBar {new QToolBar(this)},
      // A profile without a storage name is off-the-record: cookies, cache and permissions stay in memory.
      webProfile {privateMode ? new QWebEngineProfile(this) : new QWebEngineProfile("mx-viewer", this)},
      tabWidget {new TabWidget(webProfile, this)},
      args {nullptr}
{
    privateWindow = privateMode;
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
    connect(tabWidget, &TabWidget::tabClosed, this, [this](const QUrl &url) {
        if (url.isValid()) {
            QIcon icon;
            if (auto *view = currentWebView()) {
                if (view->url() == url) {
                    icon = view->icon();
                }
            }
            if (icon.isNull()) {
                for (int i = 0; i < tabWidget->count(); ++i) {
                    if (auto *view = qobject_cast<WebView *>(tabWidget->widget(i))) {
                        if (view->url() == url) {
                            icon = view->icon();
                            break;
                        }
                    }
                }
            }
            closedTabs.append({url, icon});
        }
    });
    websettings = webProfile->settings();
    loadSettings();
    addToolbar();
    addActions();
    setConnections();

    restoredTabs = !privateWindow && settings.value("SaveTabs", false).toBool() && restoreSavedTabs();
    if (privateWindow) {
        auto *label = new QLabel(tr("Private"), toolBar);
        label->setToolTip(tr("Private window: history, tabs and site data are not saved"));
        label->setContentsMargins(6, 0, 6, 0);
        toolBar->addWidget(label);
        auto markPrivate = [this](const QString &title) {
            const QString suffix = " \u2014 " + tr("Private");
            if (!title.endsWith(suffix)) {
                setWindowTitle(title + suffix);
            }
        };
        connect(this, &QWidget::windowTitleChanged, this, markPrivate);
        markPrivate(windowTitle());
    }

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
    if (privateWindow) {
        return;
    }
    settings.setValue("Geometry", saveGeometry());
    saveMenuItems(bookmarks, 2);
}

void MainWindow::addActions()
{
    auto *full = new QAction(tr("Full screen"));
    full->setShortcut(Qt::Key_F11);
    addAction(full);
    connect(full, &QAction::triggered, this, &MainWindow::toggleFullScreen);

    // Window-wide so they also work while the page has focus.
    auto *addressAction = new QAction(this);
    addressAction->setShortcuts({Qt::CTRL | Qt::Key_L, Qt::Key_F6, Qt::ALT | Qt::Key_D});
    addAction(addressAction);
    connect(addressAction, &QAction::triggered, this, &MainWindow::focusAddressBar);

    auto *nextTabAction = new QAction(this);
    nextTabAction->setShortcut(Qt::CTRL | Qt::Key_PageDown);
    addAction(nextTabAction);
    connect(nextTabAction, &QAction::triggered, this, [this] { cycleTab(1); });
    auto *previousTabAction = new QAction(this);
    previousTabAction->setShortcuts({Qt::CTRL | Qt::Key_PageUp, Qt::CTRL | Qt::SHIFT | Qt::Key_Backtab});
    addAction(previousTabAction);
    connect(previousTabAction, &QAction::triggered, this, [this] { cycleTab(-1); });

    // Also shown in the History menu; one action, so the shortcut is never registered twice.
    clearDataAction = new QAction(QIcon::fromTheme("edit-clear-history"), tr("Clear browsing data..."), this);
    clearDataAction->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete);
    addAction(clearDataAction);
    connect(clearDataAction, &QAction::triggered, this, &MainWindow::openClearDataDialog);
}

void MainWindow::openClearDataDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Clear browsing data"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *form = new QFormLayout;
    auto *range = new QComboBox(&dialog);
    // Values are ages in seconds; 0 means everything.
    range->addItem(tr("Last hour"), 3600);
    range->addItem(tr("Last 24 hours"), 24 * 3600);
    range->addItem(tr("Last 7 days"), 7 * 24 * 3600);
    range->addItem(tr("Last 4 weeks"), 28 * 24 * 3600);
    range->addItem(tr("All time"), 0);
    range->setCurrentIndex(range->count() - 1);
    form->addRow(tr("Time range:"), range);
    layout->addLayout(form);

    // A private window keeps no history of its own and must not touch the regular one on disk.
    auto *historyBox = new QCheckBox(privateWindow ? tr("Recently closed tabs")
                                                   : tr("Browsing history and recently closed tabs"),
                                     &dialog);
    auto *cookiesBox = new QCheckBox(tr("Cookies"), &dialog);
    auto *cacheBox = new QCheckBox(tr("Cached images and files"), &dialog);
    auto *permissionsBox = new QCheckBox(tr("Site permissions"), &dialog);
    historyBox->setChecked(true);
    cookiesBox->setChecked(true);
    cacheBox->setChecked(true);
    for (auto *box : {historyBox, cookiesBox, cacheBox, permissionsBox}) {
        layout->addWidget(box);
    }
    // QtWebEngine can only clear these completely.
    auto *note = new QLabel(tr("Cookies, cache and site permissions are always cleared for all time."), &dialog);
    note->setWordWrap(true);
    note->setEnabled(false);
    layout->addWidget(note);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *clear = buttons->addButton(tr("Clear data"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    auto updateButton = [=] {
        clear->setEnabled(historyBox->isChecked() || cookiesBox->isChecked() || cacheBox->isChecked()
                          || permissionsBox->isChecked());
    };
    for (auto *box : {historyBox, cookiesBox, cacheBox, permissionsBox}) {
        connect(box, &QCheckBox::toggled, &dialog, updateButton);
    }
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const qint64 age = range->currentData().toLongLong();
    if (historyBox->isChecked()) {
        if (privateWindow) {
            webProfile->clearAllVisitedLinks(); // The window's own off-the-record profile.
            closedTabs.clear();
        } else if (age == 0) {
            clearHistoryEntries();
            webProfile->clearAllVisitedLinks();
            closedTabs.clear();
        } else {
            // Entries without a timestamp predate this feature, so they are older than any range offered.
            const qint64 cutoff = QDateTime::currentSecsSinceEpoch() - age;
            auto entries = readHistory(settings);
            entries.removeIf([cutoff](const HistoryRecord &entry) { return entry.time >= cutoff; });
            writeHistory(settings, entries);
            // Closed tabs carry no time; they are all from this session, which is usually recent.
            closedTabs.clear();
        }
        if (auto *view = currentWebView(); view && view->url().scheme() == "mx-history") {
            renderHistoryPage(view);
        }
    }
    if (cookiesBox->isChecked()) {
        webProfile->cookieStore()->deleteAllCookies();
    }
    if (cacheBox->isChecked()) {
        webProfile->clearHttpCache();
    }
    if (permissionsBox->isChecked()) {
        const auto permissions = webProfile->listAllPermissions();
        for (const auto &permission : permissions) {
            permission.reset();
        }
    }
}

void MainWindow::cycleTab(int step)
{
    const int count = tabWidget->count();
    if (count > 1) {
        tabWidget->setCurrentIndex((tabWidget->currentIndex() + step + count) % count);
    }
}

void MainWindow::toggleReaderMode()
{
    QPointer<WebView> view = currentWebView();
    if (!view) {
        return;
    }
    if (view->isReaderMode()) {
        // Reader view was pushed on top of the original page, so going back restores it without a reload.
        const auto *history = view->history();
        if (history->canGoBack() && history->backItem().url() == view->url()) {
            view->back();
        } else {
            view->load(view->url());
        }
        return;
    }
    const QString scheme = view->url().scheme();
    if (scheme != "http" && scheme != "https" && scheme != "file") {
        return;
    }
    static const QString readability = [] {
        QFile file(":/readability/Readability.js");
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }();
    if (readability.isEmpty()) {
        return;
    }
    // Parse a copy so the page itself is untouched; the isolated world keeps page scripts out of it.
    const QString script = readability + QStringLiteral(R"(
;(function() {
    const article = new Readability(document.cloneNode(true)).parse();
    if (!article || !article.content) {
        return null;
    }
    return {title: article.title || document.title, byline: article.byline || '', siteName: article.siteName || '',
            content: article.content, lang: article.lang || document.documentElement.lang || '',
            dir: article.dir || ''};
})();)");
    const QUrl pageUrl = view->url();
    view->page()->runJavaScript(script, QWebEngineScript::ApplicationWorld, [this, view, pageUrl](const QVariant &result) {
        if (!view || view->url() != pageUrl || view->isReaderMode()) {
            return;
        }
        const QVariantMap article = result.toMap();
        if (article.isEmpty()) {
            auto *box = new QMessageBox(QMessageBox::Information, tr("Reader view"),
                                        tr("No article was found on this page."), QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
            return;
        }
        const auto field = [&article](const char *key) { return article.value(key).toString().toHtmlEscaped(); };
        const QString meta = QStringList {field("siteName"), field("byline")}.filter(QRegularExpression("\\S")).join(" · ");
        // The article HTML comes from the page, so the reader page may not run scripts, submit forms
        // or load anything but images, media and fonts.
        const QString html = QStringLiteral(
            "<!DOCTYPE html><html lang=\"%1\" dir=\"%2\" data-mx-reader><head><meta charset=\"utf-8\">"
            "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; img-src * data:; "
            "media-src *; font-src *; style-src 'unsafe-inline'; form-action 'none'; base-uri 'none'\">"
            "<title>%3</title><style>"
            ":root{color-scheme:light dark}"
            "body{margin:0;background:#f8f6f0;color:#222;font:1.15rem/1.65 Georgia,'DejaVu Serif',serif}"
            "a{color:#1a5fb4}"
            "@media (prefers-color-scheme:dark){body{background:#1f1f1f;color:#ddd}a{color:#8ab4f8}}"
            "main{max-width:40em;margin:0 auto;padding:2em 1.25em 4em}"
            "h1{font-family:sans-serif;line-height:1.25;margin-bottom:.3em}"
            ".meta{color:#888;font:.9rem sans-serif;margin-top:0}"
            "img,video,svg,iframe{max-width:100%;height:auto}figure{margin:1.5em 0}"
            "pre{overflow:auto;white-space:pre-wrap}table{display:block;max-width:100%;overflow:auto}"
            "</style></head><body><main><h1>%3</h1><p class=\"meta\">%4</p>%5</main></body></html>")
                                 .arg(field("lang"), field("dir"), field("title"), meta,
                                      article.value("content").toString());
        // setHtml() passes the page as a data: URL, which is limited to 2 MB once encoded.
        constexpr qsizetype maxReaderSize {1400 * 1024};
        if (html.toUtf8().size() > maxReaderSize) {
            auto *box = new QMessageBox(QMessageBox::Information, tr("Reader view"),
                                        tr("This article is too large for reader view."), QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            box->open();
            return;
        }
        view->showReaderPage(html);
    });
}

void MainWindow::addBookmarksSubmenu()
{
    // A menu takes any button release as a click, so right and middle clicks on bookmarks are handled
    // in eventFilter() before the menu sees them.
    bookmarks->installEventFilter(this);
}

void MainWindow::showBookmarkMenu(QAction *bookmark, QPoint globalPos)
{
    QList<QAction *> bookmarkActions;
    for (auto *action : bookmarks->actions()) {
        if (action->property("url").isValid()) {
            bookmarkActions.append(action);
        }
    }
    const int index = bookmarkActions.indexOf(bookmark);
    if (index < 0) {
        return;
    }
    QMenu submenu;
    QAction *moveUp {nullptr};
    QAction *moveDown {nullptr};
    if (index > 0) {
        moveUp = submenu.addAction(QIcon::fromTheme("arrow-up"), tr("Move up"));
    }
    if (index < bookmarkActions.count() - 1) {
        moveDown = submenu.addAction(QIcon::fromTheme("arrow-down"), tr("Move down"));
    }
    QAction *openInTab = submenu.addAction(QIcon::fromTheme("tab-new"), tr("Open in new tab"));
    QAction *rename = submenu.addAction(QIcon::fromTheme("edit-symbolic"), tr("Rename"));
    QAction *remove = submenu.addAction(QIcon::fromTheme("user-trash"), tr("Delete"));
    const QPointer<QAction> target(bookmark);
    QAction *chosen = submenu.exec(globalPos);
    if (!chosen || !target) {
        return;
    }
    if (chosen == moveUp) {
        bookmarks->insertAction(bookmarkActions.at(index - 1), target);
    } else if (chosen == moveDown) {
        bookmarks->insertAction(index + 2 < bookmarkActions.count() ? bookmarkActions.at(index + 2) : nullptr, target);
    } else if (chosen == openInTab) {
        openLinkInNewTab(target->property("url").toUrl());
    } else if (chosen == rename) {
        QInputDialog edit(this);
        edit.setInputMode(QInputDialog::TextInput);
        edit.setOkButtonText(tr("Save"));
        edit.setTextValue(target->text());
        edit.setLabelText(tr("Rename bookmark:"));
        edit.resize(300, edit.height());
        if (edit.exec() == QDialog::Accepted && target) {
            target->setText(edit.textValue());
        }
    } else if (chosen == remove) {
        bookmarks->removeAction(target);
        target->deleteLater();
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != bookmarks) {
        return QMainWindow::eventFilter(watched, event);
    }
    if (event->type() == QEvent::ContextMenu) {
        // Keyboard only (the Menu key); a mouse right click is handled on release below.
        auto *menuEvent = static_cast<QContextMenuEvent *>(event);
        QAction *action = bookmarks->activeAction();
        if (menuEvent->reason() == QContextMenuEvent::Keyboard && action && action->property("url").isValid()) {
            showBookmarkMenu(action, bookmarks->mapToGlobal(bookmarks->actionGeometry(action).center()));
        }
        return true;
    }
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease) {
        return false;
    }
    auto *mouseEvent = static_cast<QMouseEvent *>(event);
    if (mouseEvent->button() != Qt::RightButton && mouseEvent->button() != Qt::MiddleButton) {
        return false;
    }
    QAction *action = bookmarks->actionAt(mouseEvent->position().toPoint());
    if (!action || !action->property("url").isValid()) {
        return false;
    }
    if (event->type() == QEvent::MouseButtonRelease) {
        if (mouseEvent->button() == Qt::RightButton) {
            showBookmarkMenu(action, mouseEvent->globalPosition().toPoint());
        } else {
            // Close the whole menu chain, as a normal click would.
            QWidget *top = bookmarks;
            while (qobject_cast<QMenu *>(top->parentWidget())) {
                top = top->parentWidget();
            }
            top->hide();
            openLinkInNewTab(action->property("url").toUrl());
        }
    }
    return true;
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
    QUrl finalUrl = url.scheme() == "mx-newtab" ? QUrl() : url;
    if (finalUrl.isEmpty() && openNewTabWithHome) {
        finalUrl = QUrl::fromUserInput(homeAddress);
    }
    if (finalUrl.isEmpty()) {
        renderNewTabPage(view);
    } else {
        view->setUrl(finalUrl);
    }
    view->show();
    if (makeCurrent) {
        QTimer::singleShot(0, this, &MainWindow::focusAddressBarIfBlank);
        connect(
            view, &QWebEngineView::loadFinished, this,
            [this, view](bool) {
                if (view == currentWebView()) {
                    focusAddressBarIfBlank();
                }
            },
            Qt::SingleShotConnection);
    }
}

void MainWindow::renderNewTabPage(WebView *view)
{
    if (view) {
        view->setHtml(buildNewTabPageHtml(), QUrl("mx-newtab://"));
    }
}

// Speed dial: bookmarks and the most visited sites from history (bookmarks only in a private window).
QString MainWindow::buildNewTabPageHtml()
{
    constexpr int maxTiles = 12;
    auto tile = [](const QString &url, const QString &label, const QString &detail, const QByteArray &icon) {
        const QString iconHtml = icon.isEmpty()
            ? QStringLiteral("<span class=\"icon letter\">%1</span>").arg(label.left(1).toUpper().toHtmlEscaped())
            : QStringLiteral("<img class=\"icon\" alt=\"\" src=\"data:image/png;base64,%1\">")
                  .arg(QString::fromLatin1(icon.toBase64()));
        return QStringLiteral("<a class=\"tile\" href=\"%1\" title=\"%2\">%3<span class=\"label\">%4</span>"
                              "<span class=\"detail\">%5</span></a>")
            .arg(url.toHtmlEscaped(), (label + "\n" + url).toHtmlEscaped(), iconHtml, label.toHtmlEscaped(),
                 detail.toHtmlEscaped());
    };

    QStringList bookmarkTiles;
    const auto actions = bookmarks->actions();
    for (const QAction *action : actions) {
        const QUrl url = action->property("url").toUrl();
        if (!url.isValid() || url.isEmpty()) {
            continue;
        }
        QByteArray icon;
        if (!action->icon().isNull()) {
            QBuffer buffer(&icon);
            if (buffer.open(QIODevice::WriteOnly)) {
                action->icon().pixmap(QSize(32, 32)).save(&buffer, "PNG");
            }
        }
        const QString label = action->text().isEmpty() ? url.host() : action->text();
        bookmarkTiles.append(tile(url.toString(), label, url.host(), icon));
        if (bookmarkTiles.size() == maxTiles) {
            break;
        }
    }

    QStringList siteTiles;
    if (!privateWindow) {
        struct Site {
            QUrl root;
            int visits {};
            int lastIndex {};
            QByteArray icon;
        };
        QHash<QString, Site> sites;
        const int size = settings.beginReadArray("History");
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            const QUrl url(settings.value("url").toString());
            if (url.host().isEmpty() || (url.scheme() != "http" && url.scheme() != "https")) {
                continue;
            }
            // Keyed by port too, so local servers on different ports are separate sites.
            Site &site = sites[url.host() + ':' + QString::number(url.port())];
            // adjusted() keeps the port and IPv6 brackets that rebuilding the URL from host() would lose.
            site.root = url.adjusted(QUrl::RemoveUserInfo | QUrl::RemovePath | QUrl::RemoveQuery | QUrl::RemoveFragment);
            site.root.setPath("/");
            ++site.visits;
            site.lastIndex = i;
            const QByteArray icon = settings.value("icon").toByteArray();
            if (!icon.isEmpty()) {
                site.icon = icon;
            }
        }
        settings.endArray();
        QList<Site> ranked = sites.values();
        std::sort(ranked.begin(), ranked.end(), [](const Site &a, const Site &b) {
            return a.visits != b.visits ? a.visits > b.visits : a.lastIndex > b.lastIndex;
        });
        for (const Site &site : std::as_const(ranked)) {
            // Host with brackets and port, as typed; user info was removed from root.
            QString label = site.root.authority();
            if (label.startsWith("www.")) {
                label = label.mid(4);
            }
            siteTiles.append(tile(site.root.toString(), label, tr("%n visit(s)", nullptr, site.visits), site.icon));
            if (siteTiles.size() == maxTiles) {
                break;
            }
        }
    }

    auto section = [](const QString &heading, const QStringList &tiles) {
        return tiles.isEmpty() ? QString()
                               : QStringLiteral("<h2>%1</h2><div class=\"grid\">%2</div>")
                                     .arg(heading.toHtmlEscaped(), tiles.join("\n"));
    };
    QString body = section(tr("Bookmarks"), bookmarkTiles) + section(tr("Most visited"), siteTiles);
    if (body.isEmpty()) {
        body = QStringLiteral("<p class=\"empty\">%1</p>")
                   .arg(tr("Type an address or search terms in the address bar.").toHtmlEscaped());
    }
    return QStringLiteral(R"(<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>%1</title>
  <style>
    :root { color-scheme: light dark; }
    body { font-family: sans-serif; margin: 32px auto; max-width: 960px; padding: 0 24px; }
    h2 { font-size: 15px; font-weight: 600; opacity: 0.7; margin: 24px 0 12px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 12px; }
    .tile { display: flex; flex-direction: column; align-items: center; gap: 6px; padding: 16px 8px 12px;
            border: 1px solid rgba(128, 128, 128, 0.3); border-radius: 10px; text-decoration: none; color: inherit; }
    .tile:hover, .tile:focus { background: rgba(128, 128, 128, 0.12); outline: none; }
    .icon { width: 32px; height: 32px; border-radius: 6px; }
    .letter { display: flex; align-items: center; justify-content: center; font-weight: 700; font-size: 18px;
              background: rgba(128, 128, 128, 0.2); }
    .label, .detail { max-width: 100%; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .label { font-size: 14px; }
    .detail { font-size: 12px; opacity: 0.6; }
    .empty { text-align: center; opacity: 0.6; margin-top: 15%; }
  </style>
</head>
<body>%2</body>
</html>)")
        .arg(tr("New Tab").toHtmlEscaped(), body);
}

void MainWindow::listHistory()
{
    history->clear();
    auto *showHistory = new QAction(QIcon::fromTheme("view-list-text"), tr("History"));
    showHistory->setShortcut(Qt::CTRL | Qt::Key_H);
    connect(showHistory, &QAction::triggered, this, &MainWindow::openHistoryPage);
    history->addAction(showHistory);
    history->addAction(clearDataAction);
    history->addSeparator();
    auto *recentTitle = new QWidgetAction(history);
    auto *recentLabel = new QLabel(tr("Recent tabs"), history);
    QFont recentFont = recentLabel->font();
    recentFont.setUnderline(true);
    recentFont.setBold(true);
    recentLabel->setFont(recentFont);
    recentLabel->setStyleSheet("color: #4a4a4a; padding: 4px 18px 4px 18px;");
    recentTitle->setDefaultWidget(recentLabel);
    history->addAction(recentTitle);
    if (closedTabs.isEmpty()) {
        auto *emptyAction = history->addAction(tr("No recent tabs"));
        emptyAction->setEnabled(false);
    } else {
        for (int i = closedTabs.size() - 1; i >= 0; --i) {
            const int tabIndex = i;
            const auto entry = closedTabs.at(i);
            const QUrl url = entry.first;
            auto *action = history->addAction(entry.second, url.toDisplayString());
            connect(action, &QAction::triggered, this, [this, tabIndex] {
                if (tabIndex < 0 || tabIndex >= closedTabs.size()) {
                    return;
                }
                const auto entry = closedTabs.takeAt(tabIndex);
                openSavedTab(entry.first, true);
            });
        }
    }
    refreshHistoryCompleter();
}

QString MainWindow::buildHistoryPageHtml()
{
    struct HistoryEntry {
        QString title;
        QString url;
        QByteArray icon;
    };

    QList<HistoryEntry> entries;
    // The history on disk belongs to regular windows; a private window lists none of it.
    if (!privateWindow) {
        const int size = settings.beginReadArray("History");
        entries.reserve(size);
        for (int i = 0; i < size; ++i) {
            settings.setArrayIndex(i);
            const QString url = settings.value("url").toString();
            if (url.isEmpty()) {
                continue;
            }
            QString title = settings.value("title").toString();
            if (title.isEmpty()) {
                title = url;
            }
            entries.append({title, url, settings.value("icon").toByteArray()});
        }
        settings.endArray();
    }

    QStringList rows;
    rows.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const HistoryEntry &entry = entries.at(i);
        const QString titleEscaped = entry.title.toHtmlEscaped();
        const QString urlEscaped = entry.url.toHtmlEscaped();
        const QString searchText = (entry.title + " " + entry.url).toLower().toHtmlEscaped();
        QString iconHtml;
        if (!entry.icon.isEmpty()) {
            const QString iconBase64 = QString::fromLatin1(entry.icon.toBase64());
            iconHtml = QStringLiteral("<img class=\"icon\" alt=\"\" src=\"data:image/png;base64,%1\">")
                           .arg(iconBase64);
        } else {
            iconHtml = QStringLiteral("<span class=\"icon placeholder\"></span>");
        }
        rows.append(QStringLiteral(
                        "<li class=\"entry\" data-search=\"%1\">"
                        "%2"
                        "<div class=\"content\">"
                        "<div class=\"row\">"
                        "<a class=\"title\" href=\"%3\">%4</a>"
                        "<button class=\"delete\" data-index=\"%5\">%6</button>"
                        "</div>"
                        "<div class=\"url\">%7</div>"
                        "</div>"
                        "</li>")
                        .arg(searchText, iconHtml, urlEscaped, titleEscaped, QString::number(i),
                             tr("Delete").toHtmlEscaped(), urlEscaped));
    }

    const QString emptyText = tr("No history entries.");
    const QString html = QStringLiteral(R"(<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="cache-control" content="no-cache">
  <title>%1</title>
  <style>
    :root { color-scheme: light; }
    body { font-family: sans-serif; margin: 24px; color: #1f2328; background: #ffffff; }
    h1 { font-size: 22px; margin: 0 0 12px; }
    .controls { display: flex; gap: 12px; align-items: center; margin-bottom: 16px; flex-wrap: wrap; }
    .search { flex: 1 1 240px; padding: 8px 10px; border: 1px solid #d0d7de; border-radius: 6px; }
    .clear { padding: 8px 12px; border: 1px solid #d0d7de; background: #f6f8fa; border-radius: 6px; cursor: pointer; }
    .list { list-style: none; padding: 0; margin: 0; display: flex; flex-direction: column; gap: 10px; }
    .entry { display: grid; grid-template-columns: 24px 1fr; gap: 10px; padding: 10px 12px; border: 1px solid #eaeef2; border-radius: 8px; }
    .icon { width: 20px; height: 20px; border-radius: 4px; }
    .icon.placeholder { background: #eaeef2; }
    .content { display: flex; flex-direction: column; gap: 4px; }
    .row { display: flex; align-items: center; gap: 10px; }
    .title { color: #0969da; text-decoration: none; font-weight: 600; flex: 1 1 auto; }
    .url { color: #57606a; font-size: 12px; word-break: break-all; }
    .delete { padding: 4px 8px; border: 1px solid #d0d7de; background: #fff; border-radius: 6px; cursor: pointer; }
    .empty { padding: 16px; border: 1px dashed #d0d7de; border-radius: 8px; color: #57606a; }
  </style>
</head>
<body>
  <h1>%1</h1>
  <div class="controls">
    <input id="search" class="search" type="search" placeholder="%2" autofocus>
    <button id="clear" class="clear">%3</button>
  </div>
  %4
  <script>
    const search = document.getElementById('search');
    const entries = Array.from(document.querySelectorAll('li.entry'));
    function applyFilter() {
      const term = search.value.trim().toLowerCase();
      entries.forEach(entry => {
        entry.style.display = entry.dataset.search.includes(term) ? '' : 'none';
      });
    }
    search.addEventListener('input', applyFilter);
    document.querySelectorAll('button.delete').forEach(btn => {
      btn.addEventListener('click', event => {
        event.preventDefault();
        location.href = 'mx-history://delete?index=' + btn.dataset.index;
      });
    });
    const clearButton = document.getElementById('clear');
    clearButton.addEventListener('click', event => {
      event.preventDefault();
      if (confirm('%5')) {
        location.href = 'mx-history://clear';
      }
    });
  </script>
</body>
</html>)")
                            .arg(tr("History").toHtmlEscaped(), tr("Search history").toHtmlEscaped(),
                                 tr("Clear history").toHtmlEscaped(),
                                 rows.isEmpty()
                                     ? QStringLiteral("<div class=\"empty\">%1</div>").arg(emptyText.toHtmlEscaped())
                                     : QStringLiteral("<ul class=\"list\">%1</ul>").arg(rows.join("\n")),
                                 tr("Clear all history entries?").toHtmlEscaped());

    return html;
}

void MainWindow::renderHistoryPage(WebView *view)
{
    if (!view) {
        return;
    }
    view->setHtml(buildHistoryPageHtml(), QUrl("mx-history://list"));
    view->show();
    tabWidget->setTabTitle(tabWidget->indexOf(view), tr("History"));
    setWindowTitle(tr("History"));
    updateUrl();
}

void MainWindow::openHistoryPage()
{
    if (auto *view = currentWebView()) {
        if (view->url().scheme() == "mx-history") {
            renderHistoryPage(view);
            return;
        }
    }
    auto *view = tabWidget->createTab(true);
    if (!view) {
        return;
    }
    setConnections();
    renderHistoryPage(view);
}

void MainWindow::removeHistoryEntry(int index)
{
    if (privateWindow || index < 0) {
        return;
    }
    auto entries = readHistory(settings);
    if (index >= entries.size()) {
        return;
    }
    entries.removeAt(index);
    writeHistory(settings, entries);
}

void MainWindow::clearHistoryEntries()
{
    if (privateWindow) {
        return;
    }
    settings.remove("History");
    settings.setValue("History/size", 0);
}

bool MainWindow::handleHistoryRequest(const QUrl &url)
{
    if (url.scheme() != "mx-history") {
        return false;
    }
    const QString action = url.host();
    if (action == "delete") {
        QUrlQuery query(url);
        removeHistoryEntry(query.queryItemValue("index").toInt());
    } else if (action == "clear") {
        clearHistoryEntries();
    } else {
        return false;
    }
    refreshHistoryCompleter();
    renderHistoryPage(currentWebView());
    return true;
}

void MainWindow::refreshHistoryCompleter()
{
    QStringList completions;
    QStringList hosts;
    QSet<QString> seenUrls;
    QSet<QString> seenHosts;
    int size = settings.beginReadArray("History");
    completions.reserve(size);
    for (int i = size - 1; i >= 0; --i) {
        settings.setArrayIndex(i);
        const QString urlValue = settings.value("url").toString();
        if (urlValue.isEmpty() || urlValue == "about:blank") {
            continue;
        }
        const QUrl url = QUrl::fromUserInput(urlValue);
        if (url.scheme() == "mx-history" || url.scheme() == "mx-settings") {
            continue;
        }
        const QString host = url.host();
        if (!host.isEmpty() && !seenHosts.contains(host)) {
            seenHosts.insert(host);
            hosts.append(host);
            if (host.startsWith("www.", Qt::CaseInsensitive)) {
                const QString stripped = host.mid(4);
                if (!seenHosts.contains(stripped)) {
                    seenHosts.insert(stripped);
                    hosts.append(stripped);
                }
            }
        }
        if (!seenUrls.contains(urlValue)) {
            seenUrls.insert(urlValue);
            completions.append(urlValue);
        }
    }
    settings.endArray();
    historyCompletionModel->setStringList(completions);
    historyCompletionHosts = hosts;
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
    adaptIcons();
    toolBar->show();
}

// Run again whenever the palette or the tab's page actions change, since icons follow the panel.
void MainWindow::adaptIcons()
{
    const auto adapt = [](QWidget *widget, QPalette::ColorRole background, QPalette::ColorRole foreground) {
        const QPalette palette = widget->palette();
        for (QAction *action : widget->actions()) {
            action->setIcon(iconForBackground(action->icon(), palette.color(background), palette.color(foreground)));
        }
    };
    adapt(toolBar, QPalette::Window, QPalette::WindowText);
    adapt(addressBar, QPalette::Base, QPalette::Text);
    adapt(searchBox, QPalette::Base, QPalette::Text);
    // Only the main menu's own items: bookmark icons are saved back to settings and must stay as fetched.
    if (QMenu *menu = menuButton->menu()) {
        adapt(menu, QPalette::Window, QPalette::WindowText);
    }
}

void MainWindow::addNavigationActions()
{
    backAction = pageAction(QWebEnginePage::Back);
    forwardAction = pageAction(QWebEnginePage::Forward);
    auto *reload = pageAction(QWebEnginePage::Reload);
    stopAction = pageAction(QWebEnginePage::Stop);
    toolBar->addAction(backAction);
    toolBar->addAction(forwardAction);
    reloadAction = new QAction(reload->icon(), reload->text(), this);
    toolBar->addAction(reloadAction);
    // On the window too, so the shortcut works with the toolbar hidden. It must stay window-local:
    // with several windows an application-wide shortcut would be ambiguous and fire nowhere.
    addAction(reloadAction);
    toolBar->addAction(stopAction);
    backAction->setShortcut(QKeySequence::Back);
    forwardAction->setShortcut(QKeySequence::Forward);
    reloadAction->setShortcuts(QKeySequence::Refresh);
    stopAction->setShortcut(QKeySequence::Cancel);
    connect(reloadAction, &QAction::triggered, this, &MainWindow::reloadCurrentView);
    connect(stopAction, &QAction::triggered, this, [this] { done(true); });
}

void MainWindow::addHomeAction()
{
    homeAction = new QAction(QIcon::fromTheme("go-home", QIcon(":/icons/go-home.svg")), tr("Home"));
    toolBar->addAction(homeAction);
    homeAction->setShortcut(Qt::ALT | Qt::Key_Home);
    connect(homeAction, &QAction::triggered, this, [this] { displaySite(); });
}

void MainWindow::setupAddressBar()
{
    addressBar = new AddressBar(this);
    addressBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    addressBar->setClearButtonEnabled(true);
    historyCompletionModel = new QStringListModel(this);
    historyCompleter = new QCompleter(historyCompletionModel, this);
    historyCompleter->setCaseSensitivity(Qt::CaseInsensitive);
    historyCompleter->setCompletionMode(QCompleter::PopupCompletion);
    historyCompleter->setFilterMode(Qt::MatchContains);
    addressBar->setCompleter(historyCompleter);
    connect(historyCompleter, QOverload<const QString &>::of(&QCompleter::activated), this,
            [this](const QString &text) {
                addressBar->setText(text);
                openFromAddressBarText(text);
            });
    connect(addressBar, &AddressBar::focused, this, [this] {
        lastAddressEditLength = addressBar->text().size();
        refreshHistoryCompleter();
    });
    connect(addressBar, &AddressBar::keyPressed, this, [this](int key) {
        lastAddressEditWasDeletion = (key == Qt::Key_Backspace || key == Qt::Key_Delete);
    });
    connect(addressBar, &QLineEdit::textEdited, this, [this](const QString &text) {
        if (completingHistory) {
            return;
        }
        if (addressBar->cursorPosition() < text.size()) {
            return;
        }
        if (lastAddressEditWasDeletion) {
            lastAddressEditLength = text.size();
            lastAddressEditWasDeletion = false;
            return;
        }
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty() || trimmed.contains(' ')) {
            lastAddressEditLength = text.size();
            lastAddressEditWasDeletion = false;
            return;
        }
        if (historyCompletionHosts.isEmpty()) {
            lastAddressEditLength = text.size();
            lastAddressEditWasDeletion = false;
            return;
        }
        QString prefix;
        QString hostInput = trimmed;
        const int schemeIndex = trimmed.indexOf("://");
        if (schemeIndex >= 0) {
            prefix = trimmed.left(schemeIndex + 3);
            hostInput = trimmed.mid(schemeIndex + 3);
        }
        const int pathIndex = hostInput.indexOf('/');
        if (pathIndex >= 0) {
            lastAddressEditLength = text.size();
            lastAddressEditWasDeletion = false;
            return;
        }
        QString match;
        for (const QString &entry : historyCompletionHosts) {
            if (entry.startsWith(hostInput, Qt::CaseInsensitive)) {
                match = entry;
                break;
            }
        }
        if (match.isEmpty() || match.compare(hostInput, Qt::CaseInsensitive) == 0) {
            lastAddressEditLength = text.size();
            lastAddressEditWasDeletion = false;
            return;
        }
        completingHistory = true;
        addressBar->setText(prefix + match);
        addressBar->setSelection(prefix.size() + hostInput.size(), match.size() - hostInput.size());
        lastAddressEditLength = addressBar->text().size();
        completingHistory = false;
        lastAddressEditWasDeletion = false;
    });
    refreshHistoryCompleter();
    addBookmark = addressBar->addAction(QIcon::fromTheme("emblem-favorite", QIcon(":/icons/emblem-favorite.png")),
                                        QLineEdit::TrailingPosition);
    addBookmark->setToolTip(tr("Add bookmark"));
    connect(addressBar, &QLineEdit::returnPressed, this, &MainWindow::openFromAddressBar);
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
    findMatchCase = new QToolButton(this);
    findMatchCase->setText(QStringLiteral("Aa"));
    findMatchCase->setToolTip(tr("Match case"));
    findMatchCase->setCheckable(true);
    findMatchCase->setAutoRaise(true);
    connect(findMatchCase, &QToolButton::toggled, this, &MainWindow::findForward);
    toolBar->addWidget(findMatchCase);
    findMatches = new QLabel(this);
    findMatchesAction = toolBar->addWidget(findMatches);
    findMatchesAction->setVisible(false);
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
    zoomPercentAction->setToolTip(tr("Reset zoom for this site"));
    connect(zoomout, &QAction::triggered, this, [this] {
        setSiteZoom(currentZoomPercent() - 10);
    });
    connect(zoomin, &QAction::triggered, this, [this] {
        setSiteZoom(currentZoomPercent() + 10);
    });
    connect(zoomPercentAction, &QAction::triggered, this, [this] {
        setSiteZoom(zoomPercent);
    });
    applyZoom();
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

// Display a URL in the current view.
void MainWindow::displaySite(QString url, const QString &title)
{
    if (url.isEmpty()) {
        url = homeAddress;
    }
    if (QFile::exists(url)) {
        url = QFileInfo(url).absoluteFilePath();
    }
    QUrl qurl = QUrl::fromUserInput(url);
    auto *view = currentWebView();
    if (!view) {
        progressBar->hide();
        setWindowTitle(title);
        return;
    }
    view->setUrl(qurl);
    view->show();
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

void MainWindow::loadSettings()
{
    // Load first from system .conf file and then overwrite with CLI switches where available
    websettings->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    websettings->setAttribute(QWebEngineSettings::DnsPrefetchEnabled, true);
    webProfile->setHttpAcceptLanguage(QLocale::system().name());
    setupSpellCheck();

    homeAddress = settings.value("Home", "https://start.duckduckgo.com").toString();
    showProgress = settings.value("ShowProgressBar", false).toBool();
    openNewTabWithHome = settings.value("OpenNewTabWithHome", true).toBool();
    zoomPercent = settings.value("ZoomPercent", 100).toInt();
    cookiesEnabled = settings.value("EnableCookies", true).toBool();
    clearCookiesAtExit = settings.value("ClearCookiesAtExit", false).toBool();
    searchEngine = settings.value("SearchEngine", "DuckDuckGo").toString();
    searchEngineCustom = settings.value("SearchEngineCustom", QString()).toString();
    if (!settings.contains("EnableJavaScript") && settings.contains("DisableJava")) {
        settings.setValue("EnableJavaScript", !settings.value("DisableJava", false).toBool());
    }

    applyWebSettings();

    QSize size {defaultWidth, defaultHeight};
    const bool canRestore = settings.contains("Geometry") && (!args || !args->isSet("full-screen"));
    if (canRestore) {
        const bool restored = restoreGeometry(settings.value("Geometry").toByteArray());
        if (!restored) {
            resize(size);
            centerWindow();
        }
    } else {
        resize(size);
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
    const QList<std::pair<QString, QString>> shortcuts {
        {tr("Ctrl+T"), tr("New tab")},
        {tr("Ctrl+Shift+N"), tr("New private window")},
        {tr("Ctrl+W"), tr("Close tab")},
        {tr("Ctrl+Shift+T"), tr("Reopen closed tab")},
        {tr("Ctrl+Tab, Ctrl+PgDn"), tr("Next tab")},
        {tr("Ctrl+Shift+Tab, Ctrl+PgUp"), tr("Previous tab")},
        {tr("Ctrl+1 … Ctrl+9"), tr("Go to tab 1 … 9")},
        {tr("Ctrl+L, Alt+D, F6"), tr("Focus the address bar")},
        {tr("Alt+←, Alt+→"), tr("Back/Forward")},
        {tr("Alt+Home"), tr("Home page")},
        {tr("Ctrl+R, F5"), tr("Reload")},
        {tr("Esc"), tr("Stop loading/clear Find field")},
        {tr("Ctrl+F, F3, /"), tr("Find")},
        {tr("Shift+F3"), tr("Find previous")},
        {tr("Ctrl++, Ctrl+-"), tr("Zoom in/out")},
        {tr("Ctrl+0"), tr("Reset zoom")},
        {tr("Ctrl+D"), tr("Bookmark current address")},
        {tr("Ctrl+Shift+O"), tr("Manage bookmarks")},
        {tr("Ctrl+H"), tr("History")},
        {tr("Ctrl+Shift+Del"), tr("Clear browsing data")},
        {tr("Ctrl+J"), tr("Downloads")},
        {tr("Ctrl+O"), tr("Browse file to open")},
        {tr("Ctrl+S"), tr("Save page")},
        {tr("Ctrl+P"), tr("Print")},
        {tr("Ctrl+,"), tr("Settings")},
        {tr("F10"), tr("Menu")},
        {tr("F9, Ctrl+Alt+R"), tr("Reader view")},
        {tr("F11"), tr("Full screen")},
        {tr("F12"), tr("Developer Tools")},
        {tr("F1, ?"), tr("Open this help dialog")},
    };
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Keyboard Shortcuts"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *table = new QTableWidget(static_cast<int>(shortcuts.size()), 2, &dialog);
    table->setHorizontalHeaderLabels({tr("Shortcut"), tr("Action")});
    table->verticalHeader()->hide();
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setFocusPolicy(Qt::NoFocus);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    for (int row = 0; row < shortcuts.size(); ++row) {
        table->setItem(row, 0, new QTableWidgetItem(shortcuts.at(row).first));
        table->setItem(row, 1, new QTableWidgetItem(shortcuts.at(row).second));
    }
    table->resizeColumnsToContents();
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.resize(480, 560);
    dialog.exec();
}

bool MainWindow::isLocalHostInput(const QString &input) const
{
    const QString lower = input.toLower();
    if (lower == "localhost") {
        return true;
    }
    if (lower == "localhost.localdomain") {
        return true;
    }
    if (lower.endsWith(".local")) {
        return true;
    }
    if (lower == "127.0.0.1" || lower == "::1") {
        return true;
    }
    return false;
}

QString MainWindow::searchUrlForQuery(const QString &query) const
{
    const QByteArray encoded = QUrl::toPercentEncoding(query);
    if (searchEngine == "Custom" && !searchEngineCustom.isEmpty()) {
        const QString encodedText = QString::fromLatin1(encoded);
        if (searchEngineCustom.contains("%s")) {
            return QString(searchEngineCustom).replace("%s", encodedText);
        }
        if (searchEngineCustom.contains("?q=")) {
            return searchEngineCustom + encodedText;
        }
        return searchEngineCustom;
    }
    if (searchEngine == "Google") {
        return QString::fromLatin1("https://www.google.com/search?q=%1").arg(QString::fromLatin1(encoded));
    }
    if (searchEngine == "Bing") {
        return QString::fromLatin1("https://www.bing.com/search?q=%1").arg(QString::fromLatin1(encoded));
    }
    return QString::fromLatin1("https://duckduckgo.com/?q=%1").arg(QString::fromLatin1(encoded));
}

void MainWindow::displaySearchResults(const QString &query)
{
    const QString searchUrl = searchUrlForQuery(query);
    lastAddressMaySearch = false;
    displaySite(searchUrl, query);
}

void MainWindow::openFromAddressBar()
{
    openFromAddressBarText(addressBar->text());
}

void MainWindow::openFromAddressBarText(const QString &inputText)
{
    const QString input = inputText.trimmed();
    if (input.isEmpty()) {
        return;
    }
    if (input.startsWith("http://", Qt::CaseInsensitive) || input.startsWith("https://", Qt::CaseInsensitive)) {
        lastAddressInput = input;
        lastAddressUrl = QUrl::fromUserInput(input);
        lastAddressMaySearch = false;
        lastAddressExplicitScheme = true;
        displaySite(input);
        return;
    }
    if (QFile::exists(input)) {
        displaySite(input, input);
        return;
    }
    const bool hasExplicitScheme = input.contains("://");
    const bool hasSpace = input.contains(' ');
    const bool hasDot = input.contains('.');
    if (hasSpace || (!hasDot && !hasExplicitScheme && !isLocalHostInput(input))) {
        displaySearchResults(input);
        return;
    }
    QUrl qurl = QUrl::fromUserInput(input);
    lastAddressInput = input;
    lastAddressUrl = qurl;
    lastAddressMaySearch = true;
    lastAddressExplicitScheme = hasExplicitScheme;
    displaySite(input);
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
            if (buffer.open(QIODevice::WriteOnly)) {
                iconPixmap.save(&buffer, "PNG");
                settings.setValue("icon", iconByteArray);
            }
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
    connect(webProfile, &QWebEngineProfile::downloadRequested, downloadWidget,
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
    applyZoom();
}

void MainWindow::showFullScreenNotification()
{
    constexpr int distanceTop = 100;
    constexpr int durationMs = 800;
    constexpr double start = 0;
    constexpr double end = 0.85;
    auto *label = new QLabel(this);
    auto *effect = new QGraphicsOpacityEffect;
    label->setGraphicsEffect(effect);
    label->setStyleSheet("padding: 15px; background-color:#787878; color:white");
    label->setText(tr("Press [F11] to exit full screen"));
    label->adjustSize();
    label->move(QApplication::primaryScreen()->geometry().width() / 2 - label->width() / 2, distanceTop);
    auto *a = new QPropertyAnimation(effect, "opacity");
    a->setDuration(durationMs);
    a->setStartValue(start);
    a->setEndValue(end);
    a->setEasingCurve(QEasingCurve::InBack);
    a->start(QPropertyAnimation::DeleteWhenStopped);
    label->show();
    QTimer::singleShot(4000, this, [label, effect, end, start] {
        auto *a = new QPropertyAnimation(effect, "opacity");
        a->setDuration(durationMs);
        a->setStartValue(end);
        a->setEndValue(start);
        a->setEasingCurve(QEasingCurve::OutBack);
        a->start(QPropertyAnimation::DeleteWhenStopped);
        connect(a, &QPropertyAnimation::finished, label, &QLabel::deleteLater);
    });
}

void MainWindow::tabChanged()
{
    // The match count belongs to the previous tab's search.
    if (findMatchesAction) {
        findMatchesAction->setVisible(false);
    }
    if (pageFullScreen && currentWebView() != pageFullScreenView) {
        exitPageFullScreen();
    }
    if (!currentWebView()) {
        // The last tab just closed; its page (and these actions) will be
        // deleted shortly, so drop the references before they dangle.
        backAction = forwardAction = stopAction = nullptr;
        return;
    }
    auto *back = pageAction(QWebEnginePage::Back);
    auto *forward = pageAction(QWebEnginePage::Forward);
    auto *reload = pageAction(QWebEnginePage::Reload);
    auto *stop = pageAction(QWebEnginePage::Stop);
    back->setShortcut(QKeySequence::Back);
    forward->setShortcut(QKeySequence::Forward);
    stop->setShortcut(QKeySequence::Cancel);
    toolBar->setUpdatesEnabled(false);
    // Anchor inserts on the permanent reloadAction/homeAction rather than the
    // previous tab's (about-to-be-replaced) actions, so this stays correct
    // even right after the last tab closed and backAction/etc. were nulled.
    toolBar->insertAction(reloadAction, back);
    toolBar->insertAction(reloadAction, forward);
    toolBar->insertAction(homeAction, stop);
    if (backAction) {
        toolBar->removeAction(backAction);
    }
    if (forwardAction) {
        toolBar->removeAction(forwardAction);
    }
    if (stopAction) {
        toolBar->removeAction(stopAction);
    }
    backAction = back;
    forwardAction = forward;
    stopAction = stop;
    disconnect(stopAction, &QAction::triggered, this, nullptr);
    connect(stopAction, &QAction::triggered, this, [this] { done(true); });
    toolBar->setUpdatesEnabled(true);
    if (reloadAction) {
        reloadAction->setIcon(reload->icon());
        reloadAction->setText(reload->text());
        reloadAction->setToolTip(reload->toolTip());
        reloadAction->setEnabled(reload->isEnabled());
    }
    // Each tab brings its own page actions with the theme's icons.
    const QPalette palette = toolBar->palette();
    for (QAction *action : {backAction, forwardAction, stopAction, reloadAction}) {
        if (action) {
            action->setIcon(iconForBackground(action->icon(), palette.color(QPalette::Window),
                                              palette.color(QPalette::WindowText)));
        }
    }
    addressBar->setText(currentWebView()->url().scheme() == "mx-newtab" ? QString() : currentWebView()->url().toString());
    if (addressBar->text().isEmpty()) {
        addressBar->setFocus();
    }
    setWindowTitle(currentWebView()->title());
    setConnections();
    if (devToolsWindow && devToolsView) {
        currentWebView()->page()->setDevToolsPage(devToolsView->page());
    }
}

// Show the hovered URL in the status bar and connect it to launch it.
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

    setupMenuConnections(menu);
}

void MainWindow::addFileMenuActions(QMenu *menu)
{
    QAction *newTab {nullptr};
    menu->addAction(newTab = new QAction(QIcon::fromTheme("tab-new"), tr("&New tab")));
    newTab->setShortcut(Qt::CTRL | Qt::Key_T);
    connect(newTab, &QAction::triggered, this, [this] { addNewTab(); });

    auto *savePage = new QAction(QIcon::fromTheme("document-save-as"), tr("&Save page..."), this);
    savePage->setShortcut(QKeySequence::Save);
    menu->addAction(savePage);
    addAction(savePage);
    // Goes through downloadRequested, where the download widget asks for the file name.
    connect(savePage, &QAction::triggered, this, [this] {
        if (auto *view = currentWebView()) {
            view->triggerPageAction(QWebEnginePage::SavePage);
        }
    });

    auto *print = new QAction(QIcon::fromTheme("document-print"), tr("&Print..."), this);
    print->setShortcut(QKeySequence::Print);
    menu->addAction(print);
    addAction(print);
    connect(print, &QAction::triggered, this, [this] { printPage(currentWebView()); });

    auto *privateAction = new QAction(QIcon::fromTheme("view-private"), tr("New p&rivate window"), this);
    privateAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));
    menu->addAction(privateAction);
    addAction(privateAction);
    connect(privateAction, &QAction::triggered, this, &MainWindow::openPrivateWindow);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && isActiveWindow() && !privateWindow) {
        lastActiveWindow = this;
    }
    if (event->type() == QEvent::PaletteChange && menuButton) {
        adaptIcons();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::openFromOtherInstance(const QString &argument)
{
    // Links from other applications never go to a private window.
    MainWindow *target = lastActiveWindow;
    if (!target || !target->isVisible()) {
        target = nullptr;
        const auto widgets = QApplication::topLevelWidgets();
        for (auto *widget : widgets) {
            auto *window = qobject_cast<MainWindow *>(widget);
            if (window && !window->privateWindow && window->isVisible()) {
                target = window;
                break;
            }
        }
    }
    QUrl url = argument.isEmpty() ? QUrl() : QUrl::fromUserInput(argument);
    // Any local process can send this, so only ordinary locations are opened, never internal pages
    // such as mx-history://clear; anything else just opens a new tab.
    static const QStringList allowedSchemes {"http", "https", "file", "ftp"};
    if (!url.isEmpty() && !allowedSchemes.contains(url.scheme())) {
        url.clear();
    }
    if (!target) {
        target = new MainWindow(url, false);
        // Like a first launch: restored tabs replace the start page, so the link needs its own tab.
        if (target->restoredTabs && !url.isEmpty()) {
            target->addNewTab(url, true);
        }
        target->show();
    } else {
        target->addNewTab(url, true);
        if (target->isMinimized()) {
            target->showNormal();
        }
    }
    target->raise();
    target->activateWindow();
}

void MainWindow::openPrivateWindow()
{
    auto *window = new MainWindow(QUrl(), true);
    window->move(pos() + QPoint(40, 40));
    window->show();
}

void MainWindow::printPage(WebView *webView)
{
    QPointer<WebView> view = webView;
    if (!view || printingView) {
        return;
    }
    // QWebEngineView::print() is asynchronous. The printer is owned by a connection whose context is
    // the view, so it stays alive until printFinished and is never freed before the view itself.
    auto printer = std::make_shared<QPrinter>(QPrinter::HighResolution);
    // Print to file defaults to the user's Downloads folder, named after the page, rather than the
    // working directory.
    QString folder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (folder.isEmpty() || !QDir(folder).exists()) {
        folder = QDir::homePath();
    }
    static const QRegularExpression unsafe(QStringLiteral("[/\\\\\\x00-\\x1f]"));
    QString name = view->title().simplified().replace(unsafe, QStringLiteral("_")).left(100);
    // File names are limited to 255 bytes, and a title in a non-Latin script takes up to 3 bytes per
    // character; leave room for ".pdf" and never cut a surrogate pair.
    while (!name.isEmpty() && name.back().isHighSurrogate()) {
        name.chop(1);
    }
    while (name.toUtf8().size() > 250) {
        name.chop(name.size() >= 2 && name.back().isLowSurrogate() ? 2 : 1);
    }
    while (name.startsWith(QLatin1Char('.'))) {
        name.remove(0, 1);
    }
    if (name.isEmpty()) {
        name = view->url().host().isEmpty() ? QStringLiteral("page") : view->url().host();
    }
    printer->setOutputFileName(QDir(folder).filePath(name + QStringLiteral(".pdf")));
    // A .pdf name switches the printer to PDF; switch back so a real printer, if any, stays the default.
    printer->setOutputFormat(QPrinter::NativeFormat);
    QPrintDialog dialog(printer.get(), this);
    dialog.setWindowTitle(tr("Print page"));
    if (dialog.exec() != QDialog::Accepted || !view) {
        return;
    }
    printingView = view;
    QPointer<MainWindow> self = this;
    connect(
        view, &QWebEngineView::printFinished, view,
        [self, view, printer](bool success) {
            if (self) {
                self->printingView = nullptr;
            }
            if (!success && view) {
                QMessageBox::warning(view, tr("Print page"), tr("Printing failed."));
            }
        },
        Qt::SingleShotConnection);
    view->print(printer.get());
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
    auto *readerAction = new QAction(QIcon::fromTheme("view-readermode"), tr("&Reader view"), this);
    readerAction->setShortcuts({Qt::Key_F9, Qt::CTRL | Qt::ALT | Qt::Key_R});
    menu->addAction(readerAction);
    addAction(readerAction);
    connect(readerAction, &QAction::triggered, this, &MainWindow::toggleReaderMode);
    // The same key leaves reader view, so the item names what it will do for this tab.
    connect(menu, &QMenu::aboutToShow, readerAction, [this, readerAction] {
        const WebView *view = currentWebView();
        readerAction->setText(view && view->isReaderMode() ? tr("Exit &reader view") : tr("&Reader view"));
    });
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
    // Bookmarks are saved when a window closes; a private window must not overwrite the regular list.
    if (privateWindow) {
        addBookmark->setEnabled(false);
        manageBookmarks->setEnabled(false);
    }
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
    settingsAction->setShortcuts({Qt::CTRL | Qt::Key_Comma, QKeySequence::Preferences});
    menu->addSeparator();
    menu->addAction(help = new QAction(QIcon::fromTheme("help-contents"), tr("&Keyboard shortcuts")));
    help->setShortcut(QKeySequence::HelpContents);
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
    openSettingsPage();
}

QString MainWindow::buildSettingsPageHtml()
{
    const bool spatialNav = settings.value("SpatialNavigation", false).toBool();
    const bool enableJs = settings.value("EnableJavaScript", true).toBool();
    const bool loadImages = settings.value("LoadImages", true).toBool();
    const bool enableCookies = settings.value("EnableCookies", true).toBool();
    const bool enableThirdPartyCookies = settings.value("EnableThirdPartyCookies", true).toBool();
    const bool allowPopups = settings.value("AllowPopups", true).toBool();
    const bool saveTabs = settings.value("SaveTabs", false).toBool();
    const bool clearCookiesAtExit = settings.value("ClearCookiesAtExit", false).toBool();

    auto check = [](bool value) { return value ? QStringLiteral("checked") : QString(); };
    auto disabled = [](bool value) { return value ? QString() : QStringLiteral("disabled"); };

    QString cacheSizeText = clearingCache ? tr("Clearing...") : tr("unknown");
    if (!clearingCache) {
        const auto *profile = webProfile;
        const QStringList cacheCandidates = collectCachePaths(profile);
        const qint64 cacheBytes = totalDirectorySize(cacheCandidates);
        if (cacheBytes >= 0) {
            if (cacheBytes < 1024) {
                cacheSizeText = tr("Cleared");
            } else {
                cacheSizeText = DownloadWidget::withUnit(cacheBytes);
            }
        }
    }

    const QString html = QStringLiteral(R"(<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="cache-control" content="no-cache">
  <title>%1</title>
  <style>
    :root { color-scheme: light; }
    body { font-family: sans-serif; margin: 24px; color: #1f2328; background: #ffffff; }
    h1 { font-size: 22px; margin: 0 0 12px; }
    form { display: grid; gap: 12px; max-width: 720px; }
    label { font-weight: 600; display: block; margin-bottom: 4px; }
    .row { display: grid; gap: 6px; }
    .input, select { padding: 8px 10px; border: 1px solid #d0d7de; border-radius: 6px; }
    .check { display: flex; gap: 8px; align-items: center; }
    .check-row { display: flex; gap: 12px; align-items: center; }
    .cache-label { font-weight: 600; }
    .actions { display: flex; gap: 12px; margin-top: 8px; }
    .btn { padding: 8px 12px; border: 1px solid #d0d7de; background: #f6f8fa; border-radius: 6px; cursor: pointer; }
    .btn-inline { padding: 4px 8px; font-size: 12px; }
  </style>
</head>
<body>
  <h1>%1</h1>
  <form id="settings" action="mx-settings://save" method="get">
    <div class="row">
      <label for="home">%2</label>
      <input id="home" name="home" class="input" type="text" value="%3">
    </div>
    <div class="row">
      <label for="search">%4</label>
      <select id="search" name="search" class="input">
        <option value="DuckDuckGo" %5>%6</option>
        <option value="Google" %7>%8</option>
        <option value="Bing" %9>%10</option>
        <option value="Custom" %11>%12</option>
      </select>
    </div>
    <div class="row">
      <label for="customSearch">%13</label>
      <input id="customSearch" name="customSearch" class="input" type="text" value="%14" placeholder="https://example.com/?q=%s">
    </div>
    <div class="row">
      <label for="zoom">%15</label>
      <input id="zoom" name="zoom" class="input" type="number" min="25" max="500" value="%16">
    </div>
    <label class="check"><input id="openNewTab" name="openNewTab" type="checkbox" value="1" %17> %18</label>
    <label class="check"><input id="showProgress" name="showProgress" type="checkbox" value="1" %19> %20</label>
    <label class="check"><input id="spatialNav" name="spatialNav" type="checkbox" value="1" %21> %22</label>
    <label class="check"><input id="enableJs" name="enableJs" type="checkbox" value="1" %23> %24</label>
    <label class="check"><input id="loadImages" name="loadImages" type="checkbox" value="1" %25> %26</label>
    <div class="check-row">
      <label class="check"><input id="enableCookies" name="enableCookies" type="checkbox" value="1" %27> %28</label>
      <button class="btn btn-inline" id="clearCookies" type="button">%41</button>
    </div>
    <label class="check"><input id="thirdPartyCookies" name="thirdPartyCookies" type="checkbox" value="1" %29 %30> %31</label>
    <label class="check"><input id="clearCookiesAtExit" name="clearCookiesAtExit" type="checkbox" value="1" %32> %33</label>
    <label class="check"><input id="allowPopups" name="allowPopups" type="checkbox" value="1" %34> %35</label>
    <label class="check"><input id="saveTabs" name="saveTabs" type="checkbox" value="1" %36> %37</label>
    <div class="check-row">
      <div class="cache-label">%42</div>
      <button class="btn btn-inline" id="clearCache" type="button">%43</button>
    </div>
    <div class="actions">
      <button class="btn" id="save" type="submit">%38</button>
      <button class="btn" id="reset" type="reset">%39</button>
    </div>
  </form>
  <script>
    const form = document.getElementById('settings');
    const saveBtn = document.getElementById('save');
    const resetBtn = document.getElementById('reset');
    const searchSelect = document.getElementById('search');
    const customSearch = document.getElementById('customSearch');
    const inputs = Array.from(form.querySelectorAll('input, select'));
    saveBtn.disabled = true;
    resetBtn.disabled = true;
    window.mxSettingsDirty = false;
    function snapshot() {
      const data = {};
      inputs.forEach(el => {
        if (!el.id) {
          return;
        }
        data[el.id] = el.type === 'checkbox' ? el.checked : el.value;
      });
      return JSON.stringify(data);
    }
    let baseline = snapshot();
    function updateDirtyState() {
      const dirty = snapshot() !== baseline;
      window.mxSettingsDirty = dirty;
      saveBtn.disabled = !dirty;
      resetBtn.disabled = !dirty;
    }
    function boolValue(id) {
      return document.getElementById(id).checked ? '1' : '0';
    }
    function normalizeCustomUrl(url) {
      if (!url.includes('%s') && !url.includes('?q=')) {
        const add = confirm('%40');
        if (add) {
          const join = url.includes('?') ? '&' : '?';
          return url + join + 'q=%s';
        }
      }
      return url;
    }
    function syncCustom() {
      const isCustom = searchSelect.value === 'Custom';
      customSearch.disabled = !isCustom;
    }
    function saveSettings(event) {
      if (event) {
        event.preventDefault();
      }
      if (saveBtn.disabled) {
        return;
      }
      let customUrl = customSearch.value.trim();
      if (searchSelect.value === 'Custom' && customUrl.length > 0) {
        customUrl = normalizeCustomUrl(customUrl);
        customSearch.value = customUrl;
      }
      const params = new URLSearchParams();
      params.set('home', document.getElementById('home').value);
      params.set('search', document.getElementById('search').value);
      params.set('customSearch', customUrl);
      params.set('zoom', document.getElementById('zoom').value);
      params.set('openNewTab', boolValue('openNewTab'));
      params.set('showProgress', boolValue('showProgress'));
      params.set('spatialNav', boolValue('spatialNav'));
      params.set('enableJs', boolValue('enableJs'));
      params.set('loadImages', boolValue('loadImages'));
      params.set('enableCookies', boolValue('enableCookies'));
      params.set('thirdPartyCookies', boolValue('thirdPartyCookies'));
      params.set('allowPopups', boolValue('allowPopups'));
      params.set('saveTabs', boolValue('saveTabs'));
      params.set('clearCookiesAtExit', boolValue('clearCookiesAtExit'));
      baseline = snapshot();
      updateDirtyState();
      location.href = 'mx-settings://save?' + params.toString();
    }
    form.addEventListener('submit', saveSettings);
    resetBtn.addEventListener('click', event => {
      event.preventDefault();
      location.href = 'mx-settings://list';
    });
    const clearCookiesBtn = document.getElementById('clearCookies');
    clearCookiesBtn.addEventListener('click', event => {
      event.preventDefault();
      if (confirm('%44')) {
        location.href = 'mx-settings://clearCookies';
      }
    });
    const clearCacheBtn = document.getElementById('clearCache');
    clearCacheBtn.addEventListener('click', event => {
      event.preventDefault();
      if (confirm('%45')) {
        location.href = 'mx-settings://clearCache';
      }
    });
    searchSelect.addEventListener('change', syncCustom);
    inputs.forEach(el => {
      el.addEventListener('input', updateDirtyState);
      el.addEventListener('change', updateDirtyState);
    });
    const cookiesToggle = document.getElementById('enableCookies');
    const thirdPartyToggle = document.getElementById('thirdPartyCookies');
    function syncThirdParty() {
      thirdPartyToggle.disabled = !cookiesToggle.checked;
      if (!cookiesToggle.checked) {
        thirdPartyToggle.checked = false;
      }
    }
    syncCustom();
    cookiesToggle.addEventListener('change', syncThirdParty);
    syncThirdParty();
    baseline = snapshot();
    updateDirtyState();
  </script>
</body>
</html>)")
                            .arg(tr("Settings").toHtmlEscaped(),
                                 tr("Home address").toHtmlEscaped(),
                                 homeAddress.toHtmlEscaped(),
                                 tr("Search engine").toHtmlEscaped(),
                                 searchEngine == "DuckDuckGo" ? QStringLiteral("selected") : QString(),
                                 tr("DuckDuckGo").toHtmlEscaped(),
                                 searchEngine == "Google" ? QStringLiteral("selected") : QString(),
                                 tr("Google").toHtmlEscaped(),
                                 searchEngine == "Bing" ? QStringLiteral("selected") : QString(),
                                 tr("Bing").toHtmlEscaped(),
                                 searchEngine == "Custom" ? QStringLiteral("selected") : QString(),
                                 tr("Custom").toHtmlEscaped(),
                                 tr("Custom search URL").toHtmlEscaped(),
                                 searchEngineCustom.toHtmlEscaped(),
                                 tr("Zoom level").toHtmlEscaped(),
                                 QString::number(zoomPercent),
                                 check(openNewTabWithHome),
                                 tr("Open new tabs with home page").toHtmlEscaped(),
                                 check(showProgress),
                                 tr("Show progress bar").toHtmlEscaped(),
                                 check(spatialNav),
                                 tr("Enable spatial navigation").toHtmlEscaped(),
                                 check(enableJs),
                                 tr("Enable JavaScript").toHtmlEscaped(),
                                 check(loadImages),
                                 tr("Load images").toHtmlEscaped(),
                                 check(enableCookies),
                                 tr("Enable cookies").toHtmlEscaped(),
                                 check(enableThirdPartyCookies),
                                 disabled(enableCookies),
                                 tr("Enable third-party cookies").toHtmlEscaped(),
                                 check(clearCookiesAtExit),
                                 tr("Clear cookies at exit").toHtmlEscaped(),
                                 check(allowPopups),
                                 tr("Allow pop-up windows").toHtmlEscaped(),
                                 check(saveTabs),
                                 tr("Save tabs on closing").toHtmlEscaped(),
                                 tr("Save settings").toHtmlEscaped(),
                                 tr("Reset").toHtmlEscaped(),
                                 tr("Custom URL has no ?q= or %s placeholder. Append ?q=%s automatically?")
                                     .toHtmlEscaped(),
                                 tr("Clear cookies").toHtmlEscaped(),
                                 tr("Cache size: %1").arg(cacheSizeText).toHtmlEscaped(),
                                 tr("Clear cache").toHtmlEscaped(),
                                 tr("Clear all cookies?").toHtmlEscaped(),
                                 tr("Clear the cache?").toHtmlEscaped());

    return html;
}

void MainWindow::renderSettingsPage(WebView *view)
{
    if (!view) {
        return;
    }
    const QString ts = QString::number(QDateTime::currentMSecsSinceEpoch());
    view->setHtml(buildSettingsPageHtml(), QUrl("mx-settings://list?ts=" + ts));
    view->show();
    tabWidget->setTabTitle(tabWidget->indexOf(view), tr("Settings"));
    setWindowTitle(tr("Settings"));
    updateUrl();
}

void MainWindow::openSettingsPage()
{
    if (auto *view = currentWebView()) {
        if (view->url().scheme() == "mx-settings") {
            renderSettingsPage(view);
            return;
        }
    }
    auto *view = tabWidget->createTab(true);
    if (!view) {
        return;
    }
    setConnections();
    renderSettingsPage(view);
}

bool MainWindow::handleSettingsRequest(const QUrl &url)
{
    if (url.scheme() != "mx-settings") {
        return false;
    }
    const QString action = url.host();
    if (action == "clearcookies") {
        webProfile->cookieStore()->deleteAllCookies();
        renderSettingsPage(currentWebView());
        return true;
    } else if (action == "clearcache") {
        clearingCache = true;
        renderSettingsPage(currentWebView());
        webProfile->clearHttpCache();
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
        connect(webProfile, &QWebEngineProfile::clearHttpCacheCompleted, this, [this]() {
            clearingCache = false;
            if (auto *view = currentWebView()) {
                if (view->url().scheme() == "mx-settings") {
                    renderSettingsPage(view);
                }
            }
        }, Qt::SingleShotConnection);
#else
        // Qt < 6.7 doesn't have clearHttpCacheCompleted signal, use timer fallback
        QTimer::singleShot(500, this, [this]() {
            clearingCache = false;
            if (auto *view = currentWebView()) {
                if (view->url().scheme() == "mx-settings") {
                    renderSettingsPage(view);
                }
            }
        });
#endif
        return true;
    }
    if (action == "list") {
        renderSettingsPage(currentWebView());
        return true;
    }
    if (action != "save") {
        return false;
    }
    QUrlQuery query(url);
    const QString newHome = QUrl::fromPercentEncoding(query.queryItemValue("home").toUtf8()).trimmed();
    const QString newSearch = query.queryItemValue("search").trimmed();
    const QString newCustomSearch = QUrl::fromPercentEncoding(query.queryItemValue("customSearch").toUtf8()).trimmed();
    const int newZoom = query.queryItemValue("zoom").toInt();
    const bool newOpenNewTab = query.queryItemValue("openNewTab") == "1";
    const bool newShowProgress = query.queryItemValue("showProgress") == "1";
    const bool newSpatialNav = query.queryItemValue("spatialNav") == "1";
    const bool newEnableJs = query.queryItemValue("enableJs") == "1";
    const bool newLoadImages = query.queryItemValue("loadImages") == "1";
    const bool newEnableCookies = query.queryItemValue("enableCookies") == "1";
    bool newThirdParty = query.queryItemValue("thirdPartyCookies") == "1";
    const bool newAllowPopups = query.queryItemValue("allowPopups") == "1";
    const bool newSaveTabs = query.queryItemValue("saveTabs") == "1";
    const bool newClearCookiesAtExit = query.queryItemValue("clearCookiesAtExit") == "1";
    if (!newEnableCookies) {
        newThirdParty = false;
    }

    homeAddress = newHome.isEmpty() ? homeAddress : newHome;
    if (!newSearch.isEmpty()) {
        searchEngine = newSearch;
    }
    searchEngineCustom = newCustomSearch;
    settings.setValue("SearchEngineCustom", searchEngineCustom);
    openNewTabWithHome = newOpenNewTab;
    showProgress = newShowProgress;
    settings.setValue("Home", homeAddress);
    settings.setValue("SearchEngine", searchEngine);
    settings.setValue("OpenNewTabWithHome", openNewTabWithHome);
    settings.setValue("ShowProgressBar", showProgress);
    settings.setValue("SpatialNavigation", newSpatialNav);
    settings.setValue("EnableJavaScript", newEnableJs);
    settings.setValue("LoadImages", newLoadImages);
    settings.setValue("EnableCookies", newEnableCookies);
    settings.setValue("EnableThirdPartyCookies", newThirdParty);
    settings.setValue("AllowPopups", newAllowPopups);
    settings.setValue("SaveTabs", newSaveTabs);
    clearCookiesAtExit = newClearCookiesAtExit;
    settings.setValue("ClearCookiesAtExit", newClearCookiesAtExit);
    if (newZoom > 0) {
        setZoomPercent(newZoom, true);
    }

    applyWebSettings();
    renderSettingsPage(currentWebView());
    return true;
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

    auto *profile = webProfile;
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

// Enable spell checking when a dictionary for the system language is installed
// (e.g. en_US.bdic from hunspell-en-us); an exact locale match wins over one for the language only.
void MainWindow::setupSpellCheck()
{
    const QString dir = qEnvironmentVariable("QTWEBENGINE_DICTIONARIES_PATH");
    if (dir.isEmpty()) {
        return;
    }
    const QStringList files = QDir(dir).entryList({"*.bdic"}, QDir::Files);
    const QString locale = QLocale::system().name();
    const QString language = locale.section('_', 0, 0);
    QString match;
    for (const QString &file : files) {
        const QString name = QFileInfo(file).completeBaseName();
        if (name == locale) {
            match = name;
            break;
        }
        if (match.isEmpty() && name.section('_', 0, 0) == language) {
            match = name;
        }
    }
    if (!match.isEmpty()) {
        webProfile->setSpellCheckLanguages({match});
        webProfile->setSpellCheckEnabled(true);
    }
}

// Sets the default zoom, used for sites without their own zoom level.
void MainWindow::setZoomPercent(int percent, bool persist)
{
    zoomPercent = qBound(minZoom, percent, maxZoom);
    if (persist) {
        settings.setValue("ZoomPercent", zoomPercent);
    }
    applyZoom();
}

// Zoom levels are remembered per host (per scheme for host-less pages such as file://).
QString MainWindow::zoomKey(const QUrl &url)
{
    const QString site = url.host().isEmpty() ? url.scheme() : url.host();
    return site.isEmpty() ? QString() : "SiteZoom/" + site;
}

int MainWindow::currentZoomPercent() const
{
    const auto *view = tabWidget->currentWebView();
    return view ? qRound(view->zoomFactor() * 100) : zoomPercent;
}

void MainWindow::setSiteZoom(int percent)
{
    auto *view = currentWebView();
    if (!view) {
        return;
    }
    percent = qBound(minZoom, percent, maxZoom);
    const QString key = zoomKey(view->url());
    if (key.isEmpty()) {
        view->setZoomFactor(percent / 100.0);
        if (zoomPercentAction) {
            zoomPercentAction->setText(QString::number(percent) + "%");
        }
        return;
    }
    if (privateWindow) {
        privateZoom.insert(key, percent);
    } else if (percent == zoomPercent) {
        settings.remove(key);
    } else {
        settings.setValue(key, percent);
    }
    applyZoom();
}

void MainWindow::applyZoom()
{
    auto *view = currentWebView();
    if (!view) {
        return;
    }
    const QString key = zoomKey(view->url());
    int percent = zoomPercent;
    if (!key.isEmpty()) {
        percent = privateZoom.contains(key) ? privateZoom.value(key) : settings.value(key, zoomPercent).toInt();
        percent = qBound(minZoom, percent, maxZoom);
    }
    if (qRound(view->zoomFactor() * 100) != percent) {
        view->setZoomFactor(percent / 100.0);
    }
    if (zoomPercentAction) {
        zoomPercentAction->setText(QString::number(percent) + "%");
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
    if (!tabWidget->closeCurrentTabByShortcut()) {
        close();
    }
}

void MainWindow::reopenClosedTab()
{
    if (!closedTabs.isEmpty()) {
        openSavedTab(closedTabs.takeLast().first, true);
    }
}

void MainWindow::openLinkInNewTab(const QUrl &url)
{
    addNewTab(url, false);
}

void MainWindow::searchInNewTab(const QString &text)
{
    addNewTab(QUrl(searchUrlForQuery(text)), true);
}

void MainWindow::handleFullScreenRequest(QWebEngineFullScreenRequest request, WebView *view)
{
    if (request.toggleOn()) {
        if (pageFullScreen || view != currentWebView()) {
            request.reject();
            return;
        }
        request.accept();
        pageFullScreen = true;
        pageFullScreenView = view;
        fullScreenBeforePage = isFullScreen();
        if (!fullScreenBeforePage) {
            normalGeometry = saveGeometry();
            showFullScreen();
        }
        toolBar->hide();
        // Auto-hide would show the tab bar again as soon as a tab is added in the background.
        // Turning it off shows the bar, so hide it afterwards.
        tabWidget->setTabBarAutoHide(false);
        tabWidget->tabBar()->hide();
        statusBar()->hide();
    } else {
        request.accept();
        restoreFromPageFullScreen();
    }
}

// Leave HTML5 fullscreen. The window is restored right away rather than waiting for the page's
// toggle-off request, which never arrives if the tab is being closed.
void MainWindow::exitPageFullScreen()
{
    if (!pageFullScreen) {
        return;
    }
    QPointer<WebView> view = pageFullScreenView;
    restoreFromPageFullScreen();
    if (view) {
        view->triggerPageAction(QWebEnginePage::ExitFullScreen);
    }
}

void MainWindow::restoreFromPageFullScreen()
{
    if (!pageFullScreen) {
        return;
    }
    pageFullScreen = false;
    pageFullScreenView = nullptr;
    // Turning auto-hide back on also shows the tab bar again when there is more than one tab.
    tabWidget->setTabBarAutoHide(true);
    if (!fullScreenBeforePage) {
        showNormal();
        if (!normalGeometry.isEmpty()) {
            restoreGeometry(normalGeometry);
        }
        toolBar->show();
    }
}

void MainWindow::toggleFullScreen()
{
    if (pageFullScreen) {
        exitPageFullScreen();
        return;
    }
    if (isFullScreen()) {
        showNormal();
        if (!normalGeometry.isEmpty()) {
            restoreGeometry(normalGeometry);
        }
        toolBar->show();
    } else {
        normalGeometry = saveGeometry();
        showFullScreen();
        toolBar->hide();
        showFullScreenNotification();
    }
}

void MainWindow::updateUrl()
{
    auto *view = currentWebView();
    if (!view) {
        addressBar->clear();
        addressBar->hide();
        return;
    }
    addressBar->show();
    addressBar->setText(view->url().scheme() == "mx-newtab" ? QString() : view->url().toDisplayString());
    addressBar->setCursorPosition(0);
    applyZoom();
}

bool MainWindow::restoreSavedTabs()
{
    int size = settings.beginReadArray("SavedTabs");
    if (size == 0) {
        settings.endArray();
        return false;
    }

    QList<QUrl> savedUrls;
    QList<bool> savedPinned;
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        QString url = settings.value("url").toString();
        if (!url.isEmpty()) {
            savedUrls.append(QUrl::fromUserInput(url));
            savedPinned.append(settings.value("pinned", false).toBool());
        }
    }
    settings.endArray();
    settings.remove("SavedTabs");

    if (!savedUrls.isEmpty()) {
        tabWidget->removeTab(0);
        openSavedTab(savedUrls.first(), true);
    }

    for (int i = 1; i < savedUrls.size(); ++i) {
        openSavedTab(savedUrls.at(i), false);
    }
    // Each saved entry opened one tab in order; pinning left to right keeps the saved order.
    for (int i = 0; i < savedPinned.size() && i < tabWidget->count(); ++i) {
        if (savedPinned.at(i)) {
            tabWidget->setPinned(i, true);
        }
    }
    return !savedUrls.isEmpty();
}

void MainWindow::openSavedTab(const QUrl &url, bool makeCurrent)
{
    if (url.scheme() == "mx-history") {
        auto *view = tabWidget->createTab(makeCurrent);
        if (!view) {
            return;
        }
        if (makeCurrent) {
            setConnections();
        }
        renderHistoryPage(view);
        return;
    }
    if (url.scheme() == "mx-settings") {
        auto *view = tabWidget->createTab(makeCurrent);
        if (!view) {
            return;
        }
        if (makeCurrent) {
            setConnections();
        }
        renderSettingsPage(view);
        return;
    }
    addNewTab(url, makeCurrent);
}

void MainWindow::focusAddressBar()
{
    if (addressBar) {
        addressBar->setFocus(Qt::ShortcutFocusReason);
        addressBar->selectAll();
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
    findInPage(QWebEnginePage::FindBackward);
}

void MainWindow::findForward()
{
    findInPage({});
}

void MainWindow::findInPage(QWebEnginePage::FindFlags flags)
{
    searchBox->setFocus();
    auto *view = currentWebView();
    if (!view) {
        return;
    }
    if (findMatchCase->isChecked()) {
        flags |= QWebEnginePage::FindCaseSensitively;
    }
    const QString text = searchBox->text();
    QPointer<MainWindow> self = this;
    QPointer<WebView> guard = view;
    view->findText(text, flags, [this, self, guard, text](const QWebEngineFindTextResult &result) {
        // Ignore results for a tab that is no longer shown or a search that was replaced.
        if (!self || !guard || guard != currentWebView() || text != searchBox->text()) {
            return;
        }
        if (text.isEmpty()) {
            findMatchesAction->setVisible(false);
            return;
        }
        findMatches->setText(result.numberOfMatches() == 0
                                 ? tr("No matches")
                                 : tr("%1 of %2").arg(result.activeMatch()).arg(result.numberOfMatches()));
        findMatchesAction->setVisible(true);
    });
}

// process keystrokes
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_H && event->modifiers() == Qt::ControlModifier) {
        openHistoryPage();
        return;
    }
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
    if (event->key() == Qt::Key_Escape && pageFullScreen) {
        exitPageFullScreen();
        return;
    }
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        toggleFullScreen();
        return;
    }
    if (event->matches(QKeySequence::Cancel) && searchBox->text().isEmpty()) {
        if (auto *view = currentWebView()) {
            view->setFocus();
        }
        return;
    }
    if (event->key() == Qt::Key_R && event->modifiers() == Qt::ControlModifier) {
        reloadCurrentView();
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
    if (clearCookiesAtExit) {
        webProfile->cookieStore()->deleteAllCookies();
    }
    if (privateWindow) {
        return;
    }
    settings.setValue("Geometry", saveGeometry());

    if (settings.value("SaveTabs", false).toBool()) {
        settings.beginWriteArray("SavedTabs");
        for (int i = 0; i < tabWidget->count(); ++i) {
            settings.setArrayIndex(i);
            auto *webView = qobject_cast<WebView *>(tabWidget->widget(i));
            if (webView) {
                settings.setValue("url", webView->url().toString());
                settings.setValue("pinned", tabWidget->isPinned(i));
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

void MainWindow::reloadCurrentView()
{
    auto *view = currentWebView();
    if (!view) {
        return;
    }
    if (view->url().scheme() == "mx-settings") {
        renderSettingsPage(view);
        return;
    }
    view->reloadPage();
}

// display progressbar while loading page
void MainWindow::loading()
{
    progressBar->setFixedHeight(progBarWidth);
    progressBar->setTextVisible(false);
    progressBar->move(geometry().width() / 2 - progressBar->width() / 2, geometry().height() - progBarVerticalAdj);
    progressBar->show();
    progressBar->setRange(0, 0);
}

// done loading
void MainWindow::done(bool ok)
{
    auto *view = currentWebView();
    if (!view) {
        searchBox->clear();
        progressBar->setRange(0, 100);
        progressBar->setValue(0);
        progressBar->hide();
        return;
    }
    // Navigation to another site may have reset the zoom.
    applyZoom();
    if (!ok && lastAddressMaySearch && !lastAddressExplicitScheme && view->url() == lastAddressUrl
        && !lastAddressInput.isEmpty()) {
        displaySearchResults(lastAddressInput);
        return;
    }
    if (!ok) {
        qDebug() << "Error loading:" << view->url().toString();
    }
    view->stop();
    view->setFocus();
    searchBox->clear();
    progressBar->setRange(0, 100);
    progressBar->setValue(0);
    progressBar->hide();
    tabWidget->setTabTitle(tabWidget->currentIndex(), view->title());
    setWindowTitle(view->title());
}
