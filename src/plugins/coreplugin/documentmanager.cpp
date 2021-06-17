/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "documentmanager.h"

#include "icore.h"
#include "idocument.h"
#include "idocumentfactory.h"
#include "coreconstants.h"

#include <coreplugin/actionmanager/actioncontainer.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/diffservice.h>
#include <coreplugin/dialogs/filepropertiesdialog.h>
#include <coreplugin/dialogs/readonlyfilesdialog.h>
#include <coreplugin/dialogs/saveitemsdialog.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/editormanager_p.h>
#include <coreplugin/editormanager/editorview.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/editormanager/ieditorfactory.h>
#include <coreplugin/editormanager/iexternaleditor.h>


#include <extensionsystem/pluginmanager.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/globalfilechangeblocker.h>
#include <utils/hostosinfo.h>
#include <utils/mimetypes/mimedatabase.h>
#include <utils/optional.h>
#include <utils/pathchooser.h>
#include <utils/qtcassert.h>
#include <utils/reloadpromptutils.h>

#include <QStringList>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QSettings>
#include <QTimer>
#include <QAction>
#include <QFileDialog>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>

static const bool kUseProjectsDirectoryDefault = true;
static Q_LOGGING_CATEGORY(log, "qtc.core.documentmanager", QtWarningMsg)

/*!
  \class Core::DocumentManager
  \inheaderfile coreplugin/documentmanager.h
  \ingroup mainclasses
  \inmodule QtCreator

  \brief The DocumentManager class manages a set of documents.

  The DocumentManager service monitors a set of IDocument objects.

  This section uses the following terminology:

  \list
    \li A \e file means a collection of data stored on a disk under a name
        (that is, the usual meaning of the term \e file in computing).
    \li A \e document holds content open in Qt Creator. If it corresponds to a
        file, it might differ from it, because it was modified. But a document
        might not correspond to a file at all. For example, diff viewer
        documents or Git blame or log records are created and displayed by
        Qt Creator upon request.
    \li An \a editor provides a view into a document that is actually visible
        to the user and potentially allows editing the document. Multiple
        editors can open views into the same document.
  \endlist

  Plugins should register documents they work with at the document management
  service. The files the IDocument objects point to will be monitored at
  file system level. If a file changes on disk, the status of the IDocument
  object will be adjusted accordingly. On application exit the user will be
  asked to save all modified documents.

  Different IDocument objects in the set can point to the same file in the
  file system. The monitoring for an IDocument can be blocked by
  using the \l Core::FileChangeBlocker class.

  The functions \c expectFileChange() and \c unexpectFileChange() mark a file change
  as expected. On expected file changes all IDocument objects are notified to reload
  themselves.

  The DocumentManager service also provides convenience functions
  for saving documents, such as \l saveModifiedDocuments() and
  \l saveModifiedDocumentsSilently(). They present users with a
  dialog that lists all modified documents and asks them which
  documents should be saved.

  The service also manages the list of recent files to be shown to the user.

  \sa addToRecentFiles(), recentFiles()
 */

static const char settingsGroupC[] = "RecentFiles";
static const char filesKeyC[] = "Files";
static const char editorsKeyC[] = "EditorIds";

static const char directoryGroupC[] = "Directories";
static const char projectDirectoryKeyC[] = "Projects";
static const char useProjectDirectoryKeyC[] = "UseProjectsDirectory";

using namespace Utils;

namespace Core {

static void readSettings();

static bool saveModifiedFilesHelper(const QList<IDocument *> &documents,
                                    const QString &message,
                                    bool *cancelled, bool silently,
                                    const QString &alwaysSaveMessage,
                                    bool *alwaysSave, QList<IDocument *> *failedToSave);

namespace Internal {

struct FileStateItem
{
    QDateTime modified;
    QFile::Permissions permissions;
};

struct FileState
{
    FilePath watchedFilePath;
    QMap<IDocument *, FileStateItem> lastUpdatedState;
    FileStateItem expected;
};


class DocumentManagerPrivate : public QObject
{
    Q_OBJECT
public:
    DocumentManagerPrivate();
    QFileSystemWatcher *fileWatcher();
    QFileSystemWatcher *linkWatcher();

    void checkOnNextFocusChange();
    void onApplicationFocusChange();

    void registerSaveAllAction();

    QMap<FilePath, FileState> m_states; // filePathKey -> FileState
    QSet<FilePath> m_changedFiles; // watched file paths collected from file watcher notifications
    QList<IDocument *> m_documentsWithoutWatch;
    QMap<IDocument *, FilePaths> m_documentsWithWatch; // document -> list of filePathKeys
    QSet<FilePath> m_expectedFileNames; // set of file paths without normalization

    QList<DocumentManager::RecentFile> m_recentFiles;

    bool m_postponeAutoReload = false;
    bool m_blockActivated = false;
    bool m_checkOnFocusChange = false;
    bool m_useProjectsDirectory = kUseProjectsDirectoryDefault;

    QFileSystemWatcher *m_fileWatcher = nullptr; // Delayed creation.
    QFileSystemWatcher *m_linkWatcher = nullptr; // Delayed creation (only UNIX/if a link is seen).
    QString m_lastVisitedDirectory = QDir::currentPath();
    QString m_defaultLocationForNewFiles;
    FilePath m_projectsDirectory;
    // When we are calling into an IDocument
    // we don't want to receive a changed()
    // signal
    // That makes the code easier
    IDocument *m_blockedIDocument = nullptr;

    QAction *m_saveAllAction;
};

static DocumentManager *m_instance;
static DocumentManagerPrivate *d;

QFileSystemWatcher *DocumentManagerPrivate::fileWatcher()
{
    if (!m_fileWatcher) {
        m_fileWatcher= new QFileSystemWatcher(m_instance);
        QObject::connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
                         m_instance, &DocumentManager::changedFile);
    }
    return m_fileWatcher;
}

QFileSystemWatcher *DocumentManagerPrivate::linkWatcher()
{
    if (HostOsInfo::isAnyUnixHost()) {
        if (!m_linkWatcher) {
            m_linkWatcher = new QFileSystemWatcher(m_instance);
            m_linkWatcher->setObjectName(QLatin1String("_qt_autotest_force_engine_poller"));
            QObject::connect(m_linkWatcher, &QFileSystemWatcher::fileChanged,
                             m_instance, &DocumentManager::changedFile);
        }
        return m_linkWatcher;
    }

    return fileWatcher();
}

void DocumentManagerPrivate::checkOnNextFocusChange()
{
    m_checkOnFocusChange = true;
}

void DocumentManagerPrivate::onApplicationFocusChange()
{
    if (!m_checkOnFocusChange)
        return;
    m_checkOnFocusChange = false;
    m_instance->checkForReload();
}

void DocumentManagerPrivate::registerSaveAllAction()
{
    ActionContainer *mfile = ActionManager::actionContainer(Constants::M_FILE);
    Command *cmd = ActionManager::registerAction(m_saveAllAction, Constants::SAVEALL);
    cmd->setDefaultKeySequence(QKeySequence(useMacShortcuts ? QString() : tr("Ctrl+Shift+S")));
    mfile->addAction(cmd, Constants::G_FILE_SAVE);
    m_saveAllAction->setEnabled(false);
    connect(m_saveAllAction, &QAction::triggered, []() {
        DocumentManager::saveAllModifiedDocumentsSilently();
    });
}

DocumentManagerPrivate::DocumentManagerPrivate() :
    m_saveAllAction(new QAction(tr("Save A&ll"), this))
{
    // we do not want to do too much directly in the focus change event, so queue the connection
    connect(qApp,
            &QApplication::focusChanged,
            this,
            &DocumentManagerPrivate::onApplicationFocusChange,
            Qt::QueuedConnection);
}

} // namespace Internal
} // namespace Core

