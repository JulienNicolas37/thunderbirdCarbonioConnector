/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioListeners.h"

// Implementation of CarbonioFolderSyncListener

NS_IMPL_ISUPPORTS(CarbonioFolderSyncListener, ICarbonioFolderListener)

NS_IMETHODIMP CarbonioFolderSyncListener::OnNewRootFolder(
    const nsACString& id) {
  return mOnNewRootFolder(id);
}

NS_IMETHODIMP CarbonioFolderSyncListener::OnFolderUpserted(
    const nsACString& id, const nsACString& parentId, const nsACString& name,
    bool isMailFolder) {
  return mOnFolderUpserted(id, parentId, name, isMailFolder);
}

NS_IMETHODIMP CarbonioFolderSyncListener::OnFolderDeleted(
    const nsACString& id) {
  return mOnFolderDeleted(id);
}

NS_IMETHODIMP CarbonioFolderSyncListener::OnSyncStateTokenChanged(
    const nsACString& syncStateToken) {
  return mOnSyncStateTokenChanged(syncStateToken);
}

NS_IMETHODIMP CarbonioFolderSyncListener::OnSuccess() { return mOnSuccess(); }

NS_IMETHODIMP CarbonioFolderSyncListener::OnOperationFailure(
    nsresult status) {
  return mOnError(status);
}

// Implementation of CarbonioMessageFetchListener

NS_IMPL_ISUPPORTS(CarbonioMessageFetchListener, ICarbonioMessageFetchListener)

NS_IMETHODIMP CarbonioMessageFetchListener::OnFetchStart() {
  return mOnFetchStart();
}

NS_IMETHODIMP CarbonioMessageFetchListener::OnFetchedDataAvailable(
    nsIInputStream* stream) {
  uint64_t bytesFetched = 0;
  nsresult rv = mOnFetchedDataAvailable(stream, &bytesFetched);
  NS_ENSURE_SUCCESS(rv, rv);

  mBytesRead += bytesFetched;

  return NS_OK;
}

NS_IMETHODIMP CarbonioMessageFetchListener::OnFetchStop(nsresult status) {
  return mOnFetchStop(status, mBytesRead);
}

// Implementation of CarbonioSimpleListener

NS_IMPL_ISUPPORTS(CarbonioSimpleListener, ICarbonioSimpleOperationListener)

NS_IMETHODIMP CarbonioSimpleListener::OnOperationSuccess() {
  return mOnSuccess();
}

NS_IMETHODIMP CarbonioSimpleListener::OnOperationFailure(nsresult status) {
  return mOnFailure(status);
}
