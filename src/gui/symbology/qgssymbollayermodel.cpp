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

static const int SYMBOL_LAYER_ITEM_TYPE = QStandardItem::UserType + 1;

// Hybrid model node which may represent a symbol or a layer
// Check using node->isLayer()
class QgsSymbolLayerModelNode : public QObject
{
  public:
    explicit QgsSymbolLayerModelNode( QgsSymbolLayer *layer, Qgis::SymbolType symbolType, QgsVectorLayer *vectorLayer, QScreen *screen )
      : mVectorLayer( vectorLayer )
      , mScreen( screen )
    {
      setLayer( layer, symbolType );
    }

    explicit QgsSymbolLayerModelNode( QgsSymbol *symbol, QgsVectorLayer *vectorLayer, QScreen *screen )
      : mVectorLayer( vectorLayer )
      , mScreen( screen )
    {
      setSymbol( symbol );
    }

    void setLayer( QgsSymbolLayer *layer, Qgis::SymbolType symbolType )
    {
      mLayer = layer;
      mIsLayer = true;
      mSymbol = nullptr;
      mSymbolType = symbolType;
      updatePreview();
    }

    void setSymbol( QgsSymbol *symbol )
    {
      mSymbol = symbol;
      mIsLayer = false;
      mLayer = nullptr;
      updatePreview();
    }

    void updatePreview() //TODO check if this works
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

    int type() const { return SYMBOL_LAYER_ITEM_TYPE; }
    bool isLayer() const { return mIsLayer; }

    QIcon icon() const
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

    // returns the symbol pointer; helpful in determining a layer's parent symbol
    QgsSymbol *symbol() { return mSymbol; }

    QgsSymbolLayer *layer() { return mLayer; }

    QVariant data( int role ) const
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

      //      if ( role == Qt::SizeHintRole )
      //        return QVariant( QSize( 32, 32 ) );
      if ( role == Qt::CheckStateRole )
        return QVariant(); // could be true/false
      // return QStandardItem::data( role );
      return QVariant();
    }

    //! Gets pointer to the parent. If parent is NULLPTR, the node is a root node
    QgsSymbolLayerModelNode *parent() { return mParent; }

    /**
     * Returns a list of children belonging to the node.
     */
    QList<QgsSymbolLayerModelNode *> children() { return mChildren; }

    /**
     * Returns a list of children belonging to the node.
     */
    QList<QgsSymbolLayerModelNode *> children() const { return mChildren; }

    QgsSymbolLayer *mLayer = nullptr;
    QgsSymbol *mSymbol = nullptr;
    QPointer<QgsVectorLayer> mVectorLayer;
    bool mIsLayer = false;
    QSize mSize;
    Qgis::SymbolType mSymbolType = Qgis::SymbolType::Hybrid;
    QPointer<QScreen> mScreen;

    QgsSymbolLayerModelNode *mParent = nullptr;
    QList<QgsSymbolLayerModelNode *> mChildren;
};


QgsSymbolLayerModel::QgsSymbolLayerModel( QObject *parent )
  : QAbstractItemModel( parent )
// , mRootNode( std::make_unique<QgsCoordinateReferenceSystemModelGroupNode>( QString(), QIcon(), QString() ) )
{
  // mCrsDbRecords = QgsApplication::coordinateReferenceSystemRegistry()->crsDbRecords();

  // rebuild();

  // connect( QgsApplication::coordinateReferenceSystemRegistry(), &QgsCoordinateReferenceSystemRegistry::userCrsAdded, this, &QgsCoordinateReferenceSystemModel::userCrsAdded );
  // connect( QgsApplication::coordinateReferenceSystemRegistry(), &QgsCoordinateReferenceSystemRegistry::userCrsRemoved, this, &QgsCoordinateReferenceSystemModel::userCrsRemoved );
  // connect( QgsApplication::coordinateReferenceSystemRegistry(), &QgsCoordinateReferenceSystemRegistry::userCrsChanged, this, &QgsCoordinateReferenceSystemModel::userCrsChanged );
}

// Qt::ItemFlags QgsSymbolLayerModel::flags( const QModelIndex &index ) const
// {}

QVariant QgsSymbolLayerModel::data( const QModelIndex &index, int role ) const
{
  if ( !index.isValid() )
    return QVariant();

  QgsSymbolLayerModelNode *node = index2node( index );
  if ( !node )
    return QVariant();

  return node->data( role );
};
// QVariant QgsSymbolLayerModel::headerData( int section, Qt::Orientation orientation, int role ) const {

// };
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


void QgsSymbolLayerModel::loadSymbol( QgsSymbol *symbol, QgsSymbolLayerModelNode *parent )
{
  if ( !symbol )
    return;

  if ( !parent )
  {
    mSymbol = symbol;
    clear();
    parent = static_cast<SymbolLayerItem *>( mSymbolLayersModel->invisibleRootItem() );
  }

  QgsSymbolLayerModelNode *symbolNode = new QgsSymbolLayerModelNode( symbol, mVectorLayer, screen() );
  QFont boldFont = symbolNode->font();
  boldFont.setBold( true );
  symbolNode->setFont( boldFont );
  parent->appendRow( symbolNode );

  const int count = symbol->symbolLayerCount();
  for ( int i = count - 1; i >= 0; i-- )
  {
    SymbolLayerItem *layerItem = new SymbolLayerItem( symbol->symbolLayer( i ), symbol->type(), mVectorLayer, screen() );
    layerItem->setEditable( false );
    symbolItem->appendRow( layerItem );
    if ( symbol->symbolLayer( i )->subSymbol() )
    {
      loadSymbol( symbol->symbolLayer( i )->subSymbol(), layerItem );
    }
    layersTree->setExpanded( layerItem->index(), true );
  }
  layersTree->setExpanded( symbolItem->index(), true );

  if ( mSymbol == symbol && !layersTree->currentIndex().isValid() )
  {
    // make sure root item for symbol is selected in tree
    layersTree->setCurrentIndex( symbolItem->index() );
  }
}

void QgsSymbolLayerModel::reloadSymbol()
{
  clear();
  loadSymbol( mSymbol.get(), mRootNode.get() );
}
