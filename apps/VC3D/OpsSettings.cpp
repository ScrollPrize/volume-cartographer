#include "OpsSettings.hpp"
#include "ui_OpsSettings.h"

#include "OpChain.hpp"
#include "formsetsrc.hpp"

OpsSettings::OpsSettings(QWidget* parent)
    : QWidget(parent), ui(new Ui::OpsSettings)
{
    ui->setupUi(this);
    _box = this->findChild<QGroupBox*>("groupBox");
    _box->setVisible(false); // start invisible until a layer is selected
    _enable = this->findChild<QCheckBox*>("chkEnableLayer");    

    connect(_enable, &QCheckBox::stateChanged, this, &OpsSettings::onEnabledChanged);
}

OpsSettings::~OpsSettings() { delete ui; }

void OpsSettings::onEnabledChanged()
{
    if (_chain) {
        _chain->setEnabled((DeltaSurface*)_op, _enable->isChecked());
        sendOpChainChanged(_chain);
    }
}

QWidget *op_form_widget(Surface *op, OpsSettings *parent)
{
    if (!op)
        return nullptr;
    
    if (dynamic_cast<OpChain*>(op)) {
        auto w = new FormSetSrc(op, parent);
        //TODO inherit all settings form widgets from common base, unify the connect
        QWidget::connect(w, &FormSetSrc::sendOpChainChanged, parent, &OpsSettings::sendOpChainChanged);
        return w;
    }

    return nullptr;
}


void OpsSettings::onOpSelected(Surface *op, OpChain *chain)
{
    _op = op;
    _chain = chain;

    // If we have no layer selected (e.g. because a new surface was selected to
    // display which resets the layer selection), hide the box until a
    // layer actually is selected.
    if(!_op) {
        _box->setVisible(false);
        return;
    }

    _box->setTitle(tr("Selected Layer: %1").arg(QString(op_name(op))));    

    if (!dynamic_cast<DeltaSurface*>(_op))
        _enable->setEnabled(false);
    else {
        _enable->setEnabled(true);
        QSignalBlocker blocker(_enable);
        _enable->setChecked(_chain->enabled((DeltaSurface*)_op));
    }
    
    if (_form)
        delete _form;
    
    _form = op_form_widget(op, this);
    if (_form) {
        _box->layout()->addWidget(_form);
        _box->setVisible(true);
    }
}
