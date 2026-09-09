/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioIncomingServer.h"

#include <utility>

#include "CarbonioListeners.h"
#include "ICarbonioClient.h"
#include "mozilla/Components.h"
#include "nsIMsgFolderNotificationService.h"
#include "nsIMsgWindow.h"
#include "nsMsgFolderFlags.h"
#include "nsMsgUtils.h"
#include "nsPrintfCString.h"
#include "OfflineStorage.h"

constexpr auto kSyncStateTokenProperty = "carbonioSyncStateToken";

NS_IMPL_ADDREF_INHERITED(CarbonioIncomingServer, nsMsgIncomingServer)
NS_IMPL_RELEASE_INHERITED(CarbonioIncomingServer, nsMsgIncomingServer)
NS_IMPL_QUERY_HEAD(CarbonioIncomingServer)
NS_IMPL_QUERY_BODY(ICarbonioIncomingServer)
NS_IMPL_QUERY_TAIL_INHERITING(nsMsgIncomingServer)

CarbonioIncomingServer::CarbonioIncomingServer() = default;
CarbonioIncomingServer::~CarbonioIncomingServer() = default;

/**
 * Creates a new folder with the specified id/parent/name if none exists
 * locally with that id yet. If a folder with the same name but a different
 * (or no) Carbonio id already exists under the same parent, returns an
 * error rather than risk conflating two distinct folders.
 *
 * Mirrors EWS's `MaybeCreateFolderWithDetails`.
 */
