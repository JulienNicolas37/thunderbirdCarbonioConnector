/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioProtocolInfo.h"

#include "CarbonioIncomingServer.h"
#include "nsMailDirServiceDefs.h"
#include "nsMsgUtils.h"

#define PREF_MAIL_ROOT_CARBONIO_REL "mail.root.carbonio-rel"
#define PREF_MAIL_ROOT_CARBONIO "mail.root.carbonio"

NS_IMPL_ISUPPORTS(CarbonioProtocolInfo, nsIMsgProtocolInfo)

CarbonioProtocolInfo::CarbonioProtocolInfo() = default;
CarbonioProtocolInfo::~CarbonioProtocolInfo() = default;

NS_IMETHODIMP
CarbonioProtocolInfo::GetDefaultLocalPath(nsIFile** aDefaultLocalPath) {
  // Cribbed from ExchangeProtocolInfo, itself cribbed from nsImapService.cpp
  // - there's no shared implementation of this method even though it seems
  // protocol-agnostic.
  NS_ENSURE_ARG_POINTER(aDefaultLocalPath);
  *aDefaultLocalPath = nullptr;

  bool havePref;
  nsCOMPtr<nsIFile> localFile;
  nsresult rv = NS_GetPersistentFile(
      PREF_MAIL_ROOT_CARBONIO_REL, PREF_MAIL_ROOT_CARBONIO, NS_APP_MAIL_50_DIR,
      havePref, getter_AddRefs(localFile));
  if (NS_FAILED(rv)) return rv;

  bool exists;
  rv = localFile->Exists(&exists);
  if (NS_SUCCEEDED(rv) && !exists) {
    rv = localFile->Create(nsIFile::DIRECTORY_TYPE, 0775);
  }

  if (NS_FAILED(rv)) return rv;

  if (!havePref || !exists) {
    rv = NS_SetPersistentFile(PREF_MAIL_ROOT_CARBONIO_REL,
                              PREF_MAIL_ROOT_CARBONIO, localFile);
    NS_ASSERTION(NS_SUCCEEDED(rv), "Failed to set root dir pref.");
  }

  localFile.forget(aDefaultLocalPath);

  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::SetDefaultLocalPath(
    nsIFile* aDefaultLocalPath) {
  NS_ENSURE_ARG(aDefaultLocalPath);
  return NS_SetPersistentFile(PREF_MAIL_ROOT_CARBONIO_REL,
                              PREF_MAIL_ROOT_CARBONIO, aDefaultLocalPath);
}

NS_IMETHODIMP CarbonioProtocolInfo::GetServerIID(nsIID& aServerIID) {
  aServerIID = NS_GET_IID(CarbonioIncomingServer);
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetRequiresUsername(
    bool* aRequiresUsername) {
  NS_ENSURE_ARG_POINTER(aRequiresUsername);
  *aRequiresUsername = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetPreflightPrettyNameWithEmailAddress(
    bool* aPreflightPrettyNameWithEmailAddress) {
  NS_ENSURE_ARG_POINTER(aPreflightPrettyNameWithEmailAddress);
  *aPreflightPrettyNameWithEmailAddress = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetCanDelete(bool* aCanDelete) {
  NS_ENSURE_ARG_POINTER(aCanDelete);
  *aCanDelete = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetCanLoginAtStartUp(
    bool* aCanLoginAtStartUp) {
  NS_ENSURE_ARG_POINTER(aCanLoginAtStartUp);
  *aCanLoginAtStartUp = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetCanDuplicate(bool* aCanDuplicate) {
  NS_ENSURE_ARG_POINTER(aCanDuplicate);
  *aCanDuplicate = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetDefaultServerPort(bool isSecure,
                                                          int32_t* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);

  // Likely irrelevant in practice since the account uses the Carbonio URL
  // supplied during account setup (`carbonioUrl`), but return HTTP(S) ports
  // regardless, same as EWS.
  *_retval = isSecure ? 443 : 80;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetCanGetMessages(
    bool* aCanGetMessages) {
  NS_ENSURE_ARG_POINTER(aCanGetMessages);
  *aCanGetMessages = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetCanGetIncomingMessages(
    bool* aCanGetIncomingMessages) {
  NS_ENSURE_ARG_POINTER(aCanGetIncomingMessages);
  *aCanGetIncomingMessages = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetDefaultDoBiff(bool* aDefaultDoBiff) {
  NS_ENSURE_ARG_POINTER(aDefaultDoBiff);
  // Unlike EWS: false for now. `PerformBiff` isn't overridden yet on
  // `CarbonioIncomingServer` (phase 1 scope, see its header), so enabling
  // biff by default would rely on `nsMsgIncomingServer`'s base
  // implementation behaving sanely for a protocol it doesn't know about -
  // untested. Revisit once `PerformBiff` is implemented.
  *aDefaultDoBiff = false;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetShowComposeMsgLink(
    bool* aShowComposeMsgLink) {
  NS_ENSURE_ARG_POINTER(aShowComposeMsgLink);
  *aShowComposeMsgLink = true;
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolInfo::GetFoldersCreatedAsync(
    bool* aFoldersCreatedAsync) {
  NS_ENSURE_ARG_POINTER(aFoldersCreatedAsync);
  *aFoldersCreatedAsync = true;
  return NS_OK;
}
