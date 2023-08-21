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

#include <QDebug>
#include <QMouseEvent>
#include <QPointer>
#include <QTabBar>

TabWidget::TabWidget(QWidget *parent)
    : QTabWidget(parent),
      webView {new WebView(this)}
{
    this->setTabBarAutoHide(true);
    this->setTabsClosable(true);
    this->setMovable(true);
    this->addTab(webView, tr("New Tab"));
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::removeTab);
    connect(webView, &QWebEngineView::iconChanged, this, [this] { this->setTabIcon(0, webView->icon()); });
    connect(webView, &QWebEngineView::titleChanged, this, [this] { this->setTabText(0, webView->title()); });
}

void TabWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        auto index = this->tabBar()->tabAt(event->pos());
        if (index != -1) {
            this->removeTab(index);
        }
    }
    QTabWidget::mousePressEvent(event);
}

void TabWidget::removeTab(int index)
{
    auto *removedWidget = this->widget(index);
    removedWidget->deleteLater();
}

void TabWidget::addNewTab()
{
    QPointer<WebView> webView = new WebView;
    auto tab = this->addTab(webView, tr("New Tab"));
    this->setCurrentIndex(tab);
    connect(webView, &QWebEngineView::titleChanged, this,
            [this, webView] { this->setTabText(this->currentIndex(), webView->title()); });

    connect(webView, &QWebEngineView::iconChanged, this,
            [this, webView] { this->setTabIcon(this->currentIndex(), webView->icon()); });
}

void TabWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_1 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(0);
    } else if (event->key() == Qt::Key_2 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(1);
    } else if (event->key() == Qt::Key_3 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(2);
    } else if (event->key() == Qt::Key_4 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(3);
    } else if (event->key() == Qt::Key_5 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(4);
    } else if (event->key() == Qt::Key_6 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(5);
    } else if (event->key() == Qt::Key_7 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(6);
    } else if (event->key() == Qt::Key_8 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(7);
    } else if (event->key() == Qt::Key_9 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(8);
    } else if (event->key() == Qt::Key_0 && event->modifiers() == Qt::ControlModifier) {
        this->setCurrentIndex(9);
    } else if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::ControlModifier) {
        if (currentIndex() + 1 < this->count()) {
            this->setCurrentIndex(currentIndex() + 1);
        } else {
            this->setCurrentIndex(0);
        }
    }
}

WebView *TabWidget::currentWebView()
{
    return qobject_cast<WebView *>(this->currentWidget());
}
