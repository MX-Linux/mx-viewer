/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2023-2026 MX Authors
 *
 * Authors: Adrian <adrian@mxlinux.org>
 *          MX Linux <http://mxlinux.org>
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this package. If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/
#include "webview.h"
#include "mainwindow.h"

#include <algorithm>

#include <QApplication>
#include <QAuthenticator>
#include <QBuffer>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QTimer>
#include <QWebEngineCertificateError>
#include <QWebEngineContextMenuRequest>
#include <QWebEnginePermission>
#include <QWebEngineProfile>

// Static member definitions
bool WebView::s_ctrlHeld = false;
bool WebView::s_middleClick = false;
bool WebView::s_consumed = false;

WebPage::WebPage(QWebEngineProfile *profile, WebView *parent)
    : QWebEnginePage(profile, parent),
      m_webView(parent)
{
    connect(this, &QWebEnginePage::fullScreenRequested, this, [this](QWebEngineFullScreenRequest request) {
        auto *mw = qobject_cast<MainWindow *>(m_webView->window());
        if (!mw) {
            request.reject();
            return;
        }
        mw->handleFullScreenRequest(std::move(request), m_webView);
    });
    connect(this, &QWebEnginePage::permissionRequested, this, &WebPage::handlePermissionRequest);
    connect(this, &QWebEnginePage::certificateError, this, &WebPage::handleCertificateError);
    connect(this, &QWebEnginePage::authenticationRequired, this,
            [this](const QUrl &requestUrl, QAuthenticator *auth) {
                askCredentials(tr("%1 requires a user name and password.").arg(requestUrl.host()), auth);
            });
    connect(this, &QWebEnginePage::proxyAuthenticationRequired, this,
            [this](const QUrl &, QAuthenticator *auth, const QString &proxyHost) {
                askCredentials(tr("The proxy %1 requires a user name and password.").arg(proxyHost), auth);
            });
}

// The authenticator must be filled in before the signal returns, so this dialog has to be modal.
void WebPage::askCredentials(const QString &message, QAuthenticator *auth)
{
    // Heap-allocated and guarded: if the tab is closed during exec(), the view deletes the dialog
    // and the authenticator must not be touched.
    QPointer<QDialog> dialog = new QDialog(m_webView);
    dialog->setWindowTitle(tr("Authentication required"));
    auto *layout = new QFormLayout(dialog);
    auto *label = new QLabel(message, dialog);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    layout->addRow(label);
    if (!auth->realm().isEmpty()) {
        auto *realm = new QLabel(tr("Realm: %1").arg(auth->realm()), dialog);
        realm->setTextFormat(Qt::PlainText);
        realm->setWordWrap(true);
        layout->addRow(realm);
    }
    auto *user = new QLineEdit(auth->user(), dialog);
    auto *password = new QLineEdit(dialog);
    password->setEchoMode(QLineEdit::Password);
    layout->addRow(tr("User name:"), user);
    layout->addRow(tr("Password:"), password);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addRow(buttons);
    const int result = dialog->exec();
    if (!dialog) {
        return;
    }
    if (result == QDialog::Accepted) {
        auth->setUser(user->text());
        auth->setPassword(password->text());
    } else {
        // A null authenticator cancels the request.
        *auth = QAuthenticator();
    }
    delete dialog;
}

void WebPage::handleCertificateError(QWebEngineCertificateError error)
{
    // Only offer an override for the page itself; broken subresources are just blocked.
    // Chromium remembers an accepted certificate for the rest of the session.
    if (!error.isOverridable() || !error.isMainFrame()) {
        error.rejectCertificate();
        return;
    }
    error.defer();
    auto *box = new QMessageBox(QMessageBox::Warning, tr("Certificate error"),
                                tr("The connection to %1 is not secure.").arg(error.url().host()),
                                QMessageBox::NoButton, m_webView);
    box->setTextFormat(Qt::PlainText);
    box->setInformativeText(error.description() + "\n\n"
                            + tr("Someone could be trying to impersonate the site or intercept your data."));
    auto *back = box->addButton(tr("Go back"), QMessageBox::RejectRole);
    auto *proceed = box->addButton(tr("Proceed anyway (unsafe)"), QMessageBox::DestructiveRole);
    box->setDefaultButton(back);
    box->setEscapeButton(back);
    box->setAttribute(Qt::WA_DeleteOnClose);
    connect(box, &QMessageBox::finished, this, [box, proceed, error]() mutable {
        if (box->clickedButton() == proceed) {
            error.acceptCertificate();
        } else {
            error.rejectCertificate();
        }
    });
    box->open();
}

