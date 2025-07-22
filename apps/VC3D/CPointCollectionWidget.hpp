#pragma once

#include <QDockWidget>
#include "VCCollection.hpp"
#include <QTreeView>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <QGroupBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QItemSelection>


namespace ChaoVis {

class VCCollection;

class CPointCollectionWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit CPointCollectionWidget(VCCollection *collection, QWidget *parent = nullptr);

signals:
    void collectionSelected(uint64_t collectionId);

public slots:
    void selectCollection(uint64_t collectionId);

private slots:
    void refreshTree();
    void onCollectionAdded(uint64_t collectionId);
    void onCollectionChanged(uint64_t collectionId);
    void onCollectionRemoved(uint64_t collectionId);
    void onPointAdded(const ColPoint& point);
    void onPointChanged(const ColPoint& point);
    void onPointRemoved(uint64_t pointId);

    void onResetClicked();
    void onSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected);
    void onNewNameClicked();
    void onNameEdited(const QString &name);
    void onAbsoluteWindingChanged(int state);
    void onColorButtonClicked();

private:
    void setupUi();
    void updateMetadataWidgets();
    QStandardItem* findCollectionItem(uint64_t collectionId);

    VCCollection *_point_collection = nullptr;
    uint64_t _selected_collection_id = 0;
    uint64_t _selected_point_id = 0;

    QTreeView *_tree_view;
    QStandardItemModel *_model;
    QPushButton *_reset_button;

    QGroupBox *_collection_metadata_group;
    QLineEdit *_collection_name_edit;
    QPushButton *_new_name_button;
    QCheckBox *_absolute_winding_checkbox;
    QPushButton *_color_button;

    QGroupBox *_point_metadata_group;
    QLabel* _winding_label;
};

}
