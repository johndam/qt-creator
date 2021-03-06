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

#include "devicefactoryselectiondialog.h"
#include "ui_devicefactoryselectiondialog.h"

#include "idevice.h"
#include "idevicefactory.h"

#include <utils/fileutils.h>

#include <QPushButton>

namespace ProjectExplorer {
namespace Internal {

DeviceFactorySelectionDialog::DeviceFactorySelectionDialog(QWidget *parent) :
    QDialog(parent), ui(new Ui::DeviceFactorySelectionDialog)
{
    ui->setupUi(this);
    ui->buttonBox->button(QDialogButtonBox::Ok)->setText(tr("Start Wizard"));

    for (const IDeviceFactory * const factory : IDeviceFactory::allDeviceFactories()) {
        if (!factory->canCreate())
            continue;
        QListWidgetItem *item = new QListWidgetItem(factory->displayName());
        item->setData(Qt::UserRole, QVariant::fromValue(factory->deviceType()));
        ui->listWidget->addItem(item);
    }

    connect(ui->listWidget, &QListWidget::itemSelectionChanged,
            this, &DeviceFactorySelectionDialog::handleItemSelectionChanged);
    connect(ui->listWidget, &QListWidget::itemDoubleClicked,
            this, &DeviceFactorySelectionDialog::handleItemDoubleClicked);
    handleItemSelectionChanged();
}

DeviceFactorySelectionDialog::~DeviceFactorySelectionDialog()
{
    delete ui;
}

void DeviceFactorySelectionDialog::handleItemSelectionChanged()
{
    ui->buttonBox->button(QDialogButtonBox::Ok)
        ->setEnabled(!ui->listWidget->selectedItems().isEmpty());
}

void DeviceFactorySelectionDialog::handleItemDoubleClicked()
{
    accept();
}

Utils::Id DeviceFactorySelectionDialog::selectedId() const
{
    QList<QListWidgetItem *> selected = ui->listWidget->selectedItems();
    if (selected.isEmpty())
        return Utils::Id();
    return selected.at(0)->data(Qt::UserRole).value<Utils::Id>();
}

} // namespace Internal
} // namespace ProjectExplorer
