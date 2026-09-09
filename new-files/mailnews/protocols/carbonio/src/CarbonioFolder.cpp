/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioFolder.h"

#include "nsIMsgDatabase.h"
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
