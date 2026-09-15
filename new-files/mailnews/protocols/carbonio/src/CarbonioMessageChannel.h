/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOMESSAGECHANNEL_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOMESSAGECHANNEL_H_

#include "nsHashPropertyBag.h"
#include "nsIChannel.h"
#include "nsIMailChannel.h"
#include "nsIMsgHdr.h"
#include "nsMailChannel.h"
#include "mozilla/dom/ParentProcessChannelHandle.h"

/**
 * A channel for streaming the content of a Carbonio message out of the local
 * message store.
 *
 * The message URI is expected in the form:
 * x-moz-carbonio://{user}@{server}/{Path/To/Folder}/{MessageKey}
 *
 * Modeled after EWS's `ExchangeMessageChannel`, trimmed for phase 1: no
 * attachment/part handling (`part=` query parameter), no on-the-fly content
 * conversion beyond what's needed for basic message display. Kept: reading
 * from the local store via the shared, protocol-agnostic
 * `AsyncReadMessageFromStore()` helper (see `OfflineStorage.h`) - the same
 * one EWS itself uses for this exact case - and downloading-then-caching a
 * message that isn't offline yet, so this class can be extended later
 * (attachment support, etc.) without redoing the caching logic.
 */
class CarbonioMessageChannel : public nsMailChannel,
                               public nsIChannel,
                               public nsHashPropertyBag {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  NS_DECL_NSIREQUEST
  NS_DECL_NSICHANNEL

  explicit CarbonioMessageChannel(nsIURI* uri);

 protected:
  virtual ~CarbonioMessageChannel();

 private:
  /**
   * Starts asynchronously reading the message from the local store, once
   * it's confirmed (or made) to be present there.
   */
  nsresult StartMessageReadFromStore(nsIStreamListener* streamListener);

  /**
   * Downloads the message via the Carbonio client, writes it to the local
   * message store, tags the header with the resulting store token, then
   * starts reading it back via `StartMessageReadFromStore()`. Called when
   * `AsyncOpen()` finds the message isn't cached locally yet.
   */
  nsresult DownloadMessageAndReadFromStore(nsIStreamListener* streamListener);

  // The URI for the message which content we want to stream.
  nsCOMPtr<nsIURI> mURI;

  // The database entry for the message which content we want to stream.
  nsCOMPtr<nsIMsgDBHdr> mHdr;

  // The `nsIRequest` representing the operation of reading the message from
  // the local store, once started (see `StartMessageReadFromStore()`). Most
  // `nsIRequest` calls received by `CarbonioMessageChannel` are forwarded to
  // it once it exists.
  nsCOMPtr<nsIRequest> mReadRequest;

  // These attributes exist to allow a basic implementation of most
  // `nsIChannel` methods. Mirrors `ExchangeMessageChannel`'s approach.
  nsAutoCStringN<255> mContentType;
  nsAutoCString mCharset;
  uint32_t mContentDisposition;
  int64_t mContentLength;
  RefPtr<nsILoadGroup> mLoadGroup;
  nsLoadFlags mLoadFlags;
  nsCOMPtr<nsIInterfaceRequestor> mNotificationCallbacks;
  nsCOMPtr<nsISupports> mOwner;
  nsCOMPtr<nsILoadInfo> mLoadInfo;
  bool mPending;
  nsresult mStatus;
  RefPtr<mozilla::dom::ParentProcessChannelHandle> mParentProcessChannelHandle;
};

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOMESSAGECHANNEL_H_
