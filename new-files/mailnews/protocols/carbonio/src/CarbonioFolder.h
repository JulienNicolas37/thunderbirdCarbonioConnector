/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOFOLDER_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOFOLDER_H_

#include "nsMsgDBFolder.h"

/**
 * The Carbonio implementation of `nsIMsgFolder`.
 *
 * Modeled after `ExchangeFolder`, but *much* smaller for phase 1: reading
 * `nsMsgDBFolder.cpp` shows it already provides working default
 * implementations for nearly all of `nsIMsgFolder`'s surface (copy, move,
 * delete, compact, junk handling, filtering, auto-sync, folder cache...) -
 * none of which phase 1 (read-only: folder hierarchy display) needs anyway.
 * This only overrides what genuinely has no usable default:
 *
 *   - `GetDatabase()` - pure virtual on `nsMsgDBFolder`, mandatory for any
 *     subclass.
 *   - `CreateBaseMessageURI()`, `GetIncomingServerType()`, and
 *     `GetDBFolderInfoAndDB()` - not pure virtual, but their defaults are
 *     explicit "not implemented" placeholders (see `nsMsgDBFolder.cpp`), so
 *     a working override is needed for correct behavior even though the
 *     compiler wouldn't enforce it. `GetDBFolderInfoAndDB()`'s stub in
 *     particular isn't just an unlikely edge case: `GetStringProperty`/
 *     `SetStringProperty` (used to identify folders by their Carbonio id)
 *     both go through it, so without this override, every single
 *     `GetStringProperty` call on a `CarbonioFolder` fails with
 *     `NS_ERROR_NOT_IMPLEMENTED` - confirmed via targeted debug logging.
 *   - `GetSubFolders()` - the default doesn't lazily (re)discover children
 *     already on disk from a previous session; it only picks up folders
 *     added via `AddSubfolder` during the current one. Without this
 *     override, the hierarchy synced and displayed in one session vanishes
 *     the next time Thunderbird starts, even though nothing was actually
 *     lost - confirmed empirically. Mirrors EWS's lazy
 *     `CreateChildrenFromStore()`, asking the message store which children
 *     it already knows about.
 *
 * Deliberately relying on `nsMsgDBFolder`'s default for everything else,
 * to be revisited only if phase 1 testing shows a specific need:
 *   - `GetNewMessages` - default behavior untested against a real
 *     Carbonio-backed hierarchy yet.
 *   - `GetSupportsOffline` - the default already reads the incoming
 *     server's offline support level, which `CarbonioIncomingServer` sets
 *     to `OFFLINE_SUPPORT_LEVEL_NONE`.
 *   - `GetDeletable` - defaults to `false`, appropriate until folder
 *     deletion is implemented (a later phase).
 *   - Every copy/move/compact/junk-related method.
 */
class CarbonioFolder : public nsMsgDBFolder {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  CarbonioFolder();

  NS_IMETHOD GetSubFolders(nsTArray<RefPtr<nsIMsgFolder>>& folders) override;

 protected:
  virtual ~CarbonioFolder();

  virtual nsresult CreateBaseMessageURI(const nsACString& aURI) override;
  virtual nsresult GetDatabase() override;

  NS_IMETHOD GetIncomingServerType(nsACString& aIncomingServerType) override;
  NS_IMETHOD GetDBFolderInfoAndDB(nsIDBFolderInfo** folderInfo,
                                  nsIMsgDatabase** database) override;

 private:
  /**
   * Recursively creates child folder objects by asking the message store
   * which ones it already knows about on disk, so a hierarchy synced in a
   * previous session is rediscovered rather than appearing empty.
   * Idempotent via `mHasLoadedSubfolders`. Cribbed from EWS's
   * `CreateChildrenFromStore`.
   */
  nsresult CreateChildrenFromStore();

  nsCString mBaseMessageURI;
  bool mHasLoadedSubfolders = false;
};

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOFOLDER_H_
