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
#include "tabwidget.h"

#include <QApplication>
#include <QDataStream>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSize>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QWebEngineHistory>

TabWidget::TabWidget(QWebEngineProfile *profile, QWidget *parent)
    : QTabWidget(parent),
      newTabButton(new QPushButton("+", this)),
      profile(profile)
{
    setTabBarAutoHide(true);
    setTabsClosable(true);
    setMovable(true);
    createTab();
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::removeTab);
    connect(this, &QTabWidget::currentChanged, this, &TabWidget::handleCurrentChanged);

    newTabButton->setMaximumSize(30, 30);
    newTabButton->setParent(this);
    newTabButton->setToolTip(tr("New tab"));
    newTabButton->hide();
    connect(newTabButton, &QPushButton::clicked, this, &TabWidget::newTabButtonClicked);
    tabBar()->installEventFilter(this);
    tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tabBar(), &QTabBar::customContextMenuRequested, this, &TabWidget::showTabMenu);
    updateNewTabButton();
}

WebView *TabWidget::webViewAt(int index) const
{
    return qobject_cast<WebView *>(widget(index));
}

void TabWidget::showTabMenu(const QPoint &pos)
{
    const int index = tabBar()->tabAt(pos);
    QMenu menu(this);
    menu.addAction(tr("New tab"), this, &TabWidget::newTabButtonClicked);
    QPointer<WebView> view = webViewAt(index);
    if (view) {
        menu.addAction(tr("Reload tab"), view.data(), &QWebEngineView::reload);
        menu.addAction(tr("Duplicate tab"), this, [this, view] {
            if (view) {
                duplicateTab(indexOf(view));
            }
        });
        menu.addAction(view->page()->isAudioMuted() ? tr("Unmute tab") : tr("Mute tab"), this, [view] {
            if (view) {
                view->page()->setAudioMuted(!view->page()->isAudioMuted());
            }
        });
        menu.addSeparator();
        menu.addAction(tr("Close tab"), this, [this, view] {
            if (view) {
                removeTab(indexOf(view));
            }
        });
        QList<QWidget *> others;
        QList<QWidget *> right;
        for (int i = 0; i < count(); ++i) {
            if (i != index) {
                others.append(widget(i));
            }
            if (i > index) {
                right.append(widget(i));
            }
        }
        menu.addAction(tr("Close other tabs"), this, [this, others] { closeTabs(others); })->setEnabled(!others.isEmpty());
        menu.addAction(tr("Close tabs to the right"), this, [this, right] { closeTabs(right); })
            ->setEnabled(!right.isEmpty());
    }
    menu.exec(tabBar()->mapToGlobal(pos));
}

void TabWidget::closeTabs(const QList<QWidget *> &tabs)
{
    // Tabs may have been closed while the menu was open, so look each one up again.
    QList<QPointer<QWidget>> guarded(tabs.cbegin(), tabs.cend());
    for (const auto &tab : guarded) {
        const int i = tab ? indexOf(tab) : -1;
        if (i >= 0 && count() > 1) {
            removeTab(i);
        }
    }
}

void TabWidget::duplicateTab(int index)
{
    auto *source = webViewAt(index);
    if (!source) {
        return;
    }
    auto *copy = new WebView(profile);
    addNewTab(copy, true);
    // Copying the history restores back/forward and loads the current entry.
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << *source->history();
    QDataStream in(&data, QIODevice::ReadOnly);
    in >> *copy->history();
    tabBar()->moveTab(indexOf(copy), index + 1);
}

// Speaker button on the tab (opposite the close button) while the page plays sound or is muted;
// clicking it toggles mute.
void TabWidget::updateAudioButton(WebView *webView)
{
    const int i = indexOf(webView);
    if (i < 0) {
        return;
    }
    const auto side = static_cast<QTabBar::ButtonPosition>(
                          style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabBar()))
            == QTabBar::LeftSide
        ? QTabBar::RightSide
        : QTabBar::LeftSide;
    auto *button = qobject_cast<QToolButton *>(tabBar()->tabButton(i, side));
    const bool muted = webView->page()->isAudioMuted();
    if (!muted && !webView->page()->recentlyAudible()) {
        if (button) {
            tabBar()->setTabButton(i, side, nullptr);
            button->deleteLater();
        }
        return;
    }
    if (!button) {
        button = new QToolButton(tabBar());
        button->setAutoRaise(true);
        button->setIconSize(QSize(16, 16));
        QPointer<WebView> view = webView;
        connect(button, &QToolButton::clicked, this, [view] {
            if (view) {
                view->page()->setAudioMuted(!view->page()->isAudioMuted());
            }
        });
        tabBar()->setTabButton(i, side, button);
    }
    button->setIcon(muted ? QIcon::fromTheme("audio-volume-muted") : QIcon::fromTheme("audio-volume-high"));
    if (button->icon().isNull()) {
        button->setText(muted ? QStringLiteral("\U0001F507") : QStringLiteral("\U0001F50A"));
    }
    button->setToolTip(muted ? tr("Unmute tab") : tr("Mute tab"));
}

void TabWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        auto index = tabBar()->tabAt(event->pos());
        if (index != -1) {
            removeTab(index);
        }
    }
    QTabWidget::mousePressEvent(event);
}

bool TabWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == tabBar() && (event->type() == QEvent::Resize || event->type() == QEvent::LayoutRequest || event->type() == QEvent::Show)) {
        QTimer::singleShot(0, this, &TabWidget::positionNewTabButton);
    }
    return QTabWidget::eventFilter(obj, event);
}

void TabWidget::handleCurrentChanged(int index)
{
    auto *webView = currentWebView();
    if (webView) {
        setTabText(index, webView->title());
    }
}

