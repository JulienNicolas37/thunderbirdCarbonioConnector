/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioMessageChannel.h"

#include "CarbonioIncomingServer.h"
#include "CarbonioListeners.h"
#include "ICarbonioClient.h"
#include "nsContentSecurityManager.h"
#include "nsIInputStream.h"
#include "nsIMsgFolder.h"
#include "nsIMsgIncomingServer.h"
#include "nsIMsgMessageService.h"
#include "nsIMsgPluggableStore.h"
#include "nsIOutputStream.h"
#include "nsIStreamListener.h"
#include "nsMimeTypes.h"
#include "nsMsgMessageFlags.h"
#include "nsMsgUtils.h"
#include "nsNetUtil.h"
#include "OfflineStorage.h"

// Local property recording the Carbonio id of a message, mirrors
// `kCarbonioIdProperty` for folders (see `CarbonioIncomingServer.h`).
constexpr auto kCarbonioMsgIdProperty = "carbonioMsgId";

NS_IMPL_ISUPPORTS_INHERITED(CarbonioMessageChannel, nsHashPropertyBag,
                            nsIMailChannel, nsIChannel, nsIRequest)

CarbonioMessageChannel::CarbonioMessageChannel(nsIURI* uri)
    : mURI(uri),
      mContentDisposition(nsIChannel::DISPOSITION_INLINE),
      mContentLength(-1),
      mLoadFlags(nsIRequest::LOAD_NORMAL),
      mPending(true),
      mStatus(NS_OK) {
  mContentType.AssignLiteral(MESSAGE_RFC822);
}

CarbonioMessageChannel::~CarbonioMessageChannel() = default;

