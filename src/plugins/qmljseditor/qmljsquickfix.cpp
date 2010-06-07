/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
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
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#include "qmljsquickfix.h"
#include "qmljseditor.h"
#include "qmljs/parser/qmljsast_p.h"
#include <QtGui/QApplication>
#include <QtCore/QDebug>

using namespace QmlJSEditor::Internal;

class QmlJSQuickFixState: public TextEditor::QuickFixState
{
public:
    SemanticInfo semanticInfo;
};



QmlJSQuickFixOperation::QmlJSQuickFixOperation(TextEditor::BaseTextEditor *editor)
    : TextEditor::QuickFixOperation(editor)
{
}

QmlJSQuickFixOperation::~QmlJSQuickFixOperation()
{
}

QmlJS::Document::Ptr QmlJSQuickFixOperation::document() const
{
    return _semanticInfo.document;
}

const QmlJS::Snapshot &QmlJSQuickFixOperation::snapshot() const
{
    return _semanticInfo.snapshot;
}

const SemanticInfo &QmlJSQuickFixOperation::semanticInfo() const
{
    return _semanticInfo;
}

int QmlJSQuickFixOperation::match(TextEditor::QuickFixState *state)
{
    QmlJSQuickFixState *s = static_cast<QmlJSQuickFixState *>(state);
    _semanticInfo = s->semanticInfo;
    return check();
}

unsigned QmlJSQuickFixOperation::position(const QmlJS::AST::SourceLocation &loc) const
{
    return position(loc.startLine, loc.startColumn);
}

void QmlJSQuickFixOperation::move(const QmlJS::AST::SourceLocation &loc, int to)
{
    move(position(loc.startColumn, loc.startColumn), to);
}

void QmlJSQuickFixOperation::replace(const QmlJS::AST::SourceLocation &loc, const QString &replacement)
{
    replace(position(loc.startLine, loc.startColumn), replacement);
}

void QmlJSQuickFixOperation::remove(const QmlJS::AST::SourceLocation &loc)
{
    remove(position(loc.startLine, loc.startColumn));
}

void QmlJSQuickFixOperation::copy(const QmlJS::AST::SourceLocation &loc, int to)
{
    copy(position(loc.startLine, loc.startColumn), to);
}

QmlJSQuickFixCollector::QmlJSQuickFixCollector()
{
}

QmlJSQuickFixCollector::~QmlJSQuickFixCollector()
{
}

bool QmlJSQuickFixCollector::supportsEditor(TextEditor::ITextEditable *editable)
{
    if (qobject_cast<QmlJSTextEditor *>(editable->widget()) != 0)
        return true;

    return false;
}

TextEditor::QuickFixState *QmlJSQuickFixCollector::initializeCompletion(TextEditor::ITextEditable *editable)
{
    if (QmlJSTextEditor *editor = qobject_cast<QmlJSTextEditor *>(editable->widget())) {
        const SemanticInfo info = editor->semanticInfo();

        if (editor->isOutdated()) {
            // outdated
            qWarning() << "TODO: outdated semantic info, force a reparse.";
            return 0;
        }

        QmlJSQuickFixState *state = new QmlJSQuickFixState;
        state->semanticInfo = info;
        return state;
    }

    return 0;
}

QList<TextEditor::QuickFixOperation::Ptr> QmlJSQuickFixCollector::quickFixOperations(TextEditor::BaseTextEditor *editor) const
{
    QList<TextEditor::QuickFixOperation::Ptr> quickFixOperations;
    return quickFixOperations;
}