void TabWidget::removeTab(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    auto *w = widget(index);
    if (w) {
        auto *webView = qobject_cast<WebView *>(w);
        if (!webView || webView->url().scheme() != "mx-settings") {
            finalizeRemoveTab(index);
            return;
        }
        setCurrentIndex(index);
        QPointer<WebView> settingsView = webView;
        webView->page()->runJavaScript("window.mxSettingsDirty === true", [this, index, settingsView](const QVariant &result) {
            if (!settingsView || index < 0 || index >= count()) {
                return;
            }
            if (!result.toBool()) {
                finalizeRemoveTab(index);
                return;
            }
            QMessageBox box(this);
            box.setIcon(QMessageBox::Question);
            box.setWindowTitle(tr("Unsaved settings"));
            box.setText(tr("Save changes before closing?"));
            box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            box.setDefaultButton(QMessageBox::Save);
            const auto choice = box.exec();
            if (choice == QMessageBox::Save) {
                settingsView->page()->runJavaScript("document.getElementById('save').click();",
                                                    [this, index, settingsView](const QVariant &) {
                                                        if (!settingsView || index < 0 || index >= count()) {
                                                            return;
                                                        }
                                                        finalizeRemoveTab(index);
                                                    });
                return;
            }
            if (choice == QMessageBox::Discard) {
                finalizeRemoveTab(index);
            }
        });
        return;
    }
    updateNewTabButton();
}

void TabWidget::finalizeRemoveTab(int index)
{
    if (index < 0 || index >= count()) {
        return;
    }
    auto *w = widget(index);
    if (!w) {
        updateNewTabButton();
        return;
    }
    if (auto *webView = qobject_cast<WebView *>(w)) {
        emit tabClosed(webView->url());
    }
    QTabWidget::removeTab(index);
    w->deleteLater();
    updateNewTabButton();
}

WebView *TabWidget::createTab(bool makeCurrent)
{
    QPointer<WebView> webView = new WebView(profile);
    addNewTab(webView, makeCurrent);
    return webView.data();
}

void TabWidget::addNewTab(WebView *webView, bool makeCurrent)
{
    auto tab = addTab(webView, tr("New Tab"));
    if (makeCurrent) {
        setCurrentIndex(tab);
    }
    connect(webView, &WebView::titleChanged, this, [this, webView] {
        if (webView) {
            setTabText(indexOf(webView), webView->title());
            setTabToolTip(indexOf(webView), webView->title());
        }
    });
    connect(webView->page(), &QWebEnginePage::recentlyAudibleChanged, this, [this, webView] { updateAudioButton(webView); });
    connect(webView->page(), &QWebEnginePage::audioMutedChanged, this, [this, webView] { updateAudioButton(webView); });
    connect(webView, &WebView::iconChanged, this, [this, webView] {
        if (webView) {
            setTabIcon(indexOf(webView), webView->icon());
        }
    });
    connect(webView, &WebView::newWebView, this, [this](WebView *view, bool makeCurrent) {
        addNewTab(view, makeCurrent);
    });
    // Popups opened as tabs (e.g. OAuth flows) close themselves with window.close()
    connect(webView->page(), &QWebEnginePage::windowCloseRequested, this, [this, webView] {
        const int i = indexOf(webView);
        if (i >= 0 && count() > 1) {
            removeTab(i);
        }
    });
    updateNewTabButton();
    QTimer::singleShot(0, this, &TabWidget::positionNewTabButton);
}

void TabWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier) {
        if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_9) {
            setCurrentIndex(event->key() - Qt::Key_1);
            return;
        }
        if (event->key() == Qt::Key_0) {
            setCurrentIndex(9);
            return;
        }
        if (event->key() == Qt::Key_Tab) {
            setCurrentIndex((currentIndex() + 1) % count());
            return;
        }
        if (event->key() == Qt::Key_W) {
            if (count() == 1) {
                window()->close();
            } else {
                removeTab(currentIndex());
            }
            return;
        }
    }
    QTabWidget::keyPressEvent(event);
}

WebView *TabWidget::currentWebView()
{
    return qobject_cast<WebView *>(currentWidget());
}

void TabWidget::updateNewTabButton()
{
    if (count() < 2) {
        newTabButton->hide();
        return;
    }
    newTabButton->show();
    positionNewTabButton();
    QTimer::singleShot(0, this, &TabWidget::positionNewTabButton);
}

void TabWidget::positionNewTabButton()
{
    if (count() < 2) {
        newTabButton->hide();
        return;
    }

    int lastIndex = count() - 1;
    QRect tabRect = tabBar()->tabRect(lastIndex);
    int reservedRight = 0;
    const QRect barRect = tabBar()->rect();
    const auto toolButtons = tabBar()->findChildren<QToolButton *>();
    for (const auto *button : toolButtons) {
        if (!button || !button->isVisible()) {
            continue;
        }
        const QRect buttonRect = button->geometry();
        if (buttonRect.right() >= barRect.right() - 2) {
            reservedRight += buttonRect.width() + 2;
        }
    }

    constexpr int scrollButtonPadding = 14;
    int minX = tabRect.right() + 2;
    int buttonX = minX;
    if (reservedRight > 0) {
        int maxX = barRect.right() - reservedRight - scrollButtonPadding - newTabButton->width();
        if (maxX < minX) {
            newTabButton->hide();
            return;
        }
        buttonX = qMin(buttonX, maxX);
    }
    int buttonY = tabRect.top() + (tabRect.height() - newTabButton->height()) / 2;

    newTabButton->move(buttonX, buttonY);
    newTabButton->show();
}