NS_IMETHODIMP CarbonioMessageChannel::GetName(nsACString& aName) {
  if (mURI) {
    return mURI->GetSpec(aName);
  }
  aName.Truncate();
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::IsPending(bool* aPending) {
  if (mReadRequest) {
    return mReadRequest->IsPending(aPending);
  }
  *aPending = mPending;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetStatus(nsresult* aStatus) {
  if (mReadRequest && NS_SUCCEEDED(mStatus)) {
    return mReadRequest->GetStatus(aStatus);
  }
  *aStatus = mStatus;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::Cancel(nsresult aStatus) {
  if (mReadRequest) {
    return mReadRequest->Cancel(aStatus);
  }
  NS_WARNING("Cannot cancel a Carbonio message channel while downloading");
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP CarbonioMessageChannel::Suspend(void) {
  if (mReadRequest) {
    return mReadRequest->Suspend();
  }
  NS_WARNING("Cannot suspend a Carbonio message channel while downloading");
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP CarbonioMessageChannel::Resume(void) {
  if (mReadRequest) {
    return mReadRequest->Resume();
  }
  NS_WARNING("Cannot resume a Carbonio message channel while downloading");
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP CarbonioMessageChannel::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  if (mReadRequest) {
    return mReadRequest->GetLoadGroup(aLoadGroup);
  }
  NS_IF_ADDREF(*aLoadGroup = mLoadGroup);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetLoadGroup(nsILoadGroup* aLoadGroup) {
  if (mReadRequest) {
    return mReadRequest->SetLoadGroup(aLoadGroup);
  }
  mLoadGroup = aLoadGroup;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetLoadFlags(nsLoadFlags* aLoadFlags) {
  if (mReadRequest) {
    return mReadRequest->GetLoadFlags(aLoadFlags);
  }
  *aLoadFlags = mLoadFlags;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetLoadFlags(nsLoadFlags aLoadFlags) {
  if (mReadRequest) {
    return mReadRequest->SetLoadFlags(aLoadFlags);
  }
  mLoadFlags = aLoadFlags;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetTRRMode(nsIRequest::TRRMode* mode) {
  return GetTRRModeImpl(mode);
}

NS_IMETHODIMP CarbonioMessageChannel::SetTRRMode(nsIRequest::TRRMode mode) {
  return SetTRRModeImpl(mode);
}

NS_IMETHODIMP CarbonioMessageChannel::CancelWithReason(
    nsresult aStatus, const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP CarbonioMessageChannel::GetCanceledReason(
    nsACString& aCanceledReason) {
  return GetCanceledReasonImpl(aCanceledReason);
}

NS_IMETHODIMP CarbonioMessageChannel::SetCanceledReason(
    const nsACString& aCanceledReason) {
  return SetCanceledReasonImpl(aCanceledReason);
}

NS_IMETHODIMP CarbonioMessageChannel::GetOriginalURI(nsIURI** aOriginalURI) {
  NS_IF_ADDREF(*aOriginalURI = mURI);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetOriginalURI(nsIURI* aOriginalURI) {
  // There's no meaningful "original URI" for these requests.
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetURI(nsIURI** aURI) {
  NS_IF_ADDREF(*aURI = mURI);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetOwner(nsISupports** aOwner) {
  NS_IF_ADDREF(*aOwner = mOwner);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetOwner(nsISupports* aOwner) {
  mOwner = aOwner;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetNotificationCallbacks(
    nsIInterfaceRequestor** aNotificationCallbacks) {
  NS_IF_ADDREF(*aNotificationCallbacks = mNotificationCallbacks);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetNotificationCallbacks(
    nsIInterfaceRequestor* aNotificationCallbacks) {
  mNotificationCallbacks = aNotificationCallbacks;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetSecurityInfo(
    nsITransportSecurityInfo** aSecurityInfo) {
  // Security info does not make sense here since we're only pulling
  // messages from storage.
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP CarbonioMessageChannel::GetContentType(
    nsACString& aContentType) {
  aContentType.Assign(mContentType);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetContentType(
    const nsACString& aContentType) {
  nsresult rv =
      NS_ParseResponseContentType(aContentType, mContentType, mCharset);

  if (NS_FAILED(rv) || mContentType.IsEmpty()) {
    mContentType.AssignLiteral(MESSAGE_RFC822);
  }
  if (NS_FAILED(rv) || mCharset.IsEmpty()) {
    mCharset.AssignLiteral("UTF-8");
  }
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetContentCharset(
    nsACString& aContentCharset) {
  aContentCharset.Assign(mCharset);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetContentCharset(
    const nsACString& aContentCharset) {
  mCharset.Assign(aContentCharset);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetContentLength(
    int64_t* aContentLength) {
  *aContentLength = mContentLength;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetContentLength(
    int64_t aContentLength) {
  mContentLength = aContentLength;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::Open(nsIInputStream** _retval) {
  return NS_ImplementChannelOpen(this, _retval);
}

NS_IMETHODIMP CarbonioMessageChannel::AsyncOpen(nsIStreamListener* aListener) {
  mPending = false;

  nsCOMPtr<nsIStreamListener> listener = aListener;
  nsresult rv =
      nsContentSecurityManager::doContentSecurityCheck(this, listener);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIMsgMessageService> msgService =
      do_GetService("@mozilla.org/messenger/messageservice;1?type=carbonio",
                    &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString spec;
  rv = mURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = msgService->MessageURIToMsgHdr(spec, getter_AddRefs(mHdr));
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t flags = 0;
  rv = mHdr->GetFlags(&flags);
  NS_ENSURE_SUCCESS(rv, rv);

  if (flags & nsMsgMessageFlags::Offline) {
    // Already cached locally - stream it directly.
    return StartMessageReadFromStore(aListener);
  }

  // Not cached yet - download it first, then stream it.
  return DownloadMessageAndReadFromStore(aListener);
}

NS_IMETHODIMP CarbonioMessageChannel::GetCanceled(bool* aCanceled) {
  nsCString canceledReason;
  nsresult rv = mReadRequest->GetCanceledReason(canceledReason);
  NS_ENSURE_SUCCESS(rv, rv);
  *aCanceled = !canceledReason.IsEmpty();
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetContentDisposition(
    uint32_t* aContentDisposition) {
  *aContentDisposition = mContentDisposition;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetContentDisposition(
    uint32_t aContentDisposition) {
  mContentDisposition = aContentDisposition;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetContentDispositionFilename(
    nsAString& aContentDispositionFilename) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP CarbonioMessageChannel::SetContentDispositionFilename(
    const nsAString& aContentDispositionFilename) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP CarbonioMessageChannel::GetContentDispositionHeader(
    nsACString& aContentDispositionHeader) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP CarbonioMessageChannel::GetLoadInfo(nsILoadInfo** aLoadInfo) {
  NS_IF_ADDREF(*aLoadInfo = mLoadInfo);
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetLoadInfo(nsILoadInfo* aLoadInfo) {
  mLoadInfo = aLoadInfo;
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::GetIsDocument(bool* aIsDocument) {
  return NS_GetIsDocumentChannel(this, aIsDocument);
}

NS_IMETHODIMP CarbonioMessageChannel::GetParentProcessChannelHandle(
    mozilla::dom::ParentProcessChannelHandle** aValue) {
  *aValue = do_AddRef(mParentProcessChannelHandle).take();
  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageChannel::SetParentProcessChannelHandle(
    mozilla::dom::ParentProcessChannelHandle* aValue) {
  if (XRE_IsParentProcess()) {
    MOZ_ASSERT_UNREACHABLE(
        "SetParentProcessChannelHandle in the parent process would leak");
    return NS_ERROR_NOT_AVAILABLE;
  }
  mParentProcessChannelHandle = aValue;
  return NS_OK;
}

nsresult CarbonioMessageChannel::StartMessageReadFromStore(
    nsIStreamListener* streamListener) {
  nsresult rv = AsyncReadMessageFromStore(
      mHdr, streamListener, /* convertData */ false, this,
      getter_AddRefs(mReadRequest));
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_TRY(mReadRequest->SetLoadFlags(mLoadFlags));
  MOZ_TRY(mReadRequest->SetLoadGroup(mLoadGroup));

  return NS_OK;
}

nsresult CarbonioMessageChannel::DownloadMessageAndReadFromStore(
    nsIStreamListener* streamListener) {
  nsCOMPtr<nsIMsgFolder> folder;
  MOZ_TRY(mHdr->GetFolder(getter_AddRefs(folder)));

  nsCOMPtr<nsIMsgIncomingServer> server;
  MOZ_TRY(folder->GetServer(getter_AddRefs(server)));

  nsCOMPtr<ICarbonioIncomingServer> carbonioServer =
      do_QueryInterface(server);
  NS_ENSURE_TRUE(carbonioServer, NS_ERROR_UNEXPECTED);

  nsCOMPtr<ICarbonioClient> client;
  MOZ_TRY(carbonioServer->GetProtocolClient(getter_AddRefs(client)));

  nsCString carbonioMsgId;
  MOZ_TRY(mHdr->GetStringProperty(kCarbonioMsgIdProperty, carbonioMsgId));

  nsCOMPtr<nsIMsgPluggableStore> msgStore;
  MOZ_TRY(folder->GetMsgStore(getter_AddRefs(msgStore)));

  nsCOMPtr<nsIOutputStream> outputStream;
  MOZ_TRY(msgStore->GetNewMsgOutputStream(folder, getter_AddRefs(outputStream)));

  RefPtr<CarbonioMessageChannel> self(this);
  nsCOMPtr<nsIMsgFolder> folderRef(folder);
  nsCOMPtr<nsIMsgPluggableStore> msgStoreRef(msgStore);
  nsCOMPtr<nsIOutputStream> outputStreamRef(outputStream);
  nsCOMPtr<nsIStreamListener> consumer(streamListener);
  nsCOMPtr<nsIMsgDBHdr> hdrRef(mHdr);

  auto onFetchStart = []() { return NS_OK; };

  // Copies the (fully in-memory, see `create_string_input_stream` on the
  // Rust side) fetched content straight into the store's output stream.
  auto onFetchedDataAvailable = [outputStreamRef](nsIInputStream* input,
                                                   uint64_t* bytesWritten) {
    *bytesWritten = 0;
    nsresult rv;
    char buffer[4096];
    for (;;) {
      uint32_t readCount = 0;
      rv = input->Read(buffer, sizeof(buffer), &readCount);
      if (NS_FAILED(rv) || readCount == 0) break;

      uint32_t writeCount = 0;
      rv = outputStreamRef->Write(buffer, readCount, &writeCount);
      if (NS_FAILED(rv)) break;

      *bytesWritten += writeCount;
    }
    return rv;
  };

  auto onFetchStop = [self, folderRef, msgStoreRef, outputStreamRef, consumer,
                      hdrRef](nsresult status) {
    if (NS_FAILED(status)) {
      msgStoreRef->DiscardNewMessage(folderRef, outputStreamRef);
      consumer->OnStartRequest(self);
      consumer->OnStopRequest(self, status);
      return NS_OK;
    }

    nsCString storeToken;
    nsresult rv = msgStoreRef->FinishNewMessage(folderRef, outputStreamRef,
                                                storeToken);
    if (NS_FAILED(rv)) {
      consumer->OnStartRequest(self);
      consumer->OnStopRequest(self, rv);
      return NS_OK;
    }

    hdrRef->SetStoreToken(storeToken);
    uint32_t unused;
    hdrRef->OrFlags(nsMsgMessageFlags::Offline, &unused);

    rv = self->StartMessageReadFromStore(consumer);
    if (NS_FAILED(rv)) {
      consumer->OnStartRequest(self);
      consumer->OnStopRequest(self, rv);
    }
    return NS_OK;
  };

  RefPtr<CarbonioMessageFetchListener> listener = new CarbonioMessageFetchListener(
      onFetchStart, onFetchedDataAvailable, onFetchStop);

  return client->GetMessage(listener, carbonioMsgId);
}
