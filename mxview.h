/*****************************************************************************
 * mxview.h
 *****************************************************************************
 * Copyright (C) 2014 MX Authors
 *
 * Authors: Adrian
 *          MEPIS Community <http://forum.mepiscommunity.org>
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
 **********************************************************************/

#ifndef MXVIEW_H
#define MXVIEW_H

#include <QMainWindow>
#include <QWebView>
#include <QLineEdit>

class mxview : public QMainWindow
{
    Q_OBJECT

protected:
    void keyPressEvent(QKeyEvent* event);

public:
    mxview(QString url, QString title, QWidget *parent = 0);
    ~mxview();

    QWebView *webview;
    QLineEdit *searchBox;
    QString word;
    void displaySite(QString url, QString title);

public slots:
    void search();
    void findInPage();
};


#endif // MXVIEW_H
