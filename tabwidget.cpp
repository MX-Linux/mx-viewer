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
#include <QTabBar>

TabWidget::TabWidget(QWidget *parent)
    : QTabWidget(parent),
      webview {new WebView(this)}
{
    this->setTabBarAutoHide(true);
    this->setTabsClosable(true);
    this->setMovable(true);
    this->addTab(webview, tr("New Tab"));
    connect(this, &QTabWidget::tabCloseRequested, this, &TabWidget::removeTab);
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

WebView *TabWidget::currentWebView()
{
    return qobject_cast<WebView *>(this->currentWidget());
}
