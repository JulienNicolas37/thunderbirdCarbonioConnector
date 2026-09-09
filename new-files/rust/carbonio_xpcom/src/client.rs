/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Instant;

use async_lock::Mutex;
use url::Url;

use moz_http::Client as HttpClient;
use protocol_shared::client::ProtocolClient;

use crate::error::Result;

mod authenticate;
mod get_message;
mod sync_folder_hierarchy;

pub use sync_folder_hierarchy::{FolderChange, SyncResult};

/// A cached, still-valid (as far as we know) authentication session.
struct Session {
    token: String,
    /// When we should consider this token expired and re-authenticate,
    /// rather than waiting for the server to reject a request with it. See
    /// `authenticate.rs` for how this is computed.
    expires_at: Instant,
}

/// A client for a single Carbonio account, talking to its SOAP-over-JSON API.
///
/// Owns the account's credentials directly for now. This is a phase 1
/// prototype simplification: once `CarbonioIncomingServer` (the C++
/// incoming-server shell, modeled after `ExchangeIncomingServer`) exists,
/// credentials should instead be retrieved through it — the same way
/// `protocol_shared::authentication::AuthenticationProvider` lets EWS/Graph
/// pull credentials from `nsIMsgIncomingServer` — so this client doesn't
/// duplicate Thunderbird's credential storage and prompting logic.
pub struct CarbonioClient {
    http_client: HttpClient,
    /// The server's SOAP endpoint, e.g. `https://mail.example.com/service/soap`.
    endpoint: Url,
    username: String,
    password: String,
    session: Mutex<Option<Session>>,
    shutting_down: AtomicBool,
}

impl CarbonioClient {
    pub fn new(endpoint: Url, username: String, password: String) -> Self {
        Self {
            http_client: HttpClient::new(),
            endpoint,
            username,
            password,
            session: Mutex::new(None),
            shutting_down: AtomicBool::new(false),
        }
    }

    /// Returns a valid auth token, authenticating (or re-authenticating)
    /// against the server first if there's no cached session or the cached
    /// one is at/past its expiry.
    async fn auth_token(&self) -> Result<String> {
        authenticate::ensure_session(self).await
    }

    /// Synchronizes the folder hierarchy.
    ///
    /// `previous_sync_token` should be `None` for an initial sync, or the
    /// `new_sync_token` returned by a previous call otherwise. See
    /// [`sync_folder_hierarchy::sync_folder_hierarchy`] for details on the
    /// two different response shapes this normalizes.
    pub async fn sync_folder_hierarchy(
        &self,
        previous_sync_token: Option<String>,
    ) -> Result<SyncResult> {
        sync_folder_hierarchy::sync_folder_hierarchy(self, previous_sync_token).await
    }

    /// Fetches a single message by id.
    ///
    /// UNVALIDATED (see `get_message.rs`): the exact response shape hasn't
    /// been confirmed against a real server yet, unlike auth and folder
    /// sync. Returns the raw decoded JSON for now.
    pub async fn get_message(&self, message_id: &str) -> Result<serde_json::Value> {
        get_message::get_message(self, message_id).await
    }
}

impl ProtocolClient for CarbonioClient {
    async fn shutdown(self: std::sync::Arc<Self>) {
        // No background tasks or long-lived connections to tear down yet
        // (phase 1 has no push/idle mechanism). This flag exists so callers
        // can check `is_shutting_down()` and short-circuit rather than
        // starting new requests; wire it up once there's more than one
        // caller of this client to make that race matter in practice.
        self.shutting_down.store(true, Ordering::SeqCst);
    }
}

impl CarbonioClient {
    pub fn is_shutting_down(&self) -> bool {
        self.shutting_down.load(Ordering::SeqCst)
    }
}