nsresult CarbonioIncomingServer::MaybeCreateFolderWithDetails(
    const nsACString& id, const nsACString& parentId, const nsACString& name,
    bool isMailFolder) {
  nsCOMPtr<nsIMsgFolder> existingFolder;
  nsresult rv = FindFolderWithId(id, getter_AddRefs(existingFolder));
  if (NS_SUCCEEDED(rv)) {
    // Already created locally (e.g. by the user, or a previous sync);
    // nothing to do.
    return NS_OK;
  }

  if (!isMailFolder) {
    // Out of scope for phase 1 (calendar/contacts/tasks/tag folders) - skip
    // rather than create a local mail folder for something that isn't one.
    return NS_OK;
  }

  nsCOMPtr<nsIMsgFolder> parent;
  rv = FindFolderWithId(parentId, getter_AddRefs(parent));
  NS_ENSURE_SUCCESS(rv, rv);

  bool containsChildWithRequestedName;
  rv = parent->ContainsChildNamed(name, &containsChildWithRequestedName);
  NS_ENSURE_SUCCESS(rv, rv);
  if (containsChildWithRequestedName) {
    return NS_MSG_CANT_CREATE_FOLDER;
  }

  nsCOMPtr<nsIMsgPluggableStore> msgStore;
  rv = GetMsgStore(getter_AddRefs(msgStore));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIMsgFolder> newFolder;
  rv = msgStore->CreateFolder(parent, name, getter_AddRefs(newFolder));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = newFolder->SetStringProperty(kCarbonioIdProperty, id);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = newFolder->SetName(name);
  NS_ENSURE_SUCCESS(rv, rv);

  // Known system folder ids, confirmed empirically to be stable across
  // Carbonio instances (see the connector repo's phase 1 spec doc) - used
  // here instead of parsing a flags value like EWS does, since Carbonio's
  // `SyncResponse` doesn't expose an equivalent flags field for phase 1's
  // purposes.
  if (id.EqualsLiteral("2")) {
    rv = newFolder->SetFlag(nsMsgFolderFlags::Inbox);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (id.EqualsLiteral("3")) {
    rv = newFolder->SetFlag(nsMsgFolderFlags::Trash);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (id.EqualsLiteral("4")) {
    rv = newFolder->SetFlag(nsMsgFolderFlags::Junk);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (id.EqualsLiteral("5")) {
    rv = newFolder->SetFlag(nsMsgFolderFlags::SentMail);
    NS_ENSURE_SUCCESS(rv, rv);
  } else if (id.EqualsLiteral("6")) {
    rv = newFolder->SetFlag(nsMsgFolderFlags::Drafts);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIMsgFolderNotificationService> notifier =
      mozilla::components::FolderNotification::Service();
  notifier->NotifyFolderAdded(newFolder);

  rv = parent->NotifyFolderAdded(newFolder);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult CarbonioIncomingServer::UpdateFolderWithDetails(
    const nsACString& id, const nsACString& parentId, const nsACString& name,
    nsIMsgWindow* msgWindow) {
  nsCOMPtr<nsIMsgFolder> folder;
  nsresult rv = FindFolderWithId(id, getter_AddRefs(folder));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIMsgFolder> parentFolder;
  rv = folder->GetParent(getter_AddRefs(parentFolder));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString currentName;
  MOZ_TRY(folder->GetName(currentName));
  nsAutoCString currentParentId;
  MOZ_TRY(parentFolder->GetStringProperty(kCarbonioIdProperty, currentParentId));

  if (currentName.Equals(name) && currentParentId.Equals(parentId)) {
    // Nothing changed.
    return NS_OK;
  }

  nsCOMPtr<nsIMsgFolder> newParentFolder;
  rv = FindFolderWithId(parentId, getter_AddRefs(newParentFolder));
  NS_ENSURE_SUCCESS(rv, rv);

  return LocalRenameOrReparentFolder(folder, newParentFolder, name, msgWindow);
}

nsresult CarbonioIncomingServer::DeleteFolderWithId(const nsACString& id) {
  nsCOMPtr<nsIMsgFolder> folder;
  nsresult rv = FindFolderWithId(id, getter_AddRefs(folder));
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIMsgFolder> parentFolder;
    rv = folder->GetParent(getter_AddRefs(parentFolder));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = parentFolder->PropagateDelete(folder, true);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  // If not found locally, assume it's already been deleted locally.
  return NS_OK;
}

nsresult CarbonioIncomingServer::FindFolderWithId(const nsACString& id,
                                                   nsIMsgFolder** _retval) {
  nsresult failureStatus{NS_MSG_ERROR_FOLDER_MISSING};

  RefPtr<nsIMsgFolder> root;
  nsresult rv = GetRootFolder(getter_AddRefs(root));
  NS_ENSURE_SUCCESS(rv, rv);

  nsTArray<RefPtr<nsIMsgFolder>> foldersToScan;
  foldersToScan.AppendElement(root);

  while (foldersToScan.Length() != 0) {
    nsTArray<RefPtr<nsIMsgFolder>> nextFoldersToScan;

    for (auto folder : foldersToScan) {
      nsCString folderId;
      rv = folder->GetStringProperty(kCarbonioIdProperty, folderId);

      if (NS_SUCCEEDED(rv) && folderId.Equals(id)) {
        folder.forget(_retval);
        return NS_OK;
      }

      nsTArray<RefPtr<nsIMsgFolder>> subfolders;
      rv = folder->GetSubFolders(subfolders);
      if (NS_SUCCEEDED(rv)) {
        nextFoldersToScan.AppendElements(subfolders);
      } else {
        failureStatus = rv;
      }
    }

    foldersToScan = std::move(nextFoldersToScan);
  }

  return failureStatus;
}

NS_IMETHODIMP CarbonioIncomingServer::GetProtocolClientRunning(bool* running) {
  NS_ENSURE_ARG_POINTER(running);
  if (!mClient) {
    *running = false;
    return NS_OK;
  }
  return mClient->GetRunning(running);
}

NS_IMETHODIMP CarbonioIncomingServer::GetProtocolClientIdle(bool* idle) {
  NS_ENSURE_ARG_POINTER(idle);
  if (!mClient) {
    *idle = false;
    return NS_OK;
  }
  return mClient->GetIdle(idle);
}

/**
 * Creates a new Carbonio client, or reuses the existing one.
 */
NS_IMETHODIMP CarbonioIncomingServer::GetProtocolClient(
    ICarbonioClient** carbonioClient) {
  NS_ENSURE_ARG_POINTER(carbonioClient);

  if (!mClient) {
    nsresult rv = NS_OK;
    nsCOMPtr<ICarbonioClient> tempClient =
        do_CreateInstance("@mozilla.org/messenger/carbonio-client;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString endpoint;
    rv = GetCarbonioUrl(endpoint);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = tempClient->Initialize(endpoint, this);
    NS_ENSURE_SUCCESS(rv, rv);

    mClient = tempClient;
  }

  NS_IF_ADDREF(*carbonioClient = mClient);
  return NS_OK;
}

NS_IMETHODIMP CarbonioIncomingServer::GetCarbonioUrl(nsACString& value) {
  return GetStringValue("carbonio_url", value);
}

NS_IMETHODIMP CarbonioIncomingServer::SetCarbonioUrl(const nsACString& value) {
  return SetStringValue("carbonio_url", value);
}

NS_IMETHODIMP CarbonioIncomingServer::SyncFolderHierarchy(
    ICarbonioSimpleOperationListener* listener, nsIMsgWindow* window) {
  const auto refListener = RefPtr{listener};
  return SyncFolderList(window,
                        [refListener]() { return refListener->OnOperationSuccess(); });
}

nsresult CarbonioIncomingServer::SyncFolderList(
    nsIMsgWindow* aMsgWindow, std::function<nsresult()> postSyncCallback) {
  nsCString syncStateToken;
  nsresult rv = GetStringValue(kSyncStateTokenProperty, syncStateToken);
  if (NS_FAILED(rv)) {
    syncStateToken = EmptyCString();
  }

  auto onNewRootFolder = [self = RefPtr(this)](const nsACString& id) {
    RefPtr<nsIMsgFolder> root;
    nsresult rv = self->GetRootFolder(getter_AddRefs(root));
    NS_ENSURE_SUCCESS(rv, rv);
    return root->SetStringProperty(kCarbonioIdProperty, id);
  };

  nsCOMPtr<nsIMsgWindow> msgWindow = aMsgWindow;
  auto onFolderUpserted = [self = RefPtr(this), msgWindow](
                              const nsACString& id, const nsACString& parentId,
                              const nsACString& name, bool isMailFolder) {
    nsCOMPtr<nsIMsgFolder> existingFolder;
    nsresult rv = self->FindFolderWithId(id, getter_AddRefs(existingFolder));
    if (NS_SUCCEEDED(rv)) {
      return self->UpdateFolderWithDetails(id, parentId, name, msgWindow);
    }
    return self->MaybeCreateFolderWithDetails(id, parentId, name,
                                              isMailFolder);
  };

  auto onFolderDeleted = [self = RefPtr(this)](const nsACString& id) {
    return self->DeleteFolderWithId(id);
  };

  auto onSyncStateTokenChanged =
      [self = RefPtr(this)](const nsACString& syncStateToken) {
        return self->SetStringValue(kSyncStateTokenProperty, syncStateToken);
      };

  auto onSuccess = [postSyncCallback]() { return postSyncCallback(); };

  auto onError = [postSyncCallback](nsresult _status) {
    // As with EWS: continue with the post-sync callback even on error, since
    // it may have follow-on work (e.g. per-folder message sync in a later
    // phase) that shouldn't be skipped just because the hierarchy sync
    // itself failed.
    return postSyncCallback();
  };

  RefPtr<CarbonioFolderSyncListener> listener = new CarbonioFolderSyncListener(
      onNewRootFolder, onFolderUpserted, onFolderDeleted,
      onSyncStateTokenChanged, onSuccess, onError);

  RefPtr<ICarbonioClient> client;
  MOZ_TRY(GetProtocolClient(getter_AddRefs(client)));
  return client->SyncFolderHierarchy(listener, syncStateToken);
}

// --- nsIMsgIncomingServer ---------------------------------------------------

NS_IMETHODIMP CarbonioIncomingServer::GetLocalStoreType(
    nsACString& aLocalStoreType) {
  aLocalStoreType.Assign("carbonio");
  return NS_OK;
}

NS_IMETHODIMP CarbonioIncomingServer::GetLocalDatabaseType(
    nsACString& aLocalDatabaseType) {
  aLocalDatabaseType.AssignLiteral("mailbox");
  return NS_OK;
}

NS_IMETHODIMP CarbonioIncomingServer::GetCanBeDefaultServer(
    bool* canBeDefaultServer) {
  NS_ENSURE_ARG_POINTER(canBeDefaultServer);
  *canBeDefaultServer = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioIncomingServer::GetOfflineSupportLevel(
    int32_t* aSupportLevel) {
  NS_ENSURE_ARG_POINTER(aSupportLevel);
  *aSupportLevel = OFFLINE_SUPPORT_LEVEL_NONE;
  return NS_OK;
}

NS_IMETHODIMP CarbonioIncomingServer::GetCanSearchMessages(
    bool* canSearchMessages) {
  NS_ENSURE_ARG_POINTER(canSearchMessages);
  *canSearchMessages = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioIncomingServer::GetNewMessages(
    nsIMsgFolder* aFolder, nsIMsgWindow* aMsgWindow,
    nsIUrlListener* aUrlListener) {
  // Phase 1 scope: syncing the folder hierarchy is as far as this goes for
  // now. Per-folder message sync (mirroring EWS's `SyncAllFolders`/
  // `SyncFolders`) is the next piece of work once this compiles and folder
  // hierarchy sync is confirmed working end to end.
  return SyncFolderList(aMsgWindow, []() { return NS_OK; });
}

NS_IMETHODIMP CarbonioIncomingServer::Shutdown() {
  if (mClient) {
    return mClient->Shutdown();
  }
  return nsMsgIncomingServer::Shutdown();
}

NS_IMETHODIMP
CarbonioIncomingServer::VerifyLogon(nsIUrlListener* aUrlListener,
                                    nsIMsgWindow* aMsgWindow,
                                    nsIURI** _retval) {
  NS_ENSURE_ARG_POINTER(aUrlListener);

  RefPtr<ICarbonioClient> client;
  MOZ_TRY(GetProtocolClient(getter_AddRefs(client)));
  return client->CheckConnectivity(aUrlListener, _retval);
}
