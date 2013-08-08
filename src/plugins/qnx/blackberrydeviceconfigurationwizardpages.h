/**************************************************************************
**
** Copyright (C) 2011 - 2013 Research In Motion
**
** Contact: Research In Motion (blackberry-qt@qnx.com)
** Contact: KDAB (info@kdab.com)
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
****************************************************************************/

#ifndef QNX_INTERNAL_BLACKBERRYDEVICECONFIGURATIONWIZARDPAGES_H
#define QNX_INTERNAL_BLACKBERRYDEVICECONFIGURATIONWIZARDPAGES_H

#include "blackberrydevicelistdetector.h"

#include <projectexplorer/devicesupport/idevice.h>

#include <QWizardPage>
#include <QListWidgetItem>

namespace QSsh {
class SshKeyGenerator;
}

namespace Qnx {
namespace Internal {
namespace Ui {
class BlackBerryDeviceConfigurationWizardSetupPage;
class BlackBerryDeviceConfigurationWizardSshKeyPage;
}
class BlackBerrySshKeysGenerator;

class BlackBerryDeviceConfigurationWizardSetupPage : public QWizardPage
{
    Q_OBJECT
public:
    enum ItemKind {
        SpecifyManually, Autodetected, PleaseWait, Note
    };

    explicit BlackBerryDeviceConfigurationWizardSetupPage(QWidget *parent = 0);
    ~BlackBerryDeviceConfigurationWizardSetupPage();

    void initializePage();
    bool isComplete() const;

    QString deviceName() const;
    QString hostName() const;
    QString password() const;
    QString debugToken() const;
    ProjectExplorer::IDevice::MachineType machineType() const;
private slots:
    void requestDebugToken();
    void onDeviceSelectionChanged();
    void onDeviceDetected(const QString &deviceName, const QString &hostName, bool isSimulator);
    void onDeviceListDetectorFinished();

private:
    void refreshDeviceList();
    QListWidgetItem *createDeviceListItem(const QString &displayName, ItemKind itemKind) const;
    QListWidgetItem *findDeviceListItem(ItemKind itemKind) const;

    Ui::BlackBerryDeviceConfigurationWizardSetupPage *m_ui;
    BlackBerryDeviceListDetector *m_deviceListDetector;
};

class BlackBerryDeviceConfigurationWizardSshKeyPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit BlackBerryDeviceConfigurationWizardSshKeyPage(QWidget *parent = 0);
    ~BlackBerryDeviceConfigurationWizardSshKeyPage();

    void initializePage();
    bool isComplete() const;

    QString privateKey() const;
    QString publicKey() const;

private slots:
    void findMatchingPublicKey(const QString &privateKeyPath);

    void sshKeysGenerationFailed(const QString &error);
    void processSshKeys(const QString &privateKeyPath, const QByteArray &privateKey, const QByteArray &publicKey);
    void generateSshKeys();

private:
    bool saveKeys(const QByteArray &privateKey, const QByteArray &publicKey, const QString &privateKeyPath, const QString &publicKeyPath);
    void setBusy(bool busy);

    Ui::BlackBerryDeviceConfigurationWizardSshKeyPage *m_ui;
};

class BlackBerryDeviceConfigurationWizardFinalPage : public QWizardPage
{
    Q_OBJECT
public:
    explicit BlackBerryDeviceConfigurationWizardFinalPage(QWidget *parent = 0);
};

} // namespace Internal
} // namespace Qnx

Q_DECLARE_METATYPE(Qnx::Internal::BlackBerryDeviceConfigurationWizardSetupPage::ItemKind)

#endif // QNX_INTERNAL_BLACKBERRYDEVICECONFIGURATIONWIZARDPAGES_H
