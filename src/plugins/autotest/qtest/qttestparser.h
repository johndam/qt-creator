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

#pragma once

#include "../itestparser.h"

#include "qttesttreeitem.h"

#include <utils/optional.h>

namespace CppTools { class CppModelManager; }

namespace Autotest {
namespace Internal {

class QtTestParseResult : public TestParseResult
{
public:
    explicit QtTestParseResult(ITestFramework *framework) : TestParseResult(framework) {}
    void setInherited(bool inherited) { m_inherited = inherited; }
    bool inherited() const { return m_inherited; }
    TestTreeItem *createTestTreeItem() const override;
private:
    bool m_inherited = false;
};

class QtTestParser : public CppParser
{
public:
    explicit QtTestParser(ITestFramework *framework) : CppParser(framework) {}

    void init(const Utils::FilePaths &filesToParse, bool fullParse) override;
    void release() override;
    bool processDocument(QFutureInterface<TestParseResultPtr> futureInterface,
                         const Utils::FilePath &fileName) override;

private:
    QString testClass(const CppTools::CppModelManager *modelManager,
                      const Utils::FilePath &fileName) const;
    QHash<QString, QtTestCodeLocationList> checkForDataTags(const QString &fileName) const;
    struct TestCaseData {
        Utils::FilePath fileName;
        int line = 0;
        int column = 0;
        QMap<QString, QtTestCodeLocationAndType> testFunctions;
        QHash<QString, QtTestCodeLocationList> dataTags;
        bool valid = false;
    };

    Utils::optional<bool> fillTestCaseData(const QString &testCaseName,
                                           const CPlusPlus::Document::Ptr &doc,
                                           TestCaseData &data) const;
    QtTestParseResult *createParseResult(const QString &testCaseName, const TestCaseData &data,
                                         const QString &projectFile) const;
    QHash<Utils::FilePath, QString> m_testCaseNames;
    QMultiHash<Utils::FilePath, Utils::FilePath> m_alternativeFiles;
};

} // namespace Internal
} // namespace Autotest
