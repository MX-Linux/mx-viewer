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

#include <QApplication>
#include <QBuffer>
#include <QMessageBox>
#include <QMouseEvent>
#include <QTimer>
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
    if (!ok) {
        return;
    }
    const QUrl loadedUrl = url();
    if (!loadedUrl.isValid() || loadedUrl.toString() == "about:blank" || loadedUrl.scheme() == "mx-history"
        || loadedUrl.scheme() == "mx-settings") {
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
    historyLog.beginWriteArray("History");
    historyLog.setArrayIndex(lastHistoryIndex);
    historyLog.setValue("icon", iconByteArray);
    historyLog.endArray();
}
