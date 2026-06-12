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

#include <QAbstractItemModel>

class QgsSymbolLayerModelNode;

class QgsSymbolLayerModel : public QAbstractItemModel
{
    Q_OBJECT

  public:
    /**
   * Constructor for QgsSymbolLayerModel, with the specified \a parent object.
   */
    QgsSymbolLayerModel( QObject *parent SIP_TRANSFERTHIS = nullptr );

    // Qt::ItemFlags flags( const QModelIndex &index ) const override;
    QVariant data( const QModelIndex &index, int role ) const override;
    // QVariant headerData( int section, Qt::Orientation orientation, int role ) const override;
    int rowCount( const QModelIndex &parent = QModelIndex() ) const override;
    int columnCount( const QModelIndex & = QModelIndex() ) const override;
    QModelIndex index( int row, int column, const QModelIndex &parent = QModelIndex() ) const override;
    QModelIndex parent( const QModelIndex &child ) const override;

    void loadSymbol( QgsSymbol *symbol, SymbolLayerNode *parent );
    void reloadSymbol();

    QgsSymbolLayerModelNode *index2node( const QModelIndex &index ) const;


  private:
    QModelIndex indexOfParentTreeNode( QgsSymbolLayerModelNode *parentNode ) const;

    QModelIndex node2index( QgsSymbolLayerModelNode *node ) const;

    std::unique_ptr<QgsSymbolLayerModelNode> mRootNode;

    std::unique_ptr<QgsSymbol> mSymbol;
};

#endif