namespace Core {

using namespace Internal;

DocumentManager::DocumentManager(QObject *parent)
  : QObject(parent)
{
    d = new DocumentManagerPrivate;
    m_instance = this;

    connect(Utils::GlobalFileChangeBlocker::instance(), &Utils::GlobalFileChangeBlocker::stateChanged,
            this, [](bool blocked) {
        d->m_postponeAutoReload = blocked;
        if (!blocked)
            QTimer::singleShot(500, m_instance, &DocumentManager::checkForReload);
    });

    readSettings();

    if (d->m_useProjectsDirectory)
        setFileDialogLastVisitedDirectory(d->m_projectsDirectory.toString());
}

DocumentManager::~DocumentManager()
{
    delete d;
}

DocumentManager *DocumentManager::instance()
{
    return m_instance;
}

/* Only called from addFileInfo(IDocument *). Adds the document & state to various caches/lists,
   but does not actually add a watcher. */
static void addFileInfo(IDocument *document, const FilePath &filePath, const FilePath &filePathKey)
{
    FileStateItem state;
    if (!filePath.isEmpty()) {
        qCDebug(log) << "adding document for" << filePath << "(" << filePathKey << ")";
        state.modified = filePath.lastModified();
        state.permissions = filePath.permissions();
        // Add state if we don't have already
        if (!d->m_states.contains(filePathKey)) {
            FileState state;
            state.watchedFilePath = filePath;
            d->m_states.insert(filePathKey, state);
        }
        d->m_states[filePathKey].lastUpdatedState.insert(document, state);
    }
    d->m_documentsWithWatch[document].append(filePathKey); // inserts a new QStringList if not already there
}

/* Adds the IDocuments' file and possibly it's final link target to both m_states
   (if it's file name is not empty), and the m_filesWithWatch list,
   and adds a file watcher for each if not already done.
   (The added file names are guaranteed to be absolute and cleaned.) */
static void addFileInfos(const QList<IDocument *> &documents)
{
    FilePaths pathsToWatch;
    FilePaths linkPathsToWatch;
    for (IDocument *document : documents) {
        const FilePath filePath = DocumentManager::filePathKey(document->filePath(),
                                                               DocumentManager::KeepLinks);
        const FilePath resolvedFilePath = filePath.canonicalPath();
        const bool isLink = filePath != resolvedFilePath;
        addFileInfo(document, filePath, filePath);
        if (isLink) {
            addFileInfo(document, resolvedFilePath, resolvedFilePath);
            if (!filePath.needsDevice()) {
                linkPathsToWatch.append(d->m_states.value(filePath).watchedFilePath);
                pathsToWatch.append(d->m_states.value(resolvedFilePath).watchedFilePath);
            }
        } else if (!filePath.needsDevice()) {
            pathsToWatch.append(d->m_states.value(filePath).watchedFilePath);
        }
    }
    // Add or update watcher on file path
    // This is also used to update the watcher in case of saved (==replaced) files or
    // update link targets, even if there are multiple documents registered for it
    if (!pathsToWatch.isEmpty()) {
        qCDebug(log) << "adding full watch for" << pathsToWatch;
        d->fileWatcher()->addPaths(Utils::transform(pathsToWatch, &FilePath::toString));
    }
    if (!linkPathsToWatch.isEmpty()) {
        qCDebug(log) << "adding link watch for" << linkPathsToWatch;
        d->linkWatcher()->addPaths(Utils::transform(linkPathsToWatch, &FilePath::toString));
    }
}

/*!
    Adds a list of \a documents to the collection. If \a addWatcher is \c true
    (the default), the documents' files are added to a file system watcher that
    notifies the document manager about file changes.
*/
void DocumentManager::addDocuments(const QList<IDocument *> &documents, bool addWatcher)
{
    if (!addWatcher) {
        // We keep those in a separate list

        foreach (IDocument *document, documents) {
            if (document && !d->m_documentsWithoutWatch.contains(document)) {
                connect(document, &QObject::destroyed,
                        m_instance, &DocumentManager::documentDestroyed);
                connect(document, &IDocument::filePathChanged,
                        m_instance, &DocumentManager::filePathChanged);
                connect(document, &IDocument::changed, m_instance, &DocumentManager::updateSaveAll);
                d->m_documentsWithoutWatch.append(document);
            }
        }
        return;
    }

    const QList<IDocument *> documentsToWatch = Utils::filtered(documents, [](IDocument *document) {
        return document && !d->m_documentsWithWatch.contains(document);
    });
    for (IDocument *document : documentsToWatch) {
        connect(document, &IDocument::changed, m_instance, &DocumentManager::checkForNewFileName);
        connect(document, &QObject::destroyed, m_instance, &DocumentManager::documentDestroyed);
        connect(document, &IDocument::filePathChanged,
                m_instance, &DocumentManager::filePathChanged);
        connect(document, &IDocument::changed, m_instance, &DocumentManager::updateSaveAll);
    }
    addFileInfos(documentsToWatch);
}


/* Removes all occurrences of the IDocument from m_filesWithWatch and m_states.
   If that results in a file no longer being referenced by any IDocument, this
   also removes the file watcher.
*/
static void removeFileInfo(IDocument *document)
{
    if (!d->m_documentsWithWatch.contains(document))
        return;
    foreach (const FilePath &filePath, d->m_documentsWithWatch.value(document)) {
        if (!d->m_states.contains(filePath))
            continue;
        qCDebug(log) << "removing document (" << filePath << ")";
        d->m_states[filePath].lastUpdatedState.remove(document);
        if (d->m_states.value(filePath).lastUpdatedState.isEmpty()) {
            const FilePath &watchedFilePath = d->m_states.value(filePath).watchedFilePath;
            if (!watchedFilePath.needsDevice()) {
                const QString &localFilePath = watchedFilePath.path();
                if (d->m_fileWatcher
                    && d->m_fileWatcher->files().contains(localFilePath)) {
                    qCDebug(log) << "removing watch for" << localFilePath;
                    d->m_fileWatcher->removePath(localFilePath);
                }
                if (d->m_linkWatcher
                    && d->m_linkWatcher->files().contains(localFilePath)) {
                    qCDebug(log) << "removing watch for" << localFilePath;
                    d->m_linkWatcher->removePath(localFilePath);
                }
            }
            d->m_states.remove(filePath);
        }
    }
    d->m_documentsWithWatch.remove(document);
}

/// Dumps the state of the file manager's map
/// For debugging purposes
/*
static void dump()
{
    qDebug() << "======== dumping state map";
    QMap<QString, FileState>::const_iterator it, end;
    it = d->m_states.constBegin();
    end = d->m_states.constEnd();
    for (; it != end; ++it) {
        qDebug() << it.key();
        qDebug() << "   expected:" << it.value().expected.modified;

        QMap<IDocument *, FileStateItem>::const_iterator jt, jend;
        jt = it.value().lastUpdatedState.constBegin();
        jend = it.value().lastUpdatedState.constEnd();
        for (; jt != jend; ++jt) {
            qDebug() << "  " << jt.key()->fileName() << jt.value().modified;
        }
    }
    qDebug() << "------- dumping files with watch list";
    foreach (IDocument *key, d->m_filesWithWatch.keys()) {
        qDebug() << key->fileName() << d->m_filesWithWatch.value(key);
    }
    qDebug() << "------- dumping watch list";
    if (d->m_fileWatcher)
        qDebug() << d->m_fileWatcher->files();
    qDebug() << "------- dumping link watch list";
    if (d->m_linkWatcher)
        qDebug() << d->m_linkWatcher->files();
}
*/

/*!
    Tells the document manager that a file has been renamed from \a from to
    \a to on disk from within \QC.

    Needs to be called right after the actual renaming on disk (that is, before
    the file system watcher can report the event during the next event loop run).

    \a from needs to be an absolute file path.
    This will notify all IDocument objects pointing to that file of the rename
    by calling \l IDocument::setFilePath(), and update the cached time and
    permission information to avoid annoying the user with \e {the file has
    been removed} popups.
*/
void DocumentManager::renamedFile(const Utils::FilePath &from, const Utils::FilePath &to)
{
    const Utils::FilePath &fromKey = filePathKey(from, KeepLinks);

    // gather the list of IDocuments
    QList<IDocument *> documentsToRename;
    for (auto it = d->m_documentsWithWatch.cbegin(), end = d->m_documentsWithWatch.cend();
            it != end; ++it) {
        if (it.value().contains(fromKey))
            documentsToRename.append(it.key());
    }

    // rename the IDocuments
    foreach (IDocument *document, documentsToRename) {
        d->m_blockedIDocument = document;
        removeFileInfo(document);
        document->setFilePath(to);
        addFileInfos({document});
        d->m_blockedIDocument = nullptr;
    }
    emit m_instance->allDocumentsRenamed(from, to);
}

void DocumentManager::filePathChanged(const FilePath &oldName, const FilePath &newName)
{
    auto doc = qobject_cast<IDocument *>(sender());
    QTC_ASSERT(doc, return);
    if (doc == d->m_blockedIDocument)
        return;
    emit m_instance->documentRenamed(doc, oldName, newName);
}

void DocumentManager::updateSaveAll()
{
    d->m_saveAllAction->setEnabled(!modifiedDocuments().empty());
}

/*!
    Adds \a document to the collection. If \a addWatcher is \c true
    (the default), the document's file is added to a file system watcher
    that notifies the document manager about file changes.
*/
void DocumentManager::addDocument(IDocument *document, bool addWatcher)
{
    addDocuments({document}, addWatcher);
}

void DocumentManager::documentDestroyed(QObject *obj)
{
    auto document = static_cast<IDocument*>(obj);
    // Check the special unwatched first:
    if (!d->m_documentsWithoutWatch.removeOne(document))
        removeFileInfo(document);
}

/*!
    Removes \a document from the collection.

    Returns \c true if the document had the \c addWatcher argument to
    addDocument() set.
*/
bool DocumentManager::removeDocument(IDocument *document)
{
    QTC_ASSERT(document, return false);

    bool addWatcher = false;
    // Special casing unwatched files
    if (!d->m_documentsWithoutWatch.removeOne(document)) {
        addWatcher = true;
        removeFileInfo(document);
        disconnect(document, &IDocument::changed, m_instance, &DocumentManager::checkForNewFileName);
    }
    disconnect(document, &QObject::destroyed, m_instance, &DocumentManager::documentDestroyed);
    disconnect(document, &IDocument::changed, m_instance, &DocumentManager::updateSaveAll);
    return addWatcher;
}

/* Slot reacting on IDocument::changed. We need to check if the signal was sent
   because the document was saved under different name. */
void DocumentManager::checkForNewFileName()
{
    auto document = qobject_cast<IDocument *>(sender());
    // We modified the IDocument
    // Trust the other code to also update the m_states map
    if (document == d->m_blockedIDocument)
        return;
    QTC_ASSERT(document, return);
    QTC_ASSERT(d->m_documentsWithWatch.contains(document), return);

    // Maybe the name has changed or file has been deleted and created again ...
    // This also updates the state to the on disk state
    removeFileInfo(document);
    addFileInfos({document});
}

/*!
    Returns a guaranteed cleaned absolute file path for \a filePath in portable form.
    Resolves symlinks if \a resolveMode is ResolveLinks.
*/
QString DocumentManager::cleanAbsoluteFilePath(const QString &filePath, ResolveMode resolveMode)
{
    QFileInfo fi(QDir::fromNativeSeparators(filePath));
    if (fi.exists() && resolveMode == ResolveLinks) {
        // if the filePath is no link, we want this method to return the same for both ResolveModes
        // so wrap with absoluteFilePath because that forces drive letters upper case
        return QFileInfo(fi.canonicalFilePath()).absoluteFilePath();
    }
    return QDir::cleanPath(fi.absoluteFilePath());
}

/*!
    Returns a representation of \a filePath that can be used as a key for maps.
    It is a cleaned absolute file path in portable form, that is all lowercase
    if the file system is case insensitive in the host OS settings.
    Resolves symlinks if \a resolveMode is ResolveLinks.
*/
QString DocumentManager::filePathKey(const QString &filePath, ResolveMode resolveMode)
{
    QString s = cleanAbsoluteFilePath(filePath, resolveMode);
    if (HostOsInfo::fileNameCaseSensitivity() == Qt::CaseInsensitive)
        s = s.toLower();
    return s;
}

FilePath DocumentManager::filePathKey(const Utils::FilePath &filePath, ResolveMode resolveMode)
{
    if (resolveMode == ResolveLinks)
        return filePath.canonicalPath().absoluteFilePath();
    return filePath.absoluteFilePath();
}

/*!
    Returns the list of IDocuments that have been modified.
*/
QList<IDocument *> DocumentManager::modifiedDocuments()
{
    QList<IDocument *> modified;

    const auto docEnd = d->m_documentsWithWatch.keyEnd();
    for (auto docIt = d->m_documentsWithWatch.keyBegin(); docIt != docEnd; ++docIt) {
        IDocument *document = *docIt;
        if (document->isModified())
            modified << document;
    }

    foreach (IDocument *document, d->m_documentsWithoutWatch) {
        if (document->isModified())
            modified << document;
    }

    return modified;
}

/*!
    Treats any subsequent change to \a fileName as an expected file change.

    \sa unexpectFileChange()
*/
void DocumentManager::expectFileChange(const Utils::FilePath &filePath)
{
    if (filePath.isEmpty())
        return;
    d->m_expectedFileNames.insert(filePath);
}

/* only called from unblock and unexpect file change functions */
static void updateExpectedState(const FilePath &filePathKey)
{
    if (filePathKey.isEmpty())
        return;
    if (d->m_states.contains(filePathKey)) {
        const FilePath watched = d->m_states.value(filePathKey).watchedFilePath;
        d->m_states[filePathKey].expected.modified = watched.lastModified();
        d->m_states[filePathKey].expected.permissions = watched.permissions();
    }
}

/*!
    Considers all changes to \a fileName unexpected again.

    \sa expectFileChange()
*/
void DocumentManager::unexpectFileChange(const FilePath &filePath)
{
    // We are updating the expected time of the file
    // And in changedFile we'll check if the modification time
    // is the same as the saved one here
    // If so then it's a expected change

    if (filePath.isEmpty())
        return;
    d->m_expectedFileNames.remove(filePath);
    const FilePath cleanAbsFilePath = filePathKey(filePath, KeepLinks);
    updateExpectedState(filePathKey(filePath, KeepLinks));
    const FilePath resolvedCleanAbsFilePath = cleanAbsFilePath.canonicalPath();
    if (cleanAbsFilePath != resolvedCleanAbsFilePath)
        updateExpectedState(filePathKey(filePath, ResolveLinks));
}

static bool saveModifiedFilesHelper(const QList<IDocument *> &documents,
                                    const QString &message, bool *cancelled, bool silently,
                                    const QString &alwaysSaveMessage, bool *alwaysSave,
                                    QList<IDocument *> *failedToSave)
{
    if (cancelled)
        (*cancelled) = false;

    QList<IDocument *> notSaved;
    QHash<IDocument *, QString> modifiedDocumentsMap;
    QList<IDocument *> modifiedDocuments;

    foreach (IDocument *document, documents) {
        if (document && document->isModified() && !document->isTemporary()) {
            QString name = document->filePath().toString();
            if (name.isEmpty())
                name = document->fallbackSaveAsFileName();

            // There can be several IDocuments pointing to the same file
            // Prefer one that is not readonly
            // (even though it *should* not happen that the IDocuments are inconsistent with readonly)
            if (!modifiedDocumentsMap.key(name, nullptr) || !document->isFileReadOnly())
                modifiedDocumentsMap.insert(document, name);
        }
    }
    modifiedDocuments = modifiedDocumentsMap.keys();
    if (!modifiedDocuments.isEmpty()) {
        QList<IDocument *> documentsToSave;
        if (silently) {
            documentsToSave = modifiedDocuments;
        } else {
            SaveItemsDialog dia(ICore::dialogParent(), modifiedDocuments);
            if (!message.isEmpty())
                dia.setMessage(message);
            if (!alwaysSaveMessage.isNull())
                dia.setAlwaysSaveMessage(alwaysSaveMessage);
            if (dia.exec() != QDialog::Accepted) {
                if (cancelled)
                    (*cancelled) = true;
                if (alwaysSave)
                    (*alwaysSave) = dia.alwaysSaveChecked();
                if (failedToSave)
                    (*failedToSave) = modifiedDocuments;
                const QStringList filesToDiff = dia.filesToDiff();
                if (!filesToDiff.isEmpty()) {
                    if (auto diffService = DiffService::instance())
                        diffService->diffModifiedFiles(filesToDiff);
                }
                return false;
            }
            if (alwaysSave)
                *alwaysSave = dia.alwaysSaveChecked();
            documentsToSave = dia.itemsToSave();
        }
        // Check for files without write permissions.
        QList<IDocument *> roDocuments;
        foreach (IDocument *document, documentsToSave) {
            if (document->isFileReadOnly())
                roDocuments << document;
        }
        if (!roDocuments.isEmpty()) {
            ReadOnlyFilesDialog roDialog(roDocuments, ICore::dialogParent());
            roDialog.setShowFailWarning(true, DocumentManager::tr(
                                            "Could not save the files.",
                                            "error message"));
            if (roDialog.exec() == ReadOnlyFilesDialog::RO_Cancel) {
                if (cancelled)
                    (*cancelled) = true;
                if (failedToSave)
                    (*failedToSave) = modifiedDocuments;
                return false;
            }
        }
        foreach (IDocument *document, documentsToSave) {
            if (!EditorManagerPrivate::saveDocument(document)) {
                if (cancelled)
                    *cancelled = true;
                notSaved.append(document);
            }
        }
    }
    if (failedToSave)
        (*failedToSave) = notSaved;
    return notSaved.isEmpty();
}

bool DocumentManager::saveDocument(IDocument *document,
                                   const Utils::FilePath &filePath,
                                   bool *isReadOnly)
{
    bool ret = true;
    const Utils::FilePath &savePath = filePath.isEmpty() ? document->filePath() : filePath;
    expectFileChange(savePath); // This only matters to other IDocuments which refer to this file
    bool addWatcher = removeDocument(document); // So that our own IDocument gets no notification at all

    QString errorString;
    if (!document->save(&errorString, filePath, false)) {
        if (isReadOnly) {
            QFile ofi(savePath.toString());
            // Check whether the existing file is writable
            if (!ofi.open(QIODevice::ReadWrite) && ofi.open(QIODevice::ReadOnly)) {
                *isReadOnly = true;
                goto out;
            }
            *isReadOnly = false;
        }
        QMessageBox::critical(ICore::dialogParent(), tr("File Error"),
                              tr("Error while saving file: %1").arg(errorString));
      out:
        ret = false;
    }

    addDocument(document, addWatcher);
    unexpectFileChange(savePath);
    m_instance->updateSaveAll();
    return ret;
}

QString DocumentManager::allDocumentFactoryFiltersString(QString *allFilesFilter = nullptr)
{
    QSet<QString> uniqueFilters;

    for (IEditorFactory *factory : IEditorFactory::allEditorFactories()) {
        for (const QString &mt : factory->mimeTypes()) {
            const QString filter = mimeTypeForName(mt).filterString();
            if (!filter.isEmpty())
                uniqueFilters.insert(filter);
        }
    }

    for (IDocumentFactory *factory : IDocumentFactory::allDocumentFactories()) {
        for (const QString &mt : factory->mimeTypes()) {
            const QString filter = mimeTypeForName(mt).filterString();
            if (!filter.isEmpty())
                uniqueFilters.insert(filter);
        }
    }

    QStringList filters = Utils::toList(uniqueFilters);
    filters.sort();
    const QString allFiles = Utils::allFilesFilterString();
    if (allFilesFilter)
        *allFilesFilter = allFiles;
    filters.prepend(allFiles);
    return filters.join(QLatin1String(";;"));
}

QString DocumentManager::getSaveFileName(const QString &title, const QString &pathIn,
                                     const QString &filter, QString *selectedFilter)
{
    const QString &path = pathIn.isEmpty() ? fileDialogInitialDirectory() : pathIn;
    QString fileName;
    bool repeat;
    do {
        repeat = false;
        fileName = QFileDialog::getSaveFileName(
            ICore::dialogParent(), title, path, filter, selectedFilter, QFileDialog::DontConfirmOverwrite);
        if (!fileName.isEmpty()) {
            // If the selected filter is All Files (*) we leave the name exactly as the user
            // specified. Otherwise the suffix must be one available in the selected filter. If
            // the name already ends with such suffix nothing needs to be done. But if not, the
            // first one from the filter is appended.
            if (selectedFilter && *selectedFilter != Utils::allFilesFilterString()) {
                // Mime database creates filter strings like this: Anything here (*.foo *.bar)
                const QRegularExpression regExp(QLatin1String(".*\\s+\\((.*)\\)$"));
                QRegularExpressionMatchIterator matchIt = regExp.globalMatch(*selectedFilter);
                if (matchIt.hasNext()) {
                    bool suffixOk = false;
                    const QRegularExpressionMatch match = matchIt.next();
                    QString caption = match.captured(1);
                    caption.remove(QLatin1Char('*'));
                    const QStringList suffixes = caption.split(QLatin1Char(' '));
                    for (const QString &suffix : suffixes)
                        if (fileName.endsWith(suffix)) {
                            suffixOk = true;
                            break;
                        }
                    if (!suffixOk && !suffixes.isEmpty())
                        fileName.append(suffixes.at(0));
                }
            }
            if (QFile::exists(fileName)) {
                if (QMessageBox::warning(ICore::dialogParent(), tr("Overwrite?"),
                    tr("An item named \"%1\" already exists at this location. "
                       "Do you want to overwrite it?").arg(QDir::toNativeSeparators(fileName)),
                    QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
                    repeat = true;
                }
            }
        }
    } while (repeat);
    if (!fileName.isEmpty())
        setFileDialogLastVisitedDirectory(QFileInfo(fileName).absolutePath());
    return fileName;
}

QString DocumentManager::getSaveFileNameWithExtension(const QString &title, const QString &pathIn,
                                                  const QString &filter)
{
    QString selected = filter;
    return getSaveFileName(title, pathIn, filter, &selected);
}

/*!
    Asks the user for a new file name (\uicontrol {Save File As}) for \a document.
*/
QString DocumentManager::getSaveAsFileName(const IDocument *document)
{
    QTC_ASSERT(document, return QString());
    const QString filter = allDocumentFactoryFiltersString();
    const QString filePath = document->filePath().toString();
    QString selectedFilter;
    QString fileDialogPath = filePath;
    if (!filePath.isEmpty()) {
        selectedFilter = Utils::mimeTypeForFile(filePath).filterString();
    } else {
        const QString suggestedName = document->fallbackSaveAsFileName();
        if (!suggestedName.isEmpty()) {
            const QList<MimeType> types = Utils::mimeTypesForFileName(suggestedName);
            if (!types.isEmpty())
                selectedFilter = types.first().filterString();
        }
        const QString defaultPath = document->fallbackSaveAsPath();
        if (!defaultPath.isEmpty())
            fileDialogPath = defaultPath + (suggestedName.isEmpty()
                    ? QString()
                    : '/' + suggestedName);
    }
    if (selectedFilter.isEmpty())
        selectedFilter = Utils::mimeTypeForName(document->mimeType()).filterString();

    return getSaveFileName(tr("Save File As"),
                           fileDialogPath,
                           filter,
                           &selectedFilter);
}

/*!
    Silently saves all documents and returns \c true if all modified documents
    are saved successfully.

    This method tries to avoid showing dialogs to the user, but can do so anyway
    (e.g. if a file is not writeable).

    If users canceled any of the dialogs they interacted with, \a canceled
    is set. If passed to the method, \a failedToClose returns a list of
    documents that could not be saved.
*/
bool DocumentManager::saveAllModifiedDocumentsSilently(bool *canceled,
                                                       QList<IDocument *> *failedToClose)
{
    return saveModifiedDocumentsSilently(modifiedDocuments(), canceled, failedToClose);
}

/*!
    Silently saves \a documents and returns \c true if all of them were saved
    successfully.

    This method tries to avoid showing dialogs to the user, but can do so anyway
    (e.g. if a file is not writeable).

    If users canceled any of the dialogs they interacted with, \a canceled
    is set. If passed to the method, \a failedToClose returns a list of
    documents that could not be saved.
*/
bool DocumentManager::saveModifiedDocumentsSilently(const QList<IDocument *> &documents,
                                                    bool *canceled,
                                                    QList<IDocument *> *failedToClose)
{
    return saveModifiedFilesHelper(documents,
                                   QString(),
                                   canceled,
                                   true,
                                   QString(),
                                   nullptr,
                                   failedToClose);
}

/*!
    Silently saves \a document and returns \c true if it was saved successfully.

    This method tries to avoid showing dialogs to the user, but can do so anyway
    (e.g. if a file is not writeable).

    If users canceled any of the dialogs they interacted with, \a canceled
    is set. If passed to the method, \a failedToClose returns a list of
    documents that could not be saved.

*/
bool DocumentManager::saveModifiedDocumentSilently(IDocument *document, bool *canceled,
                                                   QList<IDocument *> *failedToClose)
{
    return saveModifiedDocumentsSilently({document}, canceled, failedToClose);
}

/*!
    Presents a dialog with all modified documents to users and asks them which
    of these should be saved.

    This method may show additional dialogs to the user, e.g. if a file is
    not writeable.

    The dialog text can be set using \a message. If users canceled any
    of the dialogs they interacted with, \a canceled is set and the
    method returns \c false.

    The \a alwaysSaveMessage shows an additional checkbox in the dialog.
    The state of this checkbox is written into \a alwaysSave if set.

    If passed to the method, \a failedToClose returns a list of
    documents that could not be saved.
*/
bool DocumentManager::saveAllModifiedDocuments(const QString &message, bool *canceled,
                                               const QString &alwaysSaveMessage, bool *alwaysSave,
                                               QList<IDocument *> *failedToClose)
{
    return saveModifiedDocuments(modifiedDocuments(), message, canceled,
                                 alwaysSaveMessage, alwaysSave, failedToClose);
}

/*!
    Presents a dialog with \a documents to users and asks them which
    of these should be saved.

    This method may show additional dialogs to the user, e.g. if a file is
    not writeable.

    The dialog text can be set using \a message. If users canceled any
    of the dialogs they interacted with, \a canceled is set and the
    method returns \c false.

    The \a alwaysSaveMessage shows an additional checkbox in the dialog.
    The state of this checkbox is written into \a alwaysSave if set.

    If passed to the method, \a failedToClose returns a list of
    documents that could not be saved.
*/
bool DocumentManager::saveModifiedDocuments(const QList<IDocument *> &documents,
                                            const QString &message, bool *canceled,
                                            const QString &alwaysSaveMessage, bool *alwaysSave,
                                            QList<IDocument *> *failedToClose)
{
    return saveModifiedFilesHelper(documents, message, canceled, false,
                                   alwaysSaveMessage, alwaysSave, failedToClose);
}

/*!
    Presents a dialog with the \a document to users and asks them whether
    it should be saved.

    This method may show additional dialogs to the user, e.g. if a file is
    not writeable.

    The dialog text can be set using \a message. If users canceled any
    of the dialogs they interacted with, \a canceled is set and the
    method returns \c false.

    The \a alwaysSaveMessage shows an additional checkbox in the dialog.
    The state of this checkbox is written into \a alwaysSave if set.

    If passed to the method, \a failedToClose returns a list of
    documents that could not be saved.
*/
bool DocumentManager::saveModifiedDocument(IDocument *document, const QString &message, bool *canceled,
                                           const QString &alwaysSaveMessage, bool *alwaysSave,
                                           QList<IDocument *> *failedToClose)
{
    return saveModifiedDocuments({document}, message, canceled,
                                 alwaysSaveMessage, alwaysSave, failedToClose);
}

void DocumentManager::showFilePropertiesDialog(const FilePath &filePath)
{
    FilePropertiesDialog properties(filePath);
    properties.exec();
}

/*!
    Asks the user for a set of file names to be opened. The \a filters
    and \a selectedFilter arguments are interpreted like in
    QFileDialog::getOpenFileNames(). \a pathIn specifies a path to open the
    dialog in if that is not overridden by the user's policy.
*/

QStringList DocumentManager::getOpenFileNames(const QString &filters,
                                              const QString &pathIn,
                                              QString *selectedFilter)
{
    const QString &path = pathIn.isEmpty() ? fileDialogInitialDirectory() : pathIn;
    const QStringList files = QFileDialog::getOpenFileNames(ICore::dialogParent(),
                                                      tr("Open File"),
                                                      path, filters,
                                                      selectedFilter);
    if (!files.isEmpty())
        setFileDialogLastVisitedDirectory(QFileInfo(files.front()).absolutePath());
    return files;
}

void DocumentManager::changedFile(const QString &fileName)
{
    const FilePath filePath = FilePath::fromString(fileName);
    const bool wasempty = d->m_changedFiles.isEmpty();

    if (d->m_states.contains(filePathKey(filePath, KeepLinks)))
        d->m_changedFiles.insert(filePath);
    qCDebug(log) << "file change notification for" << filePath;

    if (wasempty && !d->m_changedFiles.isEmpty())
        QTimer::singleShot(200, this, &DocumentManager::checkForReload);
}

void DocumentManager::checkForReload()
{
    if (d->m_postponeAutoReload || d->m_changedFiles.isEmpty())
        return;
    if (QApplication::applicationState() != Qt::ApplicationActive)
        return;
    // If d->m_blockActivated is true, then it means that the event processing of either the
    // file modified dialog, or of loading large files, has delivered a file change event from
    // a watcher *and* the timer triggered. We may never end up here in a nested way, so
    // recheck later at the end of the checkForReload function.
    if (d->m_blockActivated)
        return;
    if (QApplication::activeModalWidget()) {
        // We do not want to prompt for modified file if we currently have some modal dialog open.
        // There is no really sensible way to get notified globally if a window closed,
        // so just check on every focus change.
        d->checkOnNextFocusChange();
        return;
    }

    d->m_blockActivated = true;

    IDocument::ReloadSetting defaultBehavior = EditorManager::reloadSetting();
    ReloadPromptAnswer previousReloadAnswer = ReloadCurrent;
    FileDeletedPromptAnswer previousDeletedAnswer = FileDeletedSave;

    QList<IDocument *> documentsToClose;
    QHash<IDocument*, QString> documentsToSave;

    // collect file information
    QMap<FilePath, FileStateItem> currentStates;
    QMap<FilePath, IDocument::ChangeType> changeTypes;
    QSet<IDocument *> changedIDocuments;
    foreach (const FilePath &filePath, d->m_changedFiles) {
        const FilePath fileKey = filePathKey(filePath, KeepLinks);
        qCDebug(log) << "handling file change for" << filePath << "(" << fileKey << ")";
        IDocument::ChangeType type = IDocument::TypeContents;
        FileStateItem state;
        if (!filePath.exists()) {
            qCDebug(log) << "file was removed";
            type = IDocument::TypeRemoved;
        } else {
            state.modified = filePath.lastModified();
            state.permissions = filePath.permissions();
            qCDebug(log) << "file was modified, time:" << state.modified
                         << "permissions: " << state.permissions;
        }
        currentStates.insert(fileKey, state);
        changeTypes.insert(fileKey, type);
        foreach (IDocument *document, d->m_states.value(fileKey).lastUpdatedState.keys())
            changedIDocuments.insert(document);
    }

    // clean up. do this before we may enter the main loop, otherwise we would
    // lose consecutive notifications.
    d->m_changedFiles.clear();

    // collect information about "expected" file names
    // we can't do the "resolving" already in expectFileChange, because
    // if the resolved names are different when unexpectFileChange is called
    // we would end up with never-unexpected file names
    QSet<FilePath> expectedFileKeys;
    foreach (const FilePath &filePath, d->m_expectedFileNames) {
        const FilePath cleanAbsFilePath = filePathKey(filePath, KeepLinks);
        expectedFileKeys.insert(filePathKey(filePath, KeepLinks));
        const FilePath resolvedCleanAbsFilePath = cleanAbsFilePath.canonicalPath();
        if (cleanAbsFilePath != resolvedCleanAbsFilePath)
            expectedFileKeys.insert(filePathKey(filePath, ResolveLinks));
    }

    // handle the IDocuments
    QStringList errorStrings;
    QStringList filesToDiff;
    foreach (IDocument *document, changedIDocuments) {
        IDocument::ChangeTrigger trigger = IDocument::TriggerInternal;
        optional<IDocument::ChangeType> type;
        bool changed = false;
        // find out the type & behavior from the two possible files
        // behavior is internal if all changes are expected (and none removed)
        // type is "max" of both types (remove > contents > permissions)
        foreach (const FilePath &fileKey, d->m_documentsWithWatch.value(document)) {
            // was the file reported?
            if (!currentStates.contains(fileKey))
                continue;

            FileStateItem currentState = currentStates.value(fileKey);
            FileStateItem expectedState = d->m_states.value(fileKey).expected;
            FileStateItem lastState = d->m_states.value(fileKey).lastUpdatedState.value(document);

            // did the file actually change?
            if (lastState.modified == currentState.modified && lastState.permissions == currentState.permissions)
                continue;
            changed = true;

            // was it only a permission change?
            if (lastState.modified == currentState.modified)
                continue;

            // was the change unexpected?
            if ((currentState.modified != expectedState.modified || currentState.permissions != expectedState.permissions)
                    && !expectedFileKeys.contains(fileKey)) {
                trigger = IDocument::TriggerExternal;
            }

            // find out the type
            IDocument::ChangeType fileChange = changeTypes.value(fileKey);
            if (fileChange == IDocument::TypeRemoved)
                type = IDocument::TypeRemoved;
            else if (fileChange == IDocument::TypeContents && !type)
                type = IDocument::TypeContents;
        }

        if (!changed) // probably because the change was blocked with (un)blockFileChange
            continue;

        // handle it!
        d->m_blockedIDocument = document;

        // Update file info, also handling if e.g. link target has changed.
        // We need to do that before the file is reloaded, because removing the watcher will
        // loose any pending change events. Loosing change events *before* the file is reloaded
        // doesn't matter, because in that case we then reload the new version of the file already
        // anyhow.
        removeFileInfo(document);
        addFileInfos({document});

        bool success = true;
        QString errorString;
        // we've got some modification
        // check if it's contents or permissions:
        if (!type) {
            // Only permission change
            document->checkPermissions();
            success = true;
            // now we know it's a content change or file was removed
        } else if (defaultBehavior == IDocument::ReloadUnmodified && type == IDocument::TypeContents
                   && !document->isModified()) {
            // content change, but unmodified (and settings say to reload in this case)
            success = document->reload(&errorString, IDocument::FlagReload, *type);
            // file was removed or it's a content change and the default behavior for
            // unmodified files didn't kick in
        } else if (defaultBehavior == IDocument::ReloadUnmodified && type == IDocument::TypeRemoved
                   && !document->isModified()) {
            // file removed, but unmodified files should be reloaded
            // so we close the file
            documentsToClose << document;
        } else if (defaultBehavior == IDocument::IgnoreAll) {
            // content change or removed, but settings say ignore
            success = document->reload(&errorString, IDocument::FlagIgnore, *type);
            // either the default behavior is to always ask,
            // or the ReloadUnmodified default behavior didn't kick in,
            // so do whatever the IDocument wants us to do
        } else {
            // check if IDocument wants us to ask
            if (document->reloadBehavior(trigger, *type) == IDocument::BehaviorSilent) {
                // content change or removed, IDocument wants silent handling
                if (type == IDocument::TypeRemoved)
                    documentsToClose << document;
                else
                    success = document->reload(&errorString, IDocument::FlagReload, *type);
            // IDocument wants us to ask
            } else if (type == IDocument::TypeContents) {
                // content change, IDocument wants to ask user
                if (previousReloadAnswer == ReloadNone || previousReloadAnswer == ReloadNoneAndDiff) {
                    // answer already given, ignore
                    success = document->reload(&errorString, IDocument::FlagIgnore, IDocument::TypeContents);
                } else if (previousReloadAnswer == ReloadAll) {
                    // answer already given, reload
                    success = document->reload(&errorString, IDocument::FlagReload, IDocument::TypeContents);
                } else {
                    // Ask about content change
                    previousReloadAnswer = reloadPrompt(document->filePath(), document->isModified(),
                                                        DiffService::instance(),
                                                        ICore::dialogParent());
                    switch (previousReloadAnswer) {
                    case ReloadAll:
                    case ReloadCurrent:
                        success = document->reload(&errorString, IDocument::FlagReload, IDocument::TypeContents);
                        break;
                    case ReloadSkipCurrent:
                    case ReloadNone:
                    case ReloadNoneAndDiff:
                        success = document->reload(&errorString, IDocument::FlagIgnore, IDocument::TypeContents);
                        break;
                    case CloseCurrent:
                        documentsToClose << document;
                        break;
                    }
                }
                if (previousReloadAnswer == ReloadNoneAndDiff)
                    filesToDiff.append(document->filePath().toString());

            // IDocument wants us to ask, and it's the TypeRemoved case
            } else {
                // Ask about removed file
                bool unhandled = true;
                while (unhandled) {
                    if (previousDeletedAnswer != FileDeletedCloseAll) {
                        previousDeletedAnswer =
                                fileDeletedPrompt(document->filePath().toString(),
                                                  ICore::dialogParent());
                    }
                    switch (previousDeletedAnswer) {
                    case FileDeletedSave:
                        documentsToSave.insert(document, document->filePath().toString());
                        unhandled = false;
                        break;
                    case FileDeletedSaveAs:
                    {
                        const QString &saveFileName = getSaveAsFileName(document);
                        if (!saveFileName.isEmpty()) {
                            documentsToSave.insert(document, saveFileName);
                            unhandled = false;
                        }
                        break;
                    }
                    case FileDeletedClose:
                    case FileDeletedCloseAll:
                        documentsToClose << document;
                        unhandled = false;
                        break;
                    }
                }
            }
        }
        if (!success) {
            if (errorString.isEmpty())
                errorStrings << tr("Cannot reload %1").arg(document->filePath().toUserOutput());
            else
                errorStrings << errorString;
        }

        d->m_blockedIDocument = nullptr;
    }

    if (!filesToDiff.isEmpty()) {
        if (auto diffService = DiffService::instance())
            diffService->diffModifiedFiles(filesToDiff);
    }

    if (!errorStrings.isEmpty())
        QMessageBox::critical(ICore::dialogParent(), tr("File Error"),
                              errorStrings.join(QLatin1Char('\n')));

    // handle deleted files
    EditorManager::closeDocuments(documentsToClose, false);
    for (auto it = documentsToSave.cbegin(), end = documentsToSave.cend(); it != end; ++it) {
        saveDocument(it.key(), Utils::FilePath::fromString(it.value()));
        it.key()->checkPermissions();
    }

    d->m_blockActivated = false;
    // re-check in case files where modified while the dialog was open
    QMetaObject::invokeMethod(this, &DocumentManager::checkForReload, Qt::QueuedConnection);
//    dump();
}

/*!
    Adds the \a filePath to the list of recent files. Associates the file to
    be reopened with the editor that has the specified \a editorId, if possible.
    \a editorId defaults to the empty ID, which lets \QC figure out
    the best editor itself.
*/
void DocumentManager::addToRecentFiles(const Utils::FilePath &filePath, Id editorId)
{
    if (filePath.isEmpty())
        return;
    const FilePath fileKey = filePathKey(filePath, KeepLinks);
    Utils::erase(d->m_recentFiles, [fileKey](const RecentFile &file) {
        return fileKey == filePathKey(file.first, DocumentManager::KeepLinks);
    });
    while (d->m_recentFiles.count() >= EditorManagerPrivate::maxRecentFiles())
        d->m_recentFiles.removeLast();
    d->m_recentFiles.prepend(RecentFile(filePath, editorId));
}

/*!
    Clears the list of recent files. Should only be called by
    the core plugin when the user chooses to clear the list.
*/
void DocumentManager::clearRecentFiles()
{
    d->m_recentFiles.clear();
}

/*!
    Returns the list of recent files.
*/
QList<DocumentManager::RecentFile> DocumentManager::recentFiles()
{
    return d->m_recentFiles;
}

void DocumentManager::saveSettings()
{
    QVariantList recentFiles;
    QStringList recentEditorIds;
    foreach (const RecentFile &file, d->m_recentFiles) {
        recentFiles.append(file.first.toVariant());
        recentEditorIds.append(file.second.toString());
    }

    QtcSettings *s = ICore::settings();
    s->beginGroup(settingsGroupC);
    s->setValueWithDefault(filesKeyC, recentFiles);
    s->setValueWithDefault(editorsKeyC, recentEditorIds);
    s->endGroup();
    s->beginGroup(directoryGroupC);
    s->setValueWithDefault(projectDirectoryKeyC,
                           d->m_projectsDirectory.toString(),
                           PathChooser::homePath());
    s->setValueWithDefault(useProjectDirectoryKeyC,
                           d->m_useProjectsDirectory,
                           kUseProjectsDirectoryDefault);
    s->endGroup();
}

void readSettings()
{
    QSettings *s = ICore::settings();
    d->m_recentFiles.clear();
    s->beginGroup(QLatin1String(settingsGroupC));
    const QVariantList recentFiles = s->value(QLatin1String(filesKeyC)).toList();
    const QStringList recentEditorIds = s->value(QLatin1String(editorsKeyC)).toStringList();
    s->endGroup();
    // clean non-existing files
    for (int i = 0, n = recentFiles.size(); i < n; ++i) {
        QString editorId;
        if (i < recentEditorIds.size()) // guard against old or weird settings
            editorId = recentEditorIds.at(i);
        const Utils::FilePath &filePath = FilePath::fromVariant(recentFiles.at(i));
        if (filePath.exists() && !filePath.isDir())
            d->m_recentFiles.append({filePath, Id::fromString(editorId)});
    }

    s->beginGroup(QLatin1String(directoryGroupC));
    const FilePath settingsProjectDir = FilePath::fromString(s->value(QLatin1String(projectDirectoryKeyC),
                                                QString()).toString());
    if (!settingsProjectDir.isEmpty() && settingsProjectDir.isDir())
        d->m_projectsDirectory = settingsProjectDir;
    else
        d->m_projectsDirectory = FilePath::fromString(PathChooser::homePath());
    d->m_useProjectsDirectory
        = s->value(QLatin1String(useProjectDirectoryKeyC), kUseProjectsDirectoryDefault).toBool();

    s->endGroup();
}

/*!

  Returns the initial directory for a new file dialog. If there is a current
  document associated with a file, uses that. Or if there is a default location
  for new files, uses that. Otherwise, uses the last visited directory.

  \sa setFileDialogLastVisitedDirectory(), setDefaultLocationForNewFiles()
*/

QString DocumentManager::fileDialogInitialDirectory()
{
    IDocument *doc = EditorManager::currentDocument();
    if (doc && !doc->isTemporary() && !doc->filePath().isEmpty())
        return doc->filePath().absolutePath().path();
    if (!d->m_defaultLocationForNewFiles.isEmpty())
        return d->m_defaultLocationForNewFiles;
    return d->m_lastVisitedDirectory;
}

/*!

  Returns the default location for new files.

  \sa fileDialogInitialDirectory()
*/
QString DocumentManager::defaultLocationForNewFiles()
{
    return d->m_defaultLocationForNewFiles;
}

/*!
 Sets the default \a location for new files.
 */
void DocumentManager::setDefaultLocationForNewFiles(const QString &location)
{
    d->m_defaultLocationForNewFiles = location;
}

/*!

  Returns the directory for projects. Defaults to HOME.

  \sa setProjectsDirectory(), setUseProjectsDirectory()
*/

FilePath DocumentManager::projectsDirectory()
{
    return d->m_projectsDirectory;
}

/*!

  Sets the \a directory for projects.

  \sa projectsDirectory(), useProjectsDirectory()
*/

void DocumentManager::setProjectsDirectory(const FilePath &directory)
{
    if (d->m_projectsDirectory != directory) {
        d->m_projectsDirectory = directory;
        emit m_instance->projectsDirectoryChanged(d->m_projectsDirectory);
    }
}

/*!

    Returns whether the directory for projects is to be used or whether the user
    chose to use the current directory.

  \sa setProjectsDirectory(), setUseProjectsDirectory()
*/

bool DocumentManager::useProjectsDirectory()
{
    return d->m_useProjectsDirectory;
}

/*!

  Sets whether the directory for projects is to be used to
  \a useProjectsDirectory.

  \sa projectsDirectory(), useProjectsDirectory()
*/

void DocumentManager::setUseProjectsDirectory(bool useProjectsDirectory)
{
    d->m_useProjectsDirectory = useProjectsDirectory;
}

/*!

  Returns the last visited directory of a file dialog.

  \sa setFileDialogLastVisitedDirectory(), fileDialogInitialDirectory()

*/

QString DocumentManager::fileDialogLastVisitedDirectory()
{
    return d->m_lastVisitedDirectory;
}

/*!

  Sets the last visited \a directory of a file dialog that will be remembered
  for the next one.

  \sa fileDialogLastVisitedDirectory(), fileDialogInitialDirectory()

  */

void DocumentManager::setFileDialogLastVisitedDirectory(const QString &directory)
{
    d->m_lastVisitedDirectory = directory;
}

void DocumentManager::notifyFilesChangedInternally(const QStringList &files)
{
    emit m_instance->filesChangedInternally(files);
}

void DocumentManager::registerSaveAllAction()
{
    d->registerSaveAllAction();
}

// -------------- FileChangeBlocker

/*!
    \class Core::FileChangeBlocker
    \inheaderfile coreplugin/documentmanager.h
    \inmodule QtCreator

    \brief The FileChangeBlocker class blocks all change notifications to all
    IDocument objects that match the given filename.

    Additionally, the class unblocks in the destructor. To also reload the
    IDocument object in the destructor, set modifiedReload() to \c true.
*/

FileChangeBlocker::FileChangeBlocker(const FilePath &filePath)
    : m_filePath(filePath)
{
    DocumentManager::expectFileChange(filePath);
}

FileChangeBlocker::~FileChangeBlocker()
{
    DocumentManager::unexpectFileChange(m_filePath);
}

} // namespace Core

#include "documentmanager.moc"
