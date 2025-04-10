/***************************************************************************
    qgsblockingnetworkrequest.cpp
    -----------------------------
    begin                : November 2018
    copyright            : (C) 2018 by Nyall Dawson
    email                : nyall dot dawson at gmail dot com
 ***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include "qgsblockingnetworkrequest.h"
#include "moc_qgsblockingnetworkrequest.cpp"
#include "qgslogger.h"
#include "qgsapplication.h"
#include "qgsnetworkaccessmanager.h"
#include "qgsauthmanager.h"
#include "qgsmessagelog.h"
#include "qgsfeedback.h"
#include "qgsvariantutils.h"
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QMutex>
#include <QWaitCondition>
#include <QNetworkCacheMetaData>
#include <QAuthenticator>
#include <QBuffer>

QgsBlockingNetworkRequest::QgsBlockingNetworkRequest()
{
  connect( QgsNetworkAccessManager::instance(), qOverload< QNetworkReply * >( &QgsNetworkAccessManager::requestTimedOut ), this, &QgsBlockingNetworkRequest::requestTimedOut );
}

QgsBlockingNetworkRequest::~QgsBlockingNetworkRequest()
{
  abort();
}

void QgsBlockingNetworkRequest::requestTimedOut( QNetworkReply *reply )
{
  if ( reply == mReply )
    mTimedout = true;
}

QString QgsBlockingNetworkRequest::authCfg() const
{
  return mAuthCfg;
}

void QgsBlockingNetworkRequest::setAuthCfg( const QString &authCfg )
{
  mAuthCfg = authCfg;
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::get( QNetworkRequest &request, bool forceRefresh, QgsFeedback *feedback, RequestFlags requestFlags )
{
  return doRequest( Qgis::HttpMethod::Get, request, forceRefresh, feedback, requestFlags );
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::post( QNetworkRequest &request, const QByteArray &data, bool forceRefresh, QgsFeedback *feedback )
{
  QByteArray ldata( data );
  QBuffer buffer( &ldata );
  buffer.open( QIODevice::ReadOnly );
  return post( request, &buffer, forceRefresh, feedback );
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::post( QNetworkRequest &request, QIODevice *data, bool forceRefresh, QgsFeedback *feedback )
{
  mPayloadData = data;
  return doRequest( Qgis::HttpMethod::Post, request, forceRefresh, feedback );
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::head( QNetworkRequest &request, bool forceRefresh, QgsFeedback *feedback )
{
  return doRequest( Qgis::HttpMethod::Head, request, forceRefresh, feedback );
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::put( QNetworkRequest &request, const QByteArray &data, QgsFeedback *feedback )
{
  QByteArray ldata( data );
  QBuffer buffer( &ldata );
  buffer.open( QIODevice::ReadOnly );
  return put( request, &buffer, feedback );
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::put( QNetworkRequest &request, QIODevice *data, QgsFeedback *feedback )
{
  mPayloadData = data;
  return doRequest( Qgis::HttpMethod::Put, request, true, feedback );
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::deleteResource( QNetworkRequest &request, QgsFeedback *feedback )
{
  return doRequest( Qgis::HttpMethod::Delete, request, true, feedback );
}

void QgsBlockingNetworkRequest::sendRequestToNetworkAccessManager( const QNetworkRequest &request )
{
  switch ( mMethod )
  {
    case Qgis::HttpMethod::Get:
      mReply = QgsNetworkAccessManager::instance()->get( request );
      break;

    case Qgis::HttpMethod::Post:
      mReply = QgsNetworkAccessManager::instance()->post( request, mPayloadData );
      break;

    case Qgis::HttpMethod::Head:
      mReply = QgsNetworkAccessManager::instance()->head( request );
      break;

    case Qgis::HttpMethod::Put:
      mReply = QgsNetworkAccessManager::instance()->put( request, mPayloadData );
      break;

    case Qgis::HttpMethod::Delete:
      mReply = QgsNetworkAccessManager::instance()->deleteResource( request );
      break;
  };
}

QgsBlockingNetworkRequest::ErrorCode QgsBlockingNetworkRequest::doRequest( Qgis::HttpMethod method, QNetworkRequest &request, bool forceRefresh, QgsFeedback *feedback, RequestFlags requestFlags )
{
  mMethod = method;
  mFeedback = feedback;

  abort(); // cancel previous
  mIsAborted = false;
  mTimedout = false;
  mGotNonEmptyResponse = false;
  mRequestFlags = requestFlags;

  mErrorMessage.clear();
  mErrorCode = NoError;
  mForceRefresh = forceRefresh;
  mReplyContent.clear();

  if ( !mAuthCfg.isEmpty() &&  !QgsApplication::authManager()->updateNetworkRequest( request, mAuthCfg ) )
  {
    mErrorCode = NetworkError;
    mErrorMessage = errorMessageFailedAuth();
    QgsMessageLog::logMessage( mErrorMessage, tr( "Network" ) );
    return NetworkError;
  }

  QgsDebugMsgLevel( QStringLiteral( "Calling: %1" ).arg( request.url().toString() ), 2 );

  request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy );
  request.setAttribute( QNetworkRequest::CacheLoadControlAttribute, forceRefresh ? QNetworkRequest::AlwaysNetwork : QNetworkRequest::PreferCache );
  request.setAttribute( QNetworkRequest::CacheSaveControlAttribute, true );

  QWaitCondition authRequestBufferNotEmpty;
  QMutex waitConditionMutex;

  bool threadFinished = false;
  bool success = false;

  const bool requestMadeFromMainThread = QThread::currentThread() == QApplication::instance()->thread();

  if ( mFeedback )
    connect( mFeedback, &QgsFeedback::canceled, this, &QgsBlockingNetworkRequest::abort );

  const std::function<void()> downloaderFunction = [ this, request, &waitConditionMutex, &authRequestBufferNotEmpty, &threadFinished, &success, requestMadeFromMainThread ]()
  {
    // this function will always be run in worker threads -- either the blocking call is being made in a worker thread,
    // or the blocking call has been made from the main thread and we've fired up a new thread for this function
    Q_ASSERT( QThread::currentThread() != QgsApplication::instance()->thread() );

    QgsNetworkAccessManager::instance( Qt::DirectConnection );

    success = true;

    sendRequestToNetworkAccessManager( request );

    if ( mFeedback )
      connect( mFeedback, &QgsFeedback::canceled, mReply, &QNetworkReply::abort );

    if ( !mAuthCfg.isEmpty() && !QgsApplication::authManager()->updateNetworkReply( mReply, mAuthCfg ) )
    {
      mErrorCode = NetworkError;
      mErrorMessage = errorMessageFailedAuth();
      QgsMessageLog::logMessage( mErrorMessage, tr( "Network" ) );
      if ( requestMadeFromMainThread )
        authRequestBufferNotEmpty.wakeAll();
      success = false;
    }
    else
    {
      // We are able to use direct connection here, because we
      // * either run on the thread mReply lives in, so DirectConnection is standard and safe anyway
      // * or the owner thread of mReply is currently not doing anything because it's blocked in future.waitForFinished() (if it is the main thread)
      connect( mReply, &QNetworkReply::finished, this, &QgsBlockingNetworkRequest::replyFinished, Qt::DirectConnection );
      connect( mReply, &QNetworkReply::downloadProgress, this, &QgsBlockingNetworkRequest::replyProgress, Qt::DirectConnection );
      connect( mReply, &QNetworkReply::uploadProgress, this, &QgsBlockingNetworkRequest::replyProgress, Qt::DirectConnection );

      if ( request.hasRawHeader( "Range" ) )
        connect( mReply, &QNetworkReply::metaDataChanged, this, &QgsBlockingNetworkRequest::abortIfNotPartialContentReturned, Qt::DirectConnection );

      auto resumeMainThread = [&waitConditionMutex, &authRequestBufferNotEmpty ]()
      {
        // when this method is called we have "produced" a single authentication request -- so the buffer is now full
        // and it's time for the "consumer" (main thread) to do its part
        waitConditionMutex.lock();
        authRequestBufferNotEmpty.wakeAll();
        waitConditionMutex.unlock();

        // note that we don't need to handle waking this thread back up - that's done automatically by QgsNetworkAccessManager
      };

      QMetaObject::Connection authRequestConnection;
      QMetaObject::Connection proxyAuthenticationConnection;
#ifndef QT_NO_SSL
      QMetaObject::Connection sslErrorsConnection;
#endif

      if ( requestMadeFromMainThread )
      {
        authRequestConnection = connect( QgsNetworkAccessManager::instance(), &QgsNetworkAccessManager::authRequestOccurred, this, resumeMainThread, Qt::DirectConnection );
        proxyAuthenticationConnection = connect( QgsNetworkAccessManager::instance(), &QgsNetworkAccessManager::proxyAuthenticationRequired, this, resumeMainThread, Qt::DirectConnection );

#ifndef QT_NO_SSL
        sslErrorsConnection = connect( QgsNetworkAccessManager::instance(), &QgsNetworkAccessManager::sslErrorsOccurred, this, resumeMainThread, Qt::DirectConnection );
#endif
      }
      QEventLoop loop;
      // connecting to aboutToQuit avoids an on-going request to remain stalled
      // when QThreadPool::globalInstance()->waitForDone()
      // is called at process termination
      connect( qApp, &QCoreApplication::aboutToQuit, &loop, &QEventLoop::quit, Qt::DirectConnection );
      connect( this, &QgsBlockingNetworkRequest::finished, &loop, &QEventLoop::quit, Qt::DirectConnection );
      loop.exec();

      if ( requestMadeFromMainThread )
      {
        // event loop exited - need to disconnect as to not leave functor hanging to receive signals in future
        disconnect( authRequestConnection );
        disconnect( proxyAuthenticationConnection );
#ifndef QT_NO_SSL
        disconnect( sslErrorsConnection );
#endif
      }
    }

    if ( requestMadeFromMainThread )
    {
      waitConditionMutex.lock();
      threadFinished = true;
      authRequestBufferNotEmpty.wakeAll();
      waitConditionMutex.unlock();
    }
  };

  if ( requestMadeFromMainThread )
  {
    auto downloaderThread = std::make_unique<DownloaderThread>( downloaderFunction );
    downloaderThread->start();

    while ( true )
    {
      waitConditionMutex.lock();
      if ( threadFinished )
      {
        waitConditionMutex.unlock();
        break;
      }
      authRequestBufferNotEmpty.wait( &waitConditionMutex );

      // If the downloader thread wakes us (the main thread) up and is not yet finished
      // then it has "produced" an authentication request which we need to now "consume".
      // The processEvents() call gives the auth manager the chance to show a dialog and
      // once done with that, we can wake the downloaderThread again and continue the download.
      if ( !threadFinished )
      {
        waitConditionMutex.unlock();

        QgsApplication::processEvents();
        // we don't need to wake up the worker thread - it will automatically be woken when
        // the auth request has been dealt with by QgsNetworkAccessManager
      }
      else
      {
        waitConditionMutex.unlock();
      }
    }
    // wait for thread to gracefully exit
    downloaderThread->wait();
  }
  else
  {
    downloaderFunction();
  }
  return mErrorCode;
}

void QgsBlockingNetworkRequest::abort()
{
  mIsAborted = true;
  if ( mReply )
  {
    mReply->deleteLater();
    mReply = nullptr;
  }
}

void QgsBlockingNetworkRequest::replyProgress( qint64 bytesReceived, qint64 bytesTotal )
{
  QgsDebugMsgLevel( QStringLiteral( "%1 of %2 bytes downloaded." ).arg( bytesReceived ).arg( bytesTotal < 0 ? QStringLiteral( "unknown number of" ) : QString::number( bytesTotal ) ), 2 );

  if ( bytesReceived != 0 )
    mGotNonEmptyResponse = true;

  if ( !mIsAborted && mReply && ( !mFeedback || !mFeedback->isCanceled() ) )
  {
    if ( mReply->error() == QNetworkReply::NoError )
    {
      const QVariant redirect = mReply->attribute( QNetworkRequest::RedirectionTargetAttribute );
      if ( !QgsVariantUtils::isNull( redirect ) )
      {
        // We don't want to emit downloadProgress() for a redirect
        return;
      }
    }
  }

  if ( mMethod == Qgis::HttpMethod::Put || mMethod == Qgis::HttpMethod::Post )
    emit uploadProgress( bytesReceived, bytesTotal );
  else
    emit downloadProgress( bytesReceived, bytesTotal );
}

void QgsBlockingNetworkRequest::replyFinished()
{
  if ( !mIsAborted && mReply )
  {

    if ( mReply->error() == QNetworkReply::NoError && ( !mFeedback || !mFeedback->isCanceled() ) )
    {
      QgsDebugMsgLevel( QStringLiteral( "reply OK" ), 2 );
      const QVariant redirect = mReply->attribute( QNetworkRequest::RedirectionTargetAttribute );
      if ( !QgsVariantUtils::isNull( redirect ) )
      {
        QgsDebugMsgLevel( QStringLiteral( "Request redirected." ), 2 );

        const QUrl &toUrl = redirect.toUrl();
        mReply->request();
        if ( toUrl == mReply->url() )
        {
          mErrorMessage = tr( "Redirect loop detected: %1" ).arg( toUrl.toString() );
          QgsMessageLog::logMessage( mErrorMessage, tr( "Network" ) );
          mReplyContent.clear();
        }
        else
        {
          QNetworkRequest request( toUrl );

          if ( !mAuthCfg.isEmpty() && !QgsApplication::authManager()->updateNetworkRequest( request, mAuthCfg ) )
          {
            mReplyContent.clear();
            mErrorMessage = errorMessageFailedAuth();
            mErrorCode = NetworkError;
            QgsMessageLog::logMessage( mErrorMessage, tr( "Network" ) );
            emit finished();
            Q_NOWARN_DEPRECATED_PUSH
            emit downloadFinished();
            Q_NOWARN_DEPRECATED_POP
            return;
          }

          request.setAttribute( QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy );
          request.setAttribute( QNetworkRequest::CacheLoadControlAttribute, mForceRefresh ? QNetworkRequest::AlwaysNetwork : QNetworkRequest::PreferCache );
          request.setAttribute( QNetworkRequest::CacheSaveControlAttribute, true );

          // if that was a range request, use the same range for the redirected request
          if ( mReply->request().hasRawHeader( "Range" ) )
            request.setRawHeader( "Range", mReply->request().rawHeader( "Range" ) );

          mReply->deleteLater();
          mReply = nullptr;

          QgsDebugMsgLevel( QStringLiteral( "redirected: %1 forceRefresh=%2" ).arg( redirect.toString() ).arg( mForceRefresh ), 2 );

          sendRequestToNetworkAccessManager( request );

          if ( mFeedback )
            connect( mFeedback, &QgsFeedback::canceled, mReply, &QNetworkReply::abort );

          if ( !mAuthCfg.isEmpty() && !QgsApplication::authManager()->updateNetworkReply( mReply, mAuthCfg ) )
          {
            mReplyContent.clear();
            mErrorMessage = errorMessageFailedAuth();
            mErrorCode = NetworkError;
            QgsMessageLog::logMessage( mErrorMessage, tr( "Network" ) );
            emit finished();
            Q_NOWARN_DEPRECATED_PUSH
            emit downloadFinished();
            Q_NOWARN_DEPRECATED_POP
            return;
          }

          connect( mReply, &QNetworkReply::finished, this, &QgsBlockingNetworkRequest::replyFinished, Qt::DirectConnection );
          connect( mReply, &QNetworkReply::downloadProgress, this, &QgsBlockingNetworkRequest::replyProgress, Qt::DirectConnection );
          connect( mReply, &QNetworkReply::uploadProgress, this, &QgsBlockingNetworkRequest::replyProgress, Qt::DirectConnection );

          if ( request.hasRawHeader( "Range" ) )
            connect( mReply, &QNetworkReply::metaDataChanged, this, &QgsBlockingNetworkRequest::abortIfNotPartialContentReturned, Qt::DirectConnection );

          return;
        }
      }
      else
      {
        const QgsNetworkAccessManager *nam = QgsNetworkAccessManager::instance();

        if ( nam->cache() )
        {
          QNetworkCacheMetaData cmd = nam->cache()->metaData( mReply->request().url() );

          QNetworkCacheMetaData::RawHeaderList hl;
          const auto constRawHeaders = cmd.rawHeaders();
          for ( const QNetworkCacheMetaData::RawHeader &h : constRawHeaders )
          {
            if ( h.first != "Cache-Control" )
              hl.append( h );
          }
          cmd.setRawHeaders( hl );

          QgsDebugMsgLevel( QStringLiteral( "expirationDate:%1" ).arg( cmd.expirationDate().toString() ), 2 );
          if ( cmd.expirationDate().isNull() )
          {
            cmd.setExpirationDate( QDateTime::currentDateTime().addSecs( mExpirationSec ) );
          }

          nam->cache()->updateMetaData( cmd );
        }
        else
        {
          QgsDebugMsgLevel( QStringLiteral( "No cache!" ), 2 );
        }

#ifdef QGISDEBUG
        const bool fromCache = mReply->attribute( QNetworkRequest::SourceIsFromCacheAttribute ).toBool();
        QgsDebugMsgLevel( QStringLiteral( "Reply was cached: %1" ).arg( fromCache ), 2 );
#endif

        mReplyContent = QgsNetworkReplyContent( mReply );
        const QByteArray content = mReply->readAll();
        if ( !( mRequestFlags & RequestFlag::EmptyResponseIsValid ) && content.isEmpty() && !mGotNonEmptyResponse && mMethod == Qgis::HttpMethod::Get )
        {
          mErrorMessage = tr( "empty response: %1" ).arg( mReply->errorString() );
          mErrorCode = ServerExceptionError;
          QgsMessageLog::logMessage( mErrorMessage, tr( "Network" ) );
        }
        mReplyContent.setContent( content );
      }
    }
    else
    {
      if ( mReply->error() != QNetworkReply::OperationCanceledError )
      {
        mErrorMessage = mReply->errorString();
        mErrorCode = ServerExceptionError;
        QgsMessageLog::logMessage( mErrorMessage, tr( "Network" ) );
      }
      mReplyContent = QgsNetworkReplyContent( mReply );
      mReplyContent.setContent( mReply->readAll() );
    }
  }
  if ( mTimedout )
    mErrorCode = TimeoutError;

  if ( mReply )
  {
    mReply->deleteLater();
    mReply = nullptr;
  }

  emit finished();
  Q_NOWARN_DEPRECATED_PUSH
  emit downloadFinished();
  Q_NOWARN_DEPRECATED_POP
}

QString QgsBlockingNetworkRequest::errorMessageFailedAuth()
{
  return tr( "network request update failed for authentication config" );
}

void QgsBlockingNetworkRequest::abortIfNotPartialContentReturned()
{
  if ( mReply && mReply->attribute( QNetworkRequest::HttpStatusCodeAttribute ).toInt() == 200 )
  {
    // We're expecting a 206 - Partial Content but the server returned 200
    // It seems it does not support range requests and is returning the whole file!
    mReply->abort();
    mErrorMessage = tr( "The server does not support range requests" );
    mErrorCode = ServerExceptionError;
  }
}
