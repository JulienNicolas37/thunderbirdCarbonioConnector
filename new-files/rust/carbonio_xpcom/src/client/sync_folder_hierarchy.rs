/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::Result;
use crate::soap;
use crate::types::{SyncFolder, SyncRequest, SyncRequestBody, SyncResponseBody};

use super::CarbonioClient;

/// A single folder-level change resulting from a sync.
///
/// Carbonio's `SyncRequest` returns *current state* for anything that
/// changed since the given token, not a change log — so there is no
/// separate "created" vs "updated" case here. Confirmed empirically: a
/// folder created then renamed between two sync calls shows up as a single
/// `Upserted` entry with its final name, not two events. Callers should
/// decide "new vs existing" the same way `ExchangeIncomingServer` does for
/// EWS: look up the id locally first (`FindFolderWithId`-equivalent), and
/// create vs. update accordingly.
#[derive(Debug, Clone)]
pub enum FolderChange {
    Upserted {
        id: String,
        /// `None` only for the account's technical root (id `11`), which
        /// has no parent.
        parent_id: Option<String>,
        name: String,
        is_mail_folder: bool,
    },
    Deleted {
        id: String,
    },
}

/// Result of a [`CarbonioClient::sync_folder_hierarchy`] call.
pub struct SyncResult {
    pub changes: Vec<FolderChange>,
    /// Sync token to store and pass as `previous_sync_token` on the next
    /// call. `None` in the (should-not-happen) case the server didn't
    /// return one; callers should treat that as "resync from scratch next
    /// time" the same way `ExchangeIncomingServer::SyncFolderList` falls
    /// back to an empty token on failure to read a stored one.
    pub new_sync_token: Option<String>,
}

/// Synchronizes the folder hierarchy, normalizing Carbonio's two different
/// response shapes into one flat list of [`FolderChange`]:
///
///   - An *initial* sync (`previous_sync_token: None`) returns a nested
///     folder tree, which this function flattens.
///   - A *delta* sync (`previous_sync_token: Some(...)`) already returns a
///     flat list, with each folder's parent given by its `l` field.
///
/// Both confirmed empirically against a real Carbonio instance — see
/// `docs/carbonio-thunderbird-connecteur-phase1-specification.md` in the
/// connector repo for the raw responses this is based on.
///
/// Always sends `typed: 1`: without it, `deleted` mixes ids of every object
/// type (folders, messages, tags, ...) with no way to tell them apart —
/// also confirmed empirically, and the reason [`crate::types::Deleted`] has
/// a `folder` field separate from its generic `ids`.
pub(super) async fn sync_folder_hierarchy(
    client: &CarbonioClient,
    previous_sync_token: Option<String>,
) -> Result<SyncResult> {
    let token = client.auth_token().await?;

    let request = SyncRequestBody {
        sync_request: SyncRequest {
            jsns: "urn:zimbraMail",
            token: previous_sync_token,
            typed: Some(1),
        },
    };

    let response: SyncResponseBody =
        soap::send(&client.http_client, &client.endpoint, Some(&token), request).await?;

    let sync_response = response.sync_response;

    let mut changes = Vec::new();

    for root in &sync_response.folder {
        flatten_folder(root, &mut changes);
    }

    for deleted in &sync_response.deleted {
        log::debug!("SyncResponse deleted block (all types): {:?}", deleted.ids);
        for block in &deleted.folder {
            for id in block.split() {
                changes.push(FolderChange::Deleted {
                    id: id.to_string(),
                });
            }
        }
    }

    Ok(SyncResult {
        changes,
        new_sync_token: sync_response.token,
    })
}

/// Recursively flattens a (possibly nested) [`SyncFolder`] into `out`.
///
/// For a delta sync, `folder.folder` is always empty, so this is equivalent
/// to a single push per call — the recursion only does real work for an
/// initial sync's nested tree.
fn flatten_folder(folder: &SyncFolder, out: &mut Vec<FolderChange>) {
    // The account's absolute technical root (id `11`) carries no name and
    // isn't a folder callers should create locally; still recurse into its
    // children (which includes `USER_ROOT`, the actual mailbox root).
    if let Some(name) = &folder.name {
        out.push(FolderChange::Upserted {
            id: folder.id.clone(),
            parent_id: folder.l.clone(),
            name: name.clone(),
            is_mail_folder: is_mail_folder(&folder.id, folder.view.as_deref()),
        });
    }

    for child in &folder.folder {
        flatten_folder(child, out);
    }
}

/// Known non-mail system folders, confirmed empirically to have stable ids
/// across Carbonio instances (see the phase 1 spec doc). Needed in addition
/// to the `view`-based check below: "Chats" (id `14`) reports
/// `view: "message"`, exactly like a real mail folder, so `view` alone
/// can't tell them apart. "Tasks" (id `15`) and "Comments" (id `17`) instead
/// report no `view` at all, same as several genuine system mail folders
/// (Inbox, Trash, Drafts, Sent, Junk) - so absence of `view` isn't a
/// reliable signal either way on its own.
const NON_MAIL_SYSTEM_FOLDER_IDS: &[&str] = &[
    "7",  // Contacts
    "8",  // Tags
    "9",  // Conversations
    "10", // Calendar
    "13", // Emailed Contacts
    "14", // Chats
    "15", // Tasks
    "17", // Comments
];

/// Best-effort filter for "is this folder a mail folder", combining the
/// known system folder ids above with a `view`-based check for
/// user-created non-mail folders (e.g. a personal address book or calendar
/// created under a shared folder), which do carry an accurate `view`.
fn is_mail_folder(id: &str, view: Option<&str>) -> bool {
    if NON_MAIL_SYSTEM_FOLDER_IDS.contains(&id) {
        return false;
    }

    !matches!(
        view,
        Some("appointment")
            | Some("contact")
            | Some("task")
            | Some("calendar_group")
            | Some("tag")
            | Some("conversation")
    )
}
