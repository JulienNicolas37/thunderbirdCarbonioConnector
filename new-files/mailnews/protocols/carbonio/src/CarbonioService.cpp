/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioService.h"

#include <unistd.h>

#include "mozilla/Components.h"
#include "nsContentUtils.h"
#include "nsDocShellLoadState.h"
#include "nsIChannel.h"
#include "nsIMsgFolder.h"
#include "nsIMsgHdr.h"
#include "nsIURIMutator.h"
#include "nsIWebNavigation.h"
#include "nsMsgUtils.h"
#include "nsNetUtil.h"

NS_IMPL_ISUPPORTS(CarbonioService, nsIMsgMessageService)

CarbonioService::CarbonioService() = default;
CarbonioService::~CarbonioService() = default;

NS_IMETHODIMP CarbonioService::CopyMessage(const nsACString& aSrcURI,
                                           nsIStreamListener* aCopyListener,
                                           bool aMoveMessage,
                                           nsIUrlListener* aUrlListener,
                                           nsIMsgWindow* aMsgWindow) {
  NS_WARNING("CarbonioService::CopyMessage: not implemented in phase 1");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP CarbonioService::CopyMessages(
    const nsTArray<nsMsgKey>& aKeys, nsIMsgFolder* srcFolder,
    nsIStreamListener* aCopyListener, bool aMoveMessage,
    nsIUrlListener* aUrlListener, nsIMsgWindow* aMsgWindow, nsIURI** _retval) {
  NS_WARNING("CarbonioService::CopyMessages: not implemented in phase 1");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP CarbonioService::LoadMessage(const nsACString& aMessageURI,
                                           nsIDocShell* aDisplayConsumer,
                                           nsIMsgWindow* aMsgWindow,
                                           nsIUrlListener* aUrlListener,
                                           bool aAutodetectCharset) {
  // TEMPORARY DIAGNOSTIC
  static int sCallCount = 0;
  fprintf(stderr,
          "[carbonio-debug] LoadMessage call #%d, pid=%d, uri=%s, aUrlListener=%p\n",
          ++sCallCount, getpid(), nsCString(aMessageURI).get(),
          static_cast<void*>(aUrlListener));
  fflush(stderr);

  NS_ENSURE_ARG_POINTER(aDisplayConsumer);

  nsCOMPtr<nsIURI> channelURI;
  MOZ_TRY(GetUrlForUri(aMessageURI, aMsgWindow, getter_AddRefs(channelURI)));

  RefPtr<nsDocShellLoadState> loadState = new nsDocShellLoadState(channelURI);
  loadState->SetLoadFlags(nsIWebNavigation::LOAD_FLAGS_NONE);
  loadState->SetFirstParty(false);
  loadState->SetTriggeringPrincipal(nsContentUtils::GetSystemPrincipal());
  return aDisplayConsumer->LoadURI(loadState, false);
}

NS_IMETHODIMP CarbonioService::SaveMessageToDisk(const nsACString& aMessageURI,
                                                 nsIFile* aFile,
                                                 nsIUrlListener* aUrlListener,
                                                 bool canonicalLineEnding,
                                                 nsIMsgWindow* aMsgWindow) {
  NS_WARNING("CarbonioService::SaveMessageToDisk: not implemented in phase 1");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP CarbonioService::GetUrlForUri(const nsACString& aMessageURI,
                                            nsIMsgWindow* aMsgWindow,
                                            nsIURI** _retval) {
  nsCOMPtr<nsIURI> messageURI;
  MOZ_TRY(NS_NewURI(getter_AddRefs(messageURI), aMessageURI));

  nsAutoCString scheme;
  MOZ_TRY(messageURI->GetScheme(scheme));

  // If the URI is already a channel ("x-moz-carbonio") URI, forward it as-is.
  if (scheme.EqualsLiteral("x-moz-carbonio")) {
    messageURI.forget(_retval);
    return NS_OK;
  }

  // Otherwise, expect a "carbonio-message" URI, with the path looking like
  // /Path/To/Folder#MessageKey. Move the key into the path (rather than the
  // ref) for the channel URI, so the docShell doesn't consider every message
  // in a folder to be "the same document" (only the ref would otherwise
  // differ between them).
  nsAutoCString ref;
  nsresult rv = messageURI->GetRef(ref);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString path;
  rv = messageURI->GetFilePath(path);
  NS_ENSURE_SUCCESS(rv, rv);
  path.Append("/");
  path.Append(ref);

  nsCString query;
  rv = messageURI->GetQuery(query);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!scheme.EqualsLiteral("carbonio-message")) {
    NS_WARNING("CarbonioService::GetUrlForUri: unrecognized message URI scheme");
    return NS_ERROR_UNEXPECTED;
  }

  return NS_MutateURI(messageURI)
      .SetScheme("x-moz-carbonio"_ns)
      .SetPathQueryRef(path)
      .SetQuery(query)
      .Finalize(_retval);
}

NS_IMETHODIMP CarbonioService::Search(nsIMsgSearchSession* aSearchSession,
                                      nsIMsgWindow* aMsgWindow,
                                      nsIMsgFolder* aMsgFolder,
                                      const nsACString& aSearchUri) {
  NS_WARNING("CarbonioService::Search: not implemented in phase 1");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP CarbonioService::StreamMessage(
    const nsACString& aMessageURI, nsIStreamListener* aStreamListener,
    nsIMsgWindow* aMsgWindow, nsIUrlListener* aUrlListener, bool aConvertData,
    const nsACString& aAdditionalHeader, bool aLocalOnly, nsIURI** _retval) {
  NS_ENSURE_ARG_POINTER(aStreamListener);

  nsCOMPtr<nsIURI> channelURI;
  MOZ_TRY(GetUrlForUri(aMessageURI, aMsgWindow, getter_AddRefs(channelURI)));

  NS_IF_ADDREF(*_retval = channelURI);

  return FetchMessage(channelURI, aStreamListener);
}

NS_IMETHODIMP CarbonioService::StreamHeaders(const nsACString& aMessageURI,
                                             nsIStreamListener* aConsumer,
                                             nsIUrlListener* aUrlListener,
                                             bool aLocalOnly,
                                             nsIURI** _retval) {
  // Confirmed empirically (comparison against EWS's own working call
  // pattern for opening a message): the reading pane calls this in
  // addition to loadMessage/streamMessage, and previously getting an
  // immediate synchronous failure back from it seemingly caused the caller
  // to consider the whole open attempt failed and retry from scratch -
  // matching the infinite retry loop observed before this fix. Phase 1
  // doesn't distinguish "headers only" from the full message (our store
  // already has the whole thing cached by this point either way), so this
  // just delegates to the same fetch path as StreamMessage.
  NS_ENSURE_ARG_POINTER(aConsumer);

  nsCOMPtr<nsIURI> channelURI;
  MOZ_TRY(GetUrlForUri(aMessageURI, nullptr, getter_AddRefs(channelURI)));

  NS_IF_ADDREF(*_retval = channelURI);

  return FetchMessage(channelURI, aConsumer);
}

NS_IMETHODIMP CarbonioService::IsMsgInMemCache(nsIURI* aUrl,
                                               nsIMsgFolder* aFolder,
                                               bool* _retval) {
  // Phase 1 doesn't keep a separate memory cache beyond the local offline
  // store, so there's nothing to report here.
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = false;
  return NS_OK;
}

NS_IMETHODIMP CarbonioService::MessageURIToMsgHdr(const nsACString& uri,
                                                  nsIMsgDBHdr** _retval) {
  RefPtr<nsIURI> uriObj;
  nsresult rv = NS_NewURI(getter_AddRefs(uriObj), uri);
  NS_ENSURE_SUCCESS(rv, rv);

  return MsgHdrFromUri(uriObj, _retval);
}

nsresult CarbonioService::MsgKeyStringFromMessageURI(nsIURI* uri,
                                                     nsACString& msgKey) {
  // Expected form:
  // `carbonio-message://{username}@{host}/{folder_path}#{msg_key}`.
  nsresult rv = uri->GetRef(msgKey);
  NS_ENSURE_SUCCESS(rv, rv);

  if (msgKey.IsEmpty()) {
    NS_ERROR("message URI has no message key ref");
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

nsresult CarbonioService::MsgKeyStringFromChannelURI(
    nsIURI* uri, nsACString& msgKey, nsACString& folderURIPath) {
  nsresult rv = uri->GetFilePath(folderURIPath);
  NS_ENSURE_SUCCESS(rv, rv);

  // Avoid a trailing slash producing an empty message key.
  folderURIPath.Trim("/", false, true);

  for (const auto& word : folderURIPath.Split('/')) {
    msgKey.Assign(word);
  }

  auto keyStartIndex = folderURIPath.Length() - msgKey.Length() - 1;
  auto keyLengthInURI = msgKey.Length() + 1;
  folderURIPath.Cut(keyStartIndex, keyLengthInURI);

  return NS_OK;
}

nsresult CarbonioService::MsgHdrFromUri(nsIURI* uri, nsIMsgDBHdr** _retval) {
  nsCString keyStr;
  nsCString folderURIPath;

  nsCString scheme;
  nsresult rv = uri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);

  if (scheme.EqualsLiteral("carbonio-message")) {
    rv = MsgKeyStringFromMessageURI(uri, keyStr);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = uri->GetFilePath(folderURIPath);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (scheme.EqualsLiteral("x-moz-carbonio")) {
    rv = MsgKeyStringFromChannelURI(uri, keyStr, folderURIPath);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    NS_WARNING("CarbonioService::MsgHdrFromUri: unrecognized URI scheme");
    return NS_ERROR_UNEXPECTED;
  }

  nsMsgKey key =
      msgKeyFromInt(ParseUint64Str(PromiseFlatCString(keyStr).get()));

  // Folders are created with a "carbonio" scheme; strip the message key
  // before looking the folder up.
  RefPtr<nsIURI> folderUri;
  rv = NS_MutateURI(uri)
           .SetScheme("carbonio"_ns)
           .SetFilePath(folderURIPath)
           .SetQuery(""_ns)
           .SetRef(""_ns)
           .Finalize(getter_AddRefs(folderUri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCString folderSpec;
  rv = folderUri->GetSpec(folderSpec);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<nsIMsgFolder> folder;
  rv = GetExistingFolder(folderSpec, getter_AddRefs(folder));
  NS_ENSURE_SUCCESS(rv, rv);

  return folder->GetMessageHeader(key, _retval);
}

nsresult CarbonioService::FetchMessage(nsIURI* aURI,
                                       nsIStreamListener* aStreamListener) {
  nsCOMPtr<nsIIOService> netService = mozilla::components::IO::Service();
  NS_ENSURE_TRUE(netService, NS_ERROR_UNEXPECTED);

  nsCOMPtr<nsIChannel> messageChannel;
  MOZ_TRY(netService->NewChannelFromURI(
      aURI, nullptr, nsContentUtils::GetSystemPrincipal(), nullptr,
      nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
      nsIContentPolicy::TYPE_OTHER, getter_AddRefs(messageChannel)));

  return messageChannel->AsyncOpen(aStreamListener);
}
