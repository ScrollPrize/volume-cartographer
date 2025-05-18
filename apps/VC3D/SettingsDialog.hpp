#pragma once

#include "ui_VCSettings.h"
#include <QStringList>

namespace ChaoVis
{

class SettingsDialog : public QDialog, private Ui_VCSettingsDlg
{
    Q_OBJECT

    public:
        SettingsDialog(QWidget* parent = nullptr);

        static std::vector<int> expandSettingToIntRange(const QString& setting);
        
        // Updates the default volume combobox with available volumes
        void updateVolumeList(const QStringList& volumeIds);

    protected slots:
        void accept() override;
};

}
