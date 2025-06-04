#ifndef OPSSETTINGS_HPP
#define OPSSETTINGS_HPP

#include <QWidget>

class Surface;
class QGroupBox;
class QCheckBox;
class OpChain;

namespace Ui
{
class OpsSettings;
}

class OpsSettings : public QWidget
{
    Q_OBJECT

public:
    explicit OpsSettings(QWidget* parent = nullptr);
    ~OpsSettings();

public slots:
    void onOpSelected(Surface *op, OpChain *chain);
    void onEnabledChanged();

signals:
    void sendOpChainChanged(OpChain *chain);

private:
    Ui::OpsSettings* ui;
    QGroupBox *_box;
    QCheckBox *_enable;

    Surface *_op = nullptr;
    OpChain *_chain = nullptr;
    
    QWidget *_form = nullptr;
};

#endif  // OPSSETTINGS_HPP
