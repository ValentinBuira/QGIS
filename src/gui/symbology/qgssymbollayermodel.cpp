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

#include "qgssymbollayermodel.h"

#include "qgsapplication.h"
#include "qgsexpressioncontextutils.h"
#include "qgsguiutils.h"
#include "qgsstyle.h"
#include "qgssymbol.h"
#include "qgssymbollayer.h"
#include "qgssymbollayerregistry.h"
#include "qgssymbollayerutils.h"
#include "qgsvectorlayer.h"

#include <QStandardItem>
#include <qscreen.h>

#include "moc_qgssymbollayermodel.cpp"

using namespace Qt::StringLiterals;

QgsSymbolLayerModelNode::QgsSymbolLayerModelNode()
{}

QgsSymbolLayerModelNode::QgsSymbolLayerModelNode( QgsSymbolLayer *layer, Qgis::SymbolType symbolType, QgsVectorLayer *vectorLayer, QScreen *screen )
  : mVectorLayer( vectorLayer )
  , mScreen( screen )
{
  setLayer( layer, symbolType );
}

QgsSymbolLayerModelNode::QgsSymbolLayerModelNode( QgsSymbol *symbol, QgsVectorLayer *vectorLayer, QScreen *screen )
  : mVectorLayer( vectorLayer )
  , mScreen( screen )
{
  setSymbol( symbol );
}

QgsSymbolLayerModelNode::~QgsSymbolLayerModelNode()
{
  deleteChildren();
}

void QgsSymbolLayerModelNode::setLayer( QgsSymbolLayer *layer, Qgis::SymbolType symbolType )
{
  mLayer = layer;
  mIsLayer = true;
  mSymbol = nullptr;
  mSymbolType = symbolType;
  updatePreview();
}

void QgsSymbolLayerModelNode::setSymbol( QgsSymbol *symbol )
{
  mSymbol = symbol;
  mIsLayer = false;
  mLayer = nullptr;
  updatePreview();
}

void QgsSymbolLayerModelNode::updatePreview() //TODO check if this works
{
  if ( !mSize.isValid() )
  {
    const int size = QgsGuiUtils::scaleIconSize( 16 );
    mSize = QSize( size, size );
  }

  if ( auto *lParent = parent() )
    static_cast<QgsSymbolLayerModelNode *>( lParent )->updatePreview();

  //Set Data icon
}

QIcon QgsSymbolLayerModelNode::icon() const
{
  QIcon icon;
  if ( mIsLayer )
    icon = QgsSymbolLayerUtils::
      symbolLayerPreviewIcon( mLayer, Qgis::RenderUnit::Millimeters, mSize, QgsMapUnitScale(), mSymbol ? mSymbol->type() : mSymbolType, mVectorLayer, QgsScreenProperties( mScreen.data() ) );
  else
  {
    QgsExpressionContext expContext;
    expContext.appendScopes( QgsExpressionContextUtils::globalProjectLayerScopes( mVectorLayer ) );
    icon = QIcon( QgsSymbolLayerUtils::symbolPreviewPixmap( mSymbol, mSize, 0, nullptr, false, &expContext, nullptr, QgsScreenProperties( mScreen.data() ) ) );
  }
  return icon;
}