QString WebPage::permissionDescription(QWebEnginePermission::PermissionType type)
{
    using Type = QWebEnginePermission::PermissionType;
    switch (type) {
    case Type::MediaAudioCapture:
        return tr("use your microphone");
    case Type::MediaVideoCapture:
        return tr("use your camera");
    case Type::MediaAudioVideoCapture:
        return tr("use your camera and microphone");
    case Type::DesktopVideoCapture:
        return tr("share your screen");
    case Type::DesktopAudioVideoCapture:
        return tr("share your screen and audio");
    case Type::MouseLock:
        return tr("lock your mouse pointer");
    case Type::Geolocation:
        return tr("know your location");
    case Type::ClipboardReadWrite:
        return tr("read and write your clipboard");
    case Type::LocalFontsAccess:
        return tr("access the fonts installed on your computer");
    case Type::Notifications:
    case Type::Unsupported:
        break;
    }
    return {};
}

void WebPage::handlePermissionRequest(QWebEnginePermission permission)
{
    // No notification presenter is installed, so granting would have no visible effect;
    // decline quietly instead of prompting.
    const QString what = permissionDescription(permission.permissionType());
    if (what.isEmpty()) {
        permission.deny();
        return;
    }
    const QString host = permission.origin().host().isEmpty() ? permission.origin().toDisplayString()
                                                               : permission.origin().host();
    auto *box = new QMessageBox(QMessageBox::Question, tr("Permission request"), tr("%1 wants to %2.").arg(host, what),
                                QMessageBox::NoButton, m_webView);
    box->setTextFormat(Qt::PlainText);
    box->addButton(tr("Allow"), QMessageBox::AcceptRole);
    auto *block = box->addButton(tr("Block"), QMessageBox::RejectRole);
    box->setDefaultButton(block);
    box->setAttribute(Qt::WA_DeleteOnClose);
    // The profile stores the answer on disk for persistent permission types, so a site is asked only once.
    connect(box, &QMessageBox::finished, this, [box, block, permission] {
        if (box->clickedButton() && box->clickedButton() != block) {
            permission.grant();
        } else {
            permission.deny();
        }
    });
    box->open();
}

void WebPage::javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString &message, int lineNumber,
                                       const QString &sourceID)
{
    Q_UNUSED(level);
    Q_UNUSED(message);
    Q_UNUSED(lineNumber);
    Q_UNUSED(sourceID);
}

