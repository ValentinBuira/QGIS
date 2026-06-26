/***************************************************************************
    qgssymbollayermodel.h
    ---------------------
    begin                : June 2026
    copyright            : (C) 2026 by Valentin Buira
    email                : valentin dot buira at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#ifndef QGSSYMBOLLAYERMODEL_H
#define QGSSYMBOLLAYERMODEL_H

#include "qgsvectorlayer.h"

#include <QAbstractItemModel>

class QgsSymbolLayerModelNode;
class QgsSymbol;
class QScreen;
class QgsSymbolLayer;
// class QgsVectorLayer;

// #include "qgis_gui.h"
// #include "qgis_sip.h"

// #include "qgspanelwidget.h"
//
// #include "qgssymbolwidgetcontext.h"


// Hybrid model node which may represent a symbol or a layer
// Check using node->isLayer()
class QgsSymbolLayerModelNode : public QObject
{
    Q_OBJECT
  public:
    QgsSymbolLayerModelNode();
    QgsSymbolLayerModelNode( QgsSymbolLayer *layer, Qgis::SymbolType symbolType, QgsVectorLayer *vectorLayer, QScreen *screen );
    QgsSymbolLayerModelNode( QgsSymbol *symbol, QgsVectorLayer *vectorLayer, QScreen *screen );

    ~QgsSymbolLayerModelNode() override;
    void setLayer( QgsSymbolLayer *layer, Qgis::SymbolType symbolType );
    void setSymbol( QgsSymbol *symbol );

    void updatePreview();

    bool isLayer() const { return mIsLayer; }

    QIcon icon() const;

    QVariant data( int role ) const;

    // returns the symbol pointer; helpful in determining a layer's parent symbol
    QgsSymbol *symbol() { return mSymbol; }

    QgsSymbolLayer *layer() { return mLayer; }

    void addChildNode( QgsSymbolLayerModelNode *node );
    void deleteChildren();

    bool expanded() const;
    void setExpanded( bool expanded );

    //! Gets pointer to the parent. If parent is NULLPTR, the node is a root node
    QgsSymbolLayerModelNode *parent() { return mParent; }

    bool isRootNode() const { return mParent == nullptr; }

    /**
     * Returns a list of children belonging to the node.
     */
    QList<QgsSymbolLayerModelNode *> children() { return mChildren; }

    /**
     * Returns a list of children belonging to the node.
     */
    QList<QgsSymbolLayerModelNode *> children() const { return mChildren; }


    int myRowCount() const;
    int myRow() const;

  private:
    QgsSymbolLayer *mLayer = nullptr;
    QgsSymbol *mSymbol = nullptr;
    QPointer<QgsVectorLayer> mVectorLayer;
    bool mIsLayer = false;
    QSize mSize;
    Qgis::SymbolType mSymbolType = Qgis::SymbolType::Hybrid;
    QPointer<QScreen> mScreen;

    QgsSymbolLayerModelNode *mParent = nullptr;
    QList<QgsSymbolLayerModelNode *> mChildren;

    //! whether the node should be shown in GUI as expanded
    bool mExpanded = true;
};


class QgsSymbolLayerModel : public QAbstractItemModel
{
    Q_OBJECT

  public:
    /**
   * Constructor for QgsSymbolLayerModel, with the specified \a parent object.
   */
    QgsSymbolLayerModel( QgsVectorLayer *vl, QObject *parent SIP_TRANSFERTHIS = nullptr );

    // Qt::ItemFlags flags( const QModelIndex &index ) const override;
    QVariant data( const QModelIndex &index, int role ) const override;
    // QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex & = QModelIndex() ) const override;
    QModelIndex index( int row, int column, const QModelIndex &parent = QModelIndex() ) const override;
    QModelIndex parent( const QModelIndex &child ) const override;

    void rebuild();
    void updateNode( QgsSymbol *symbol, QgsSymbolLayerModelNode *parent );

    void setSymbol( QgsSymbol *symbol );
    void loadSymbol( QgsSymbol *symbol, QgsSymbolLayerModelNode *parent );

    //! TMP while be remove with QStandartItemModel
    // void reloadSymbol();

    QModelIndex node2index( QgsSymbolLayerModelNode *node ) const;
    QgsSymbolLayerModelNode *index2node( const QModelIndex &index ) const;

    QgsSymbolLayerModelNode *rootNode() const { return mRootNode.get(); }


  private:
    QModelIndex indexOfParentTreeNode( QgsSymbolLayerModelNode *parentNode ) const;


    //! Returns whether the node should be shown as expanded or collapsed in GUI
    bool expanded() const;
    //! Sets whether the node should be shown as expanded or collapsed in GUI
    void setExpanded( bool expanded );

    std::unique_ptr<QgsSymbolLayerModelNode> mRootNode;

    QgsSymbol *mSymbol = nullptr;


    QPointer<QgsVectorLayer> mVectorLayer;
};

#endif
