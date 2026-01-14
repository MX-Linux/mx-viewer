/**********************************************************************
 *
 **********************************************************************
 * Copyright (C) 2023 MX Authors
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
#include <QDebug>
#include <QMouseEvent>
#include <QPointer>
#include <QTabBar>

TabWidget::TabWidget(QWidget *parent)
    : QTabWidget(parent)
{
    setTabBarAutoHide(true);
    setTabsClosable(true);
    setMovable(true);
    createTab();
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::removeTab);
    connect(this, &QTabWidget::currentChanged, this, &TabWidget::handleCurrentChanged);
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
        QTabWidget::removeTab(index);
        w->deleteLater();
    }
}

void TabWidget::createTab()
{
    QPointer<WebView> webView = new WebView;
    addNewTab(webView);
}

void TabWidget::addNewTab(WebView *webView)
{
    auto tab = addTab(webView, tr("New Tab"));
    setCurrentIndex(tab);
    connect(webView, &WebView::titleChanged, this, [this, webView] {
        if (webView) {
            setTabText(indexOf(webView), webView->title());
        }
    });
    connect(webView, &WebView::iconChanged, this, [this, webView] {
        if (webView) {
            setTabIcon(indexOf(webView), webView->icon());
        }
    });
    connect(webView, &WebView::newWebView, this, &TabWidget::addNewTab);
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
            count() == 1 ? QApplication::quit() : removeTab(currentIndex());
            return;
        }
    }
    QTabWidget::keyPressEvent(event);
}

WebView *TabWidget::currentWebView()
{
    return qobject_cast<WebView *>(currentWidget());
}