bool WebPage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    Q_UNUSED(isMainFrame);
    // Internal actions may only come from the internal page itself or from the address bar,
    // never from a remote page navigating to an mx-history:// or mx-settings:// URL.
    const bool internalScheme = url.scheme() == "mx-history" || url.scheme() == "mx-settings";
    if (internalScheme && type != NavigationTypeTyped && this->url().scheme() != url.scheme()) {
        return false;
    }
    if (url.scheme() == "mx-history") {
        auto *mw = qobject_cast<MainWindow *>(m_webView->window());
        if (!mw) {
            mw = qobject_cast<MainWindow *>(QApplication::activeWindow());
        }
        if (mw && mw->handleHistoryRequest(url)) {
            return false;
        }
    }
    if (url.scheme() == "mx-settings") {
        auto *mw = qobject_cast<MainWindow *>(m_webView->window());
        if (!mw) {
            mw = qobject_cast<MainWindow *>(QApplication::activeWindow());
        }
        if (mw && mw->handleSettingsRequest(url)) {
            return false;
        }
    }
    // Handle Ctrl+click / middle-click on regular links
    if (type == NavigationTypeLinkClicked && WebView::consumeIfNewTabRequest()) {
        auto *mw = qobject_cast<MainWindow *>(m_webView->window());
        if (!mw) {
            mw = qobject_cast<MainWindow *>(QApplication::activeWindow());
        }
        if (mw) {
            mw->openLinkInNewTab(url);
        }
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

WebView::WebView(QWebEngineProfile *profile, QWidget *parent)
    : QWebEngineView(profile, parent),
      index(historyLog.value("History/size", 0).toInt()),
      profile(profile)
{
    setPage(new WebPage(profile, this));
    connect(this, &WebView::loadFinished, this, &WebView::handleLoadFinished);
    connect(this, &WebView::iconChanged, this, &WebView::handleIconChanged);
    connect(this, &WebView::renderProcessTerminated, this, &WebView::handleRenderProcessTerminated);
    // window.print() from the page
    connect(this, &WebView::printRequested, this, [this] {
        if (auto *mw = qobject_cast<MainWindow *>(window())) {
            mw->printPage(this);
        }
    });
}

void WebView::handleRenderProcessTerminated(QWebEnginePage::RenderProcessTerminationStatus status)
{
    if (status == QWebEnginePage::NormalTerminationStatus) {
        return;
    }
    const QUrl crashedUrl = url();
    // Load the replacement page outside the signal handler, once the dead renderer is torn down.
    QTimer::singleShot(0, this, [this, crashedUrl] {
        const QString link = QString::fromUtf8(crashedUrl.toEncoded()).toHtmlEscaped();
        const QString html
            = QStringLiteral("<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>%1</title></head>"
                             "<body style=\"font-family: sans-serif; text-align: center; margin-top: 15%;\">"
                             "<h2>%1</h2><p>%2</p><p><a href=\"%3\">%4</a></p></body></html>")
                  .arg(tr("This tab crashed").toHtmlEscaped(),
                       tr("The page stopped unexpectedly.").toHtmlEscaped(), link, tr("Reload").toHtmlEscaped());
        generatedPageShown = true;
        readerMode = false;
        // Keep the original URL as base so the address bar still shows it.
        setHtml(html, crashedUrl);
    });
}

void WebView::showReaderPage(const QString &html)
{
    generatedPageShown = true;
    readerMode = true;
    setHtml(html, url());
}

void WebView::contextMenuEvent(QContextMenuEvent *event)
{
    auto *menu = createStandardContextMenu();
    // Spelling suggestions for a misspelled word in an editable field go first.
    const auto *request = lastContextMenuRequest();
    if (request && !request->misspelledWord().isEmpty()) {
        QAction *first = menu->actions().value(0);
        const QStringList suggestions = request->spellCheckerSuggestions();
        for (const QString &suggestion : suggestions) {
            auto *action = new QAction(suggestion, menu);
            QFont font = action->font();
            font.setBold(true);
            action->setFont(font);
            connect(action, &QAction::triggered, this, [this, suggestion] { page()->replaceMisspelledWord(suggestion); });
            menu->insertAction(first, action);
        }
        if (suggestions.isEmpty()) {
            auto *none = new QAction(tr("No spelling suggestions"), menu);
            none->setEnabled(false);
            menu->insertAction(first, none);
        }
        menu->insertSeparator(first);
    }
    // "Open link in new window" also ends up as a tab here, so it would just duplicate "new tab".
    menu->removeAction(pageAction(QWebEnginePage::OpenLinkInNewWindow));
    const QString selection = selectedText().simplified();
    if (!selection.isEmpty()) {
        const QString shown = selection.size() > 30 ? selection.left(30) + QChar(0x2026) : selection;
        auto *search = new QAction(tr("Search the web for \"%1\"").arg(shown), menu);
        connect(search, &QAction::triggered, this, [this, selection] {
            if (auto *mw = qobject_cast<MainWindow *>(window())) {
                mw->searchInNewTab(selection);
            }
        });
        const auto actions = menu->actions();
        const auto copy = std::find(actions.cbegin(), actions.cend(), pageAction(QWebEnginePage::Copy));
        QAction *before = (copy != actions.cend() && std::next(copy) != actions.cend()) ? *std::next(copy) : nullptr;
        menu->insertAction(before, search);
    }
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->popup(event->globalPos());
}

void WebView::installEventFilterOnFocusProxy()
{
    QWidget *proxy = focusProxy();
    if (proxy && proxy != m_currentProxy) {
        proxy->installEventFilter(this);
        m_currentProxy = proxy;
    }
}

bool WebView::event(QEvent *event)
{
    // focusProxy() is created lazily, install event filter when it becomes available
    if (event->type() == QEvent::ChildAdded) {
        QTimer::singleShot(0, this, &WebView::installEventFilterOnFocusProxy);
    }
    return QWebEngineView::event(event);
}

bool WebView::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == focusProxy() && event->type() == QEvent::MouseButtonPress) {
        auto *me = static_cast<QMouseEvent *>(event);
        s_ctrlHeld = (me->modifiers() & Qt::ControlModifier);
        s_middleClick = (me->button() == Qt::MiddleButton);
        s_consumed = false;  // New click, reset consumed state
    }
    return QWebEngineView::eventFilter(obj, event);
}

