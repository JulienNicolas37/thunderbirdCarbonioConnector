/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioFolder.h"

#include "nsIMsgDatabase.h"
#include "nsIMsgPluggableStore.h"
#include "nsMsgUtils.h"
#include "nsNetUtil.h"

// Distinct from the "carbonio:" scheme used for the account/folder URIs -
// mirrors EWS's separate "ews-message:" scheme for individual messages.
// Not yet backed by a working protocol handler (deferred, see
// components.conf) - safe to construct as a plain string regardless, it
// just won't be resolvable/openable until that handler exists.
constexpr auto kCarbonioMessageRootURI = "carbonio-message:/";

NS_IMPL_ISUPPORTS_INHERITED0(CarbonioFolder, nsMsgDBFolder)

CarbonioFolder::CarbonioFolder() = default;
CarbonioFolder::~CarbonioFolder() = default;

nsresult CarbonioFolder::CreateBaseMessageURI(const nsACString& aURI) {
  nsCOMPtr<nsIURI> folderUri;
  nsresult rv =
      NS_NewURI(getter_AddRefs(folderUri), PromiseFlatCString(aURI).Data());
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString scheme;
  rv = folderUri->GetScheme(scheme);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!scheme.EqualsLiteral("carbonio")) {
    return NS_ERROR_UNEXPECTED;
  }

  nsAutoCString tailURI(aURI);

  // Remove the scheme and the following ":/".
  nsAutoCString uriRoot(scheme);
  uriRoot.Append(":/");
  if (tailURI.Find(uriRoot) == 0) {
    tailURI.Cut(0, uriRoot.Length());
  }

  mBaseMessageURI = kCarbonioMessageRootURI;
  mBaseMessageURI += tailURI;

  return NS_OK;
}

/**
 * Mandatory override (pure virtual on `nsMsgDBFolder`). Cribbed from EWS,
 * itself cribbed from `nsImapMailFolder.cpp` - there's no shared default
 * implementation of this even though it seems protocol-agnostic.
 */
nsresult CarbonioFolder::GetDatabase() {
  if (!mDatabase) {
    nsresult rv;
    nsCOMPtr<nsIMsgDBService> msgDBService =
        do_GetService("@mozilla.org/msgDatabase/msgDBService;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    // Create the database, blowing it away if it needs to be rebuilt.
    rv = msgDBService->OpenFolderDB(this, false, getter_AddRefs(mDatabase));
    if (NS_FAILED(rv)) {
      rv = msgDBService->CreateNewDB(this, getter_AddRefs(mDatabase));
    }
    NS_ENSURE_SUCCESS(rv, rv);

    UpdateNewMessages();

    if (mAddListener) {
      mDatabase->AddListener(this);
    }

    UpdateSummaryTotals(true);
  }

  return NS_OK;
}

NS_IMETHODIMP CarbonioFolder::GetIncomingServerType(
    nsACString& aIncomingServerType) {
  aIncomingServerType.AssignLiteral("carbonio");
  return NS_OK;
}

/**
 * Mandatory in practice, even though not pure virtual: the base
 * `nsMsgDBFolder` implementation is an explicit `NS_ERROR_NOT_IMPLEMENTED`
 * stub. `GetStringProperty`/`SetStringProperty` - which we rely on to tag
 * folders with their Carbonio id - both go through this, so without a
 * working override every such call fails. Confirmed via targeted debug
 * logging. Cribbed from EWS, itself cribbed from `nsImapMailFolder.cpp`.
 */
NS_IMETHODIMP CarbonioFolder::GetDBFolderInfoAndDB(
    nsIDBFolderInfo** folderInfo, nsIMsgDatabase** database) {
  NS_ENSURE_ARG_POINTER(folderInfo);
  NS_ENSURE_ARG_POINTER(database);

  // Ensure that our cached database handle is initialized.
  nsresult rv = GetDatabase();
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*database = mDatabase);

  return (*database)->GetDBFolderInfo(folderInfo);
}

NS_IMETHODIMP CarbonioFolder::GetSubFolders(
    nsTArray<RefPtr<nsIMsgFolder>>& folders) {
  // Lazily rediscover children already known to the message store from a
  // previous session - without this, subFolders only ever reflects what
  // was added via AddSubfolder during the current one.
  if (!mHasLoadedSubfolders) {
    nsresult rv = CreateChildrenFromStore();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return nsMsgDBFolder::GetSubFolders(folders);
}

nsresult CarbonioFolder::CreateChildrenFromStore() {
  MOZ_ASSERT(!mHasLoadedSubfolders);  // Should only be called once.

  nsCOMPtr<nsIMsgPluggableStore> msgStore;
  nsresult rv = GetMsgStore(getter_AddRefs(msgStore));
  NS_ENSURE_SUCCESS(rv, rv);

  // Ask the store which children it thinks we have.
  nsTArray<nsCString> childNames;
  rv = msgStore->DiscoverChildFolders(this, childNames);
  NS_ENSURE_SUCCESS(rv, rv);

  // Have to set this NOW: folder creation via the folder lookup service
  // uses GetSubFolders() to search for existing folders, which would
  // otherwise recurse back into this same method.
  mHasLoadedSubfolders = true;

  for (auto& childName : childNames) {
    // Use the base AddSubfolder, not a Carbonio-specific one (we don't have
    // one): this is meant to restore state for folders that already exist,
    // not to treat them as newly created.
    nsCOMPtr<nsIMsgFolder> child;
    rv = nsMsgDBFolder::AddSubfolder(childName, getter_AddRefs(child));
    NS_ENSURE_SUCCESS(rv, rv);
    // mSubFolders now includes the new child.
  }

  for (nsIMsgFolder* child : mSubFolders) {
    // Recurse downward (we know it's a CarbonioFolder, since that's what
    // our folder factory always creates).
    rv = static_cast<CarbonioFolder*>(child)->CreateChildrenFromStore();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}
