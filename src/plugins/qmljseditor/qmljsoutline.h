#ifndef QMLJSOUTLINE_H
#define QMLJSOUTLINE_H

#include "qmljseditor.h"

#include <texteditor/ioutlinewidget.h>

#include <QtGui/QTreeView>
#include <QtGui/QSortFilterProxyModel>

namespace Core {
class IEditor;
}

namespace QmlJS {
class Editor;
};

namespace QmlJSEditor {
namespace Internal {

class QmlJSOutlineTreeView : public QTreeView
{
    Q_OBJECT
public:
    QmlJSOutlineTreeView(QWidget *parent = 0);
};

class QmlJSOutlineFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    QmlJSOutlineFilterModel(QObject *parent);
    // QSortFilterProxyModel
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex &sourceParent) const;

    bool filterBindings() const;
    void setFilterBindings(bool filterBindings);
private:
    bool m_filterBindings;
};

class QmlJSOutlineWidget : public TextEditor::IOutlineWidget
{
    Q_OBJECT
public:
    QmlJSOutlineWidget(QWidget *parent = 0);

    void setEditor(QmlJSTextEditor *editor);

    // IOutlineWidget
    virtual QList<QAction*> filterMenuActions() const;
    virtual void setCursorSynchronization(bool syncWithCursor);

private slots:
    void modelUpdated();
    void updateSelectionInTree(const QModelIndex &index);
    void updateSelectionInText(const QItemSelection &selection);
    void setShowBindings(bool showBindings);

private:
    bool syncCursor();

private:
    QmlJSOutlineTreeView *m_treeView;
    QmlJSOutlineFilterModel *m_filterModel;
    QWeakPointer<QmlJSTextEditor> m_editor;

    QAction *m_showBindingsAction;

    bool m_enableCursorSync;
    bool m_blockCursorSync;
};

class QmlJSOutlineWidgetFactory : public TextEditor::IOutlineWidgetFactory
{
    Q_OBJECT
public:
    bool supportsEditor(Core::IEditor *editor) const;
    TextEditor::IOutlineWidget *createWidget(Core::IEditor *editor);
};

} // namespace Internal
} // namespace QmlJSEditor

#endif // QMLJSOUTLINE_H