QVariant QgsSymbolLayerModelNode::data( int role ) const
{
  if ( role == Qt::DisplayRole || role == Qt::EditRole )
  {
    if ( mIsLayer )
    {
      QgsSymbolLayerAbstractMetadata *m = QgsApplication::symbolLayerRegistry()->symbolLayerMetadata( mLayer->layerType() );
      if ( m )
        return m->visibleName();
      else
        return QString();
    }
    else
    {
      switch ( mSymbol->type() )
      {
        case Qgis::SymbolType::Marker:
          return QCoreApplication::translate( "QgsSymbolLayerModelNode", "Marker" );
        case Qgis::SymbolType::Fill:
          return QCoreApplication::translate( "QgsSymbolLayerModelNode", "Fill" );
        case Qgis::SymbolType::Line:
          return QCoreApplication::translate( "QgsSymbolLayerModelNode", "Line" );
        default:
          return "Symbol";
      }
    }
  }
  else if ( role == Qt::ForegroundRole && mIsLayer )
  {
    if ( !mLayer->enabled() )
    {
      QPalette pal = qApp->palette();
      // QBrush brush = QStandardItem::data( role ).value<QBrush>();
      QBrush brush = QBrush(); // TODO check if this work
      brush.setColor( pal.color( QPalette::Disabled, QPalette::WindowText ) );
      return brush;
    }
    else
    {
      return QVariant();
    }
  }
  else if ( role == Qt::DecorationRole )
  {
    return icon();
  }
  else if ( role == Qt::FontRole && !mIsLayer )
  {
    QFont font;
    font.setBold( true );
    return font;
  }

  //      if ( role == Qt::SizeHintRole )
  //        return QVariant( QSize( 32, 32 ) );
  if ( role == Qt::CheckStateRole )
    return QVariant(); // could be true/false
  // return QStandardItem::data( role );
  return QVariant();
}


void QgsSymbolLayerModelNode::deleteChildren()
{
  qDeleteAll( mChildren );
  mChildren.clear();
}

void QgsSymbolLayerModelNode::addChildNode( QgsSymbolLayerModelNode *node )
{
  if ( !node )
    return;

  Q_ASSERT( !node->mParent );
  node->mParent = this;

  mChildren.append( node );
}

bool QgsSymbolLayerModelNode::expanded() const
{
  return mExpanded;
}

void QgsSymbolLayerModelNode::setExpanded( bool expanded )
{
  if ( mExpanded == expanded )
    return;

  mExpanded = expanded;
}


int QgsSymbolLayerModelNode::myRowCount() const
{
  return children().count();
}

int QgsSymbolLayerModelNode::myRow() const
{
  if ( !mParent )
    return -1;

  return mParent->children().indexOf( this );
}

QgsSymbolLayerModel::QgsSymbolLayerModel( QgsVectorLayer *vl, QObject *parent )
  : QAbstractItemModel( parent )
  , mVectorLayer( vl )
  , mRootNode( std::make_unique<QgsSymbolLayerModelNode>() )
{}


QVariant QgsSymbolLayerModel::data( const QModelIndex &index, int role ) const
{
  if ( !index.isValid() )
    return QVariant();

  QgsSymbolLayerModelNode *node = index2node( index );
  if ( !node )
    return QVariant();

  return node->data( role );
};

int QgsSymbolLayerModel::rowCount( const QModelIndex &parent ) const
{
  QgsSymbolLayerModelNode *n = index2node( parent );
  if ( !n )
    return 0;

  return n->children().count();
};

int QgsSymbolLayerModel::columnCount( const QModelIndex & ) const
{
  return 1;
};

QModelIndex QgsSymbolLayerModel::index( int row, int column, const QModelIndex &parent ) const
{
  if ( !hasIndex( row, column, parent ) )
    return QModelIndex();

  QgsSymbolLayerModelNode *node = index2node( parent );
  if ( !node )
    return QModelIndex(); // have no children

  return createIndex( row, column, static_cast<QObject *>( node->children().at( row ) ) );
};

QModelIndex QgsSymbolLayerModel::parent( const QModelIndex &child ) const
{
  if ( !child.isValid() )
    return QModelIndex();

  if ( QgsSymbolLayerModelNode *node = index2node( child ) )
  {
    return indexOfParentTreeNode( node->parent() ); // must not be null
  }
  else
  {
    Q_ASSERT( false ); // no other node types!
    return QModelIndex();
  }
};

QModelIndex QgsSymbolLayerModel::indexOfParentTreeNode( QgsSymbolLayerModelNode *parentNode ) const
{
  Q_ASSERT( parentNode );

  QgsSymbolLayerModelNode *grandParentNode = parentNode->parent();
  if ( !grandParentNode )
    return QModelIndex(); // root node -> invalid index

  int row = grandParentNode->children().indexOf( parentNode );
  Q_ASSERT( row >= 0 );

  return createIndex( row, 0, parentNode );
};

