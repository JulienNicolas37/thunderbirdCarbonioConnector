/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOINCOMINGSERVER_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOINCOMINGSERVER_H_

#include <functional>

#include "ICarbonioIncomingServer.h"
#include "nsMsgIncomingServer.h"

// Randomly generated for this new component - arbitrary but must be unique
// within Thunderbird.
#define CARBONIO_INCOMING_SERVER_IID \
  {0x7b3e9a5c, 0x1d4f, 0x4a8e, {0x9c, 0x2a, 0x6b, 0x8d, 0x1e, 0x4f, 0x7a, 0x3c}}

/**
 * Local property name used to record a folder's Carbonio id, so we can
 * translate between local folder and remote id when needed. Mirrors EWS's
 * `kExchangeIdProperty` (`"ewsId"`).
 */
constexpr auto kCarbonioIdProperty = "carbonioId";

/**
 * The Carbonio implementation of `nsIMsgIncomingServer`.
 *
 * Modeled after `ExchangeIncomingServer`, trimmed to phase 1 scope
 * (read-only: authentication, folder hierarchy sync, message retrieval).
 * Notably absent relative to EWS: OAuth-related attributes and accessors
 * (Carbonio phase 1 auth is username/password via `nsMsgIncomingServer`'s
 * built-in credential storage), delete model / trash folder path handling
 * (no message deletion yet), `PerformBiff`/`PerformExpand` (not wired up
 * yet - can be added once basic sync is confirmed working end to end).
 */
class CarbonioIncomingServer : public nsMsgIncomingServer,
                               public ICarbonioIncomingServer {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_ICARBONIOINCOMINGSERVER

  CarbonioIncomingServer();

  NS_INLINE_DECL_STATIC_IID(CARBONIO_INCOMING_SERVER_IID)

 protected:
  virtual ~CarbonioIncomingServer();

  /**
   * Locally creates a folder with the given properties if it doesn't already
   * exist (looked up via `kCarbonioIdProperty`). Intended to be called by a
   * friend class such as `CarbonioFolderSyncListener`.
   */
  nsresult MaybeCreateFolderWithDetails(const nsACString& id,
                                        const nsACString& parentId,
                                        const nsACString& name,
                                        bool isMailFolder);

  /**
   * Updates the local folder matching `id` if its name and/or parent differ
   * from what's given here (rename and/or reparent). Intended to be called
   * by a friend class such as `CarbonioFolderSyncListener`.
   */
  nsresult UpdateFolderWithDetails(const nsACString& id,
                                   const nsACString& parentId,
                                   const nsACString& name,
                                   nsIMsgWindow* msgWindow);

  /**
   * Deletes the local folder matching `id`, if any. Intended to be called by
   * a friend class such as `CarbonioFolderSyncListener`.
   */
  nsresult DeleteFolderWithId(const nsACString& id);

  friend class CarbonioFolderSyncListener;

  // nsIMsgIncomingServer
  NS_IMETHOD GetLocalStoreType(nsACString& aLocalStoreType) override;
  NS_IMETHOD GetLocalDatabaseType(nsACString& aLocalDatabaseType) override;
  NS_IMETHOD GetCanBeDefaultServer(bool* canBeDefaultServer) override;
  NS_IMETHOD GetOfflineSupportLevel(int32_t* aSupportLevel) override;
  NS_IMETHOD GetNewMessages(nsIMsgFolder* aFolder, nsIMsgWindow* aMsgWindow,
                            nsIUrlListener* aUrlListener) override;
  NS_IMETHOD Shutdown() override;
  NS_IMETHOD VerifyLogon(nsIUrlListener* aUrlListener, nsIMsgWindow* aMsgWindow,
                         nsIURI** _retval) override;
  NS_IMETHOD GetCanSearchMessages(bool* canSearchMessages) override;

 private:
  /**
   * Retrieves the folder associated with the given Carbonio id, via a
   * breadth-first search of the local folder tree (same approach as EWS's
   * `FindFolderWithId` - no separate id-to-folder index is maintained).
   * Returns `NS_MSG_ERROR_FOLDER_MISSING` if not found.
   */
  nsresult FindFolderWithId(const nsACString& id, nsIMsgFolder** _retval);

  /**
   * Synchronizes the folder list for this account, then calls the given
   * callback.
   */
  nsresult SyncFolderList(nsIMsgWindow* aMsgWindow,
                          std::function<nsresult()> postSyncCallback);

  RefPtr<ICarbonioClient> mClient;
};

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOINCOMINGSERVER_H_
