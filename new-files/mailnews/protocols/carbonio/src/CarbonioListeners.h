/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOLISTENERS_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOLISTENERS_H_

#include <utility>

#include "ICarbonioClient.h"

/**
 * Adapts a set of lambda callbacks to `ICarbonioFolderListener`, so callers
 * (see `CarbonioIncomingServer::SyncFolderList`) can express sync handling
 * as inline closures rather than writing a dedicated listener class per
 * call site. Modeled after EWS's `ExchangeFolderSyncListener`, with
 * `onFolderCreated`/`onFolderUpdated` merged into a single
 * `onFolderUpserted` callback to match `ICarbonioFolderListener` (see that
 * interface's doc comment for why).
 */
class CarbonioFolderSyncListener : public ICarbonioFolderListener {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_ICARBONIOFOLDERLISTENER

  CarbonioFolderSyncListener(
      std::function<nsresult(const nsACString&)> onNewRootFolder,
      std::function<nsresult(const nsACString&, const nsACString&,
                             const nsACString&, bool)>
          onFolderUpserted,
      std::function<nsresult(const nsACString&)> onFolderDeleted,
      std::function<nsresult(const nsACString&)> onSyncStateTokenChanged,
      std::function<nsresult()> onSuccess,
      std::function<nsresult(nsresult)> onError)
      : mOnNewRootFolder(std::move(onNewRootFolder)),
        mOnFolderUpserted(std::move(onFolderUpserted)),
        mOnFolderDeleted(std::move(onFolderDeleted)),
        mOnSyncStateTokenChanged(std::move(onSyncStateTokenChanged)),
        mOnSuccess(std::move(onSuccess)),
        mOnError(std::move(onError)) {}

 protected:
  virtual ~CarbonioFolderSyncListener() = default;

 private:
  std::function<nsresult(const nsACString&)> mOnNewRootFolder;
  std::function<nsresult(const nsACString&, const nsACString&,
                         const nsACString&, bool)>
      mOnFolderUpserted;
  std::function<nsresult(const nsACString&)> mOnFolderDeleted;
  std::function<nsresult(const nsACString&)> mOnSyncStateTokenChanged;
  std::function<nsresult()> mOnSuccess;
  std::function<nsresult(nsresult)> mOnError;
};

/**
 * A listener for fetching the content of a single Carbonio message.
 *
 * Modeled after EWS's `ExchangeMessageFetchListener`: `onFetchedDataAvailable`
 * reports the number of bytes read from the input stream via its last
 * out parameter, and `onFetchStop` takes the total byte count read.
 */
class CarbonioMessageFetchListener : public ICarbonioMessageFetchListener {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_ICARBONIOMESSAGEFETCHLISTENER

  CarbonioMessageFetchListener(
      std::function<nsresult()> onFetchStart,
      std::function<nsresult(nsIInputStream*, uint64_t*)>
          onFetchedDataAvailable,
      std::function<nsresult(nsresult, uint64_t)> onFetchStop)
      : mOnFetchStart(std::move(onFetchStart)),
        mOnFetchedDataAvailable(std::move(onFetchedDataAvailable)),
        mOnFetchStop(std::move(onFetchStop)),
        mBytesRead(0) {}

 protected:
  virtual ~CarbonioMessageFetchListener() = default;

 private:
  std::function<nsresult()> mOnFetchStart;
  std::function<nsresult(nsIInputStream*, uint64_t*)> mOnFetchedDataAvailable;
  std::function<nsresult(nsresult, uint64_t)> mOnFetchStop;
  uint64_t mBytesRead;
};

/**
 * Adapts a pair of lambda callbacks to `ICarbonioSimpleOperationListener`.
 */
class CarbonioSimpleListener : public ICarbonioSimpleOperationListener {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_ICARBONIOSIMPLEOPERATIONLISTENER

  CarbonioSimpleListener(std::function<nsresult()> onSuccess,
                         std::function<nsresult(nsresult)> onFailure)
      : mOnSuccess(std::move(onSuccess)), mOnFailure(std::move(onFailure)) {}

 protected:
  virtual ~CarbonioSimpleListener() = default;

 private:
  std::function<nsresult()> mOnSuccess;
  std::function<nsresult(nsresult)> mOnFailure;
};

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOLISTENERS_H_