bool WebView::lastClickWasNewTabRequest()
{
    return s_ctrlHeld || s_middleClick;
}

bool WebView::consumeIfNewTabRequest()
{
    if (s_ctrlHeld || s_middleClick) {
        s_consumed = true;
        s_ctrlHeld = false;
        s_middleClick = false;
        return true;
    }
    return false;
}

void WebView::clearClickState()
{
    s_ctrlHeld = false;
    s_middleClick = false;
    s_consumed = false;
}

bool WebView::wasClickConsumed()
{
    return s_consumed;
}

WebView *WebView::createWindow(QWebEnginePage::WebWindowType type)
{
    auto *newView = new WebView(profile);
    if (type == QWebEnginePage::WebBrowserTab) {
        // Check if acceptNavigationRequest already handled this click
        bool background = !wasClickConsumed() && lastClickWasNewTabRequest();
        clearClickState();
        emit newWebView(newView, !background);
    } else if (type == QWebEnginePage::WebBrowserBackgroundTab) {
        emit newWebView(newView, false);
    } else {
        // WebBrowserWindow / WebDialog: open in a tab of this window so the view is owned, shown and
        // shares this window's profile (a separate MainWindow would own a different profile object).
        emit newWebView(newView, true);
    }
    return newView;
}

void WebView::handleLoadFinished(bool ok)
{
    // Generated pages (crash notice, reader view) are not visits; any other load leaves reader view.
    if (generatedPageShown) {
        generatedPageShown = false;
        return;
    }
    readerMode = false;
    if (!ok || page()->profile()->isOffTheRecord()) {
        return;
    }
    const QUrl loadedUrl = url();
    if (!loadedUrl.isValid() || loadedUrl.toString() == "about:blank" || loadedUrl.scheme() == "mx-history"
        || loadedUrl.scheme() == "mx-settings" || loadedUrl.scheme() == "mx-newtab") {
        return;
    }
    QTimer::singleShot(750, this, [this, loadedUrl] {
        if (page()->isLoading()) {
            return;
        }
        if (url() != loadedUrl) {
            return;
        }
        index = historyLog.value("History/size", 0).toInt();
        historyLog.beginWriteArray("History");
        historyLog.setArrayIndex(index);
        historyLog.setValue("title", title());
        historyLog.setValue("url", loadedUrl.toString());
        historyLog.setValue("time", QDateTime::currentSecsSinceEpoch());
        historyLog.endArray();
        historyLog.setValue("History/size", index + 1);
        lastHistoryIndex = index;
        lastHistoryUrl = loadedUrl;
        handleIconChanged();
    });
}

void WebView::handleIconChanged()
{
    if (icon().isNull() || lastHistoryIndex < 0) {
        return;
    }
    if (url() != lastHistoryUrl) {
        return;
    }
    QPixmap iconPixmap = icon().pixmap(QSize(22, 22));
    QByteArray iconByteArray;
    QBuffer buffer(&iconByteArray);
    if (buffer.open(QIODevice::WriteOnly)) {
        iconPixmap.save(&buffer, "PNG");
    }
    // Address the entry directly: writing through an array would make endArray() reset History/size to
    // this index and drop newer entries. Other windows may have rewritten the array, so check it is ours.
    const QString entry = QStringLiteral("History/%1/").arg(lastHistoryIndex + 1);
    if (historyLog.value(entry + "url").toString() != lastHistoryUrl.toString()) {
        lastHistoryIndex = -1;
        return;
    }
    historyLog.setValue(entry + "icon", iconByteArray);
}