QgsSymbolLayerModelNode *QgsSymbolLayerModel::index2node( const QModelIndex &index ) const
{
  if ( !index.isValid() )
    return mRootNode.get();

  return reinterpret_cast<QgsSymbolLayerModelNode *>( index.internalPointer() );
};

QModelIndex QgsSymbolLayerModel::node2index( QgsSymbolLayerModelNode *node ) const
{
  if ( !node || !node->parent() )
    return QModelIndex(); // this is the only root item -> invalid index

  QModelIndex parentIndex = node2index( node->parent() );

  int row = node->parent()->children().indexOf( node );
  Q_ASSERT( row >= 0 );
  return index( row, 0, parentIndex );
};

void QgsSymbolLayerModel::rebuild()
{
  beginResetModel();
  mRootNode->deleteChildren();
  loadSymbol( mSymbol, mRootNode.get() );
  endResetModel();
}

void QgsSymbolLayerModel::updateNode( QgsSymbol *symbol, QgsSymbolLayerModelNode *parent )
{
  const QModelIndex parentIndex = node2index( parent );
  beginRemoveRows( parentIndex, 0, rowCount( parentIndex ) - 1 );
  parent->deleteChildren();
  endRemoveRows();

  beginInsertRows( parentIndex, 0, symbol->symbolLayerCount() - 1 );
  // loadSymbol( symbol, parent );


  // QgsSymbolLayerModelNode *symbolNode = new QgsSymbolLayerModelNode( symbol, mVectorLayer, nullptr /*screen()*/ );
  // parent->addChildNode( symbolNode );


  const int count = symbol->symbolLayerCount();
  for ( int i = count - 1; i >= 0; i-- )
  {
    QgsSymbolLayerModelNode *layerNode = new QgsSymbolLayerModelNode( symbol->symbolLayer( i ), symbol->type(), mVectorLayer, nullptr /*screen()*/ );
    //   layerItem->setEditable( false ); TODO check if this is set to false in QAbstractItemModel::flags
    parent->addChildNode( layerNode );
    if ( symbol->symbolLayer( i )->subSymbol() )
    {
      loadSymbol( symbol->symbolLayer( i )->subSymbol(), layerNode );
    }
    layerNode->setExpanded( true );
  }
  parent->setExpanded( true );

  endInsertRows();
}


void QgsSymbolLayerModel::setSymbol( QgsSymbol *symbol )
{
  if ( symbol )
  {
    mSymbol = symbol;
  }
}

void QgsSymbolLayerModel::loadSymbol( QgsSymbol *symbol, QgsSymbolLayerModelNode *parent )
{
  if ( !symbol )
    return;

  if ( !parent )
  {
    return;
  }

  QgsSymbolLayerModelNode *symbolNode = new QgsSymbolLayerModelNode( symbol, mVectorLayer, nullptr /*screen()*/ );
  parent->addChildNode( symbolNode );


  const int count = symbol->symbolLayerCount();
  for ( int i = count - 1; i >= 0; i-- )
  {
    QgsSymbolLayerModelNode *layerNode = new QgsSymbolLayerModelNode( symbol->symbolLayer( i ), symbol->type(), mVectorLayer, nullptr /*screen()*/ );
    //   layerItem->setEditable( false ); TODO check if this is set to false in QAbstractItemModel::flags
    symbolNode->addChildNode( layerNode );
    if ( symbol->symbolLayer( i )->subSymbol() )
    {
      loadSymbol( symbol->symbolLayer( i )->subSymbol(), layerNode );
    }
    layerNode->setExpanded( true );
  }
  symbolNode->setExpanded( true );


  //TODO keep in the view
  // if ( mSymbol == symbol && !layersTree->currentIndex().isValid() )
  // {
  //   // make sure root item for symbol is selected in tree
  //   layersTree->setCurrentIndex( symbolItem->index() );
  // }
}

// void QgsSymbolLayerModel::reloadSymbol()
// {
//   // clear();
//   // loadSymbol( mSymbol.get(), mRootNode.get() );
// }
