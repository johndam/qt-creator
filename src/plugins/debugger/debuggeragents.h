/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://www.qtsoftware.com/contact.
**
**************************************************************************/

#ifndef DEBUGGER_AGENTS_H
#define DEBUGGER_AGENTS_H

#include "debuggermanager.h"

#include <coreplugin/icore.h>
#include <coreplugin/coreconstants.h>
#include <coreplugin/editormanager/ieditor.h>

#include <utils/qtcassert.h>

#include <QtCore/QObject>
#include <QtCore/QDebug>
#include <QtCore/QPointer>
#include <QtGui/QAction>

namespace Debugger {
namespace Internal {

class DebuggerManager;

// Object form this class are created in response to user actions in
// the Gui for showing raw memory from the inferior. After creation
// it handles communication between the engine and the bineditor.

class MemoryViewAgent : public QObject
{
    Q_OBJECT

public:
    // Called from Gui
    MemoryViewAgent(DebuggerManager *manager, quint64 startaddr);
    ~MemoryViewAgent();

    enum { BinBlockSize = 1024 };

public slots:
    // Called from Engine
    void addLazyData(quint64 addr, const QByteArray &data);
    // Called from Editor
    void fetchLazyData(int block, bool sync);

public:
    QPointer<IDebuggerEngine> m_engine;
    QPointer<Core::IEditor> m_editor;
};

} // namespace Internal
} // namespace Debugger

#endif // DEBUGGER_WATCHWINDOW_H
