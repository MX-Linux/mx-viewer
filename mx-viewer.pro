#/*****************************************************************************
#* mx-viewer.pro
# *****************************************************************************
# * Copyright (C) 2014 MX Authors
# *
# * Authors: Adrian
# *          MX Linux <http://mxlinux.org>
# *
# * This program is free software: you can redistribute it and/or modify
# * it under the terms of the GNU General Public License as published by
# * the Free Software Foundation, either version 3 of the License, or
# * (at your option) any later version.
# *
# * MX Viewer is distributed in the hope that it will be useful,
# * but WITHOUT ANY WARRANTY; without even the implied warranty of
# * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# * GNU General Public License for more details.
# *
# * You should have received a copy of the GNU General Public License
# * along with MX Viewer.  If not, see <http://www.gnu.org/licenses/>.
# **********************************************************************/

QT       += core gui webkitwidgets

TARGET = mx-viewer
TEMPLATE = app

SOURCES += main.cpp \
    mainwindow.cpp

HEADERS  += \
    version.h \
    mainwindow.h

TRANSLATIONS += translations/mx-viewer_fr.ts
