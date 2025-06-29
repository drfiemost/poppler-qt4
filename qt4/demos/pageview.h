/*
 * Copyright (C) 2008-2009, Pino Toscano <pino@kde.org>
 * Copyright (C) 2013, Fabio D'Urso <fabiodurso@hotmail.it>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef PAGEVIEW_H
#define PAGEVIEW_H

#include <QtGui/QScrollArea>

#include "documentobserver.h"

class QLabel;

class PageView : public QScrollArea, public DocumentObserver
{
    Q_OBJECT

public:
    PageView(QWidget *parent = nullptr);
    ~PageView();

    /*virtual*/ void documentLoaded();
    /*virtual*/ void documentClosed();
    /*virtual*/ void pageChanged(int page);

private Q_SLOTS:
    void slotZoomChanged(qreal value);
    void slotRotationChanged(int value);

private:
    QLabel *m_imageLabel;
    qreal m_zoom;
    int m_rotation;
    int m_dpiX;
    int m_dpiY;
};

#endif
