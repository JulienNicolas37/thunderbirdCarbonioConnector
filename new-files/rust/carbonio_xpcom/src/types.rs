/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! JSON (de)serialization types for Carbonio's SOAP-over-JSON API
//! (`urn:zimbraSoap`).
//!
//! Scope note: these types only cover what phase 1 (read-only mail sync)
//! needs. Carbonio/Zimbra's SOAP responses carry many more fields (ACLs,
//! retention policies, calendar/contact-specific attributes, etc.) which are
//! simply ignored by `serde` (unknown fields are dropped by default) rather
//! than modeled here. Extend as later phases need more data.
//!
//! Confirmed empirically (see `docs/carbonio-thunderbird-connecteur-phase1-specification.md`
//! in the connector repo) against a real Carbonio instance:
//!   - the auth token travels in `Header.context.authToken`, not as an HTTP
//!     header or cookie;
//!   - `GetFolderRequest`/an initial (token-less) `SyncRequest` return a
//!     *nested* folder tree; a delta `SyncRequest` (with `token`) returns a
//!     *flat* list of changed folders, each carrying its parent id in `l`;
//!   - `SyncRequest` must be sent with `typed: 1`, otherwise `deleted` mixes
//!     ids of every object type (folders, messages, tags, ...) with no way
//!     to tell them apart.

use serde::{Deserialize, Serialize};

/// The full envelope wrapping every SOAP-over-JSON request/response.
#[derive(Debug, Serialize)]
pub(crate) struct Envelope<B> {
    #[serde(rename = "Header")]
    pub header: Header,
    #[serde(rename = "Body")]
    pub body: B,
    #[serde(rename = "_jsns")]
    pub jsns: &'static str,
}

#[derive(Debug, Serialize)]
pub(crate) struct Header {
    pub context: Context,
}

#[derive(Debug, Serialize)]
pub(crate) struct Context {
    #[serde(rename = "_jsns")]
    pub jsns: &'static str,
    #[serde(rename = "authToken", skip_serializing_if = "Option::is_none")]
    pub auth_token: Option<String>,
}

/// Envelope shape for deserializing *any* response, before we know which
/// request kind it corresponds to. Lets us check for `Fault` uniformly.
#[derive(Debug, Deserialize)]
pub(crate) struct ResponseEnvelope<B> {
    #[serde(rename = "Body")]
    pub body: ResponseBody<B>,
}

#[derive(Debug, Deserialize)]
pub(crate) struct ResponseBody<B> {
    #[serde(rename = "Fault")]
    pub fault: Option<Fault>,
    #[serde(flatten)]
    pub content: Option<B>,
}

#[derive(Debug, Deserialize)]
pub(crate) struct Fault {
    #[serde(rename = "Reason")]
    pub reason: FaultReason,
    #[serde(rename = "Detail")]
    pub detail: Option<FaultDetail>,
}

#[derive(Debug, Deserialize)]
pub(crate) struct FaultReason {
    #[serde(rename = "Text")]
    pub text: String,
}

#[derive(Debug, Deserialize)]
pub(crate) struct FaultDetail {
    #[serde(rename = "Error")]
    pub error: Option<FaultError>,
}

#[derive(Debug, Deserialize)]
pub(crate) struct FaultError {
    #[serde(rename = "Code")]
    pub code: Option<String>,
}

// --- AuthRequest / AuthResponse --------------------------------------------

#[derive(Debug, Serialize)]
pub(crate) struct AuthRequest {
    #[serde(rename = "_jsns")]
    pub jsns: &'static str,
    #[serde(rename = "csrfTokenSecured")]
    pub csrf_token_secured: bool,
    #[serde(rename = "persistAuthTokenCookie")]
    pub persist_auth_token_cookie: bool,
    pub account: AuthAccount,
    pub password: String,
}

/// Wraps [`AuthRequest`] under the `"AuthRequest"` key expected as the
/// direct child of `Body` - confirmed empirically to be required (without
/// it, Carbonio responds with a `service.UNKNOWN_DOCUMENT` fault, having
/// misidentified a nested field as the command name). Mirrors how
/// [`AuthResponseBody`] already unwraps the equivalent `"AuthResponse"` key
/// on the way back.
#[derive(Debug, Serialize)]
pub(crate) struct AuthRequestBody {
    #[serde(rename = "AuthRequest")]
    pub auth_request: AuthRequest,
}

#[derive(Debug, Serialize)]
pub(crate) struct AuthAccount {
    pub by: &'static str,
    #[serde(rename = "_content")]
    pub content: String,
}

#[derive(Debug, Deserialize)]
pub(crate) struct AuthResponseBody {
    #[serde(rename = "AuthResponse")]
    pub auth_response: AuthResponse,
}

#[derive(Debug, Deserialize)]
pub(crate) struct AuthResponse {
    #[serde(rename = "authToken")]
    pub auth_token: Vec<AuthToken>,
    /// Token lifetime, in milliseconds, as returned by the server.
    pub lifetime: u64,
}

#[derive(Debug, Deserialize)]
pub(crate) struct AuthToken {
    #[serde(rename = "_content")]
    pub content: String,
}

// --- SyncRequest / SyncResponse --------------------------------------------

#[derive(Debug, Serialize)]
pub(crate) struct SyncRequest {
    #[serde(rename = "_jsns")]
    pub jsns: &'static str,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub token: Option<String>,
    /// Always sent as `Some(1)` by this client: without it, `deleted` mixes
    /// ids of every object type with no way to tell them apart. See the
    /// module-level doc comment.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub typed: Option<u8>,
}

/// See [`AuthRequestBody`] for why this wrapping is needed.
#[derive(Debug, Serialize)]
pub(crate) struct SyncRequestBody {
    #[serde(rename = "SyncRequest")]
    pub sync_request: SyncRequest,
}

#[derive(Debug, Deserialize)]
pub(crate) struct SyncResponseBody {
    #[serde(rename = "SyncResponse")]
    pub sync_response: SyncResponse,
}

#[derive(Debug, Deserialize)]
pub(crate) struct SyncResponse {
    /// New sync token to store and pass as `token` on the next call.
    ///
    /// Confirmed empirically: Carbonio does not consistently encode this as
    /// either a JSON string or a JSON number across responses, so we accept
    /// either and normalize to a `String`.
    #[serde(deserialize_with = "deserialize_token", default)]
    pub token: Option<String>,
    #[serde(default)]
    pub folder: Vec<SyncFolder>,
    #[serde(default)]
    pub deleted: Vec<Deleted>,
}

fn deserialize_token<'de, D>(deserializer: D) -> Result<Option<String>, D::Error>
where
    D: serde::Deserializer<'de>,
{
    #[derive(Deserialize)]
    #[serde(untagged)]
    enum TokenValue {
        Str(String),
        Num(i64),
    }

    Ok(Option::<TokenValue>::deserialize(deserializer)?.map(|v| match v {
        TokenValue::Str(s) => s,
        TokenValue::Num(n) => n.to_string(),
    }))
}

/// A folder as it appears in a `SyncResponse`.
///
/// Note the recursive `folder` field: this is only populated for an
/// *initial* sync (no `token` sent), which returns a nested tree. A delta
/// sync (with `token`) returns folders flat, with an empty `folder` here for
/// each one — the caller is expected to flatten the initial-sync tree into
/// the same flat shape before processing, so a single code path can handle
/// both. See [`crate::client::sync_folder_hierarchy`].
#[derive(Debug, Clone, Deserialize)]
pub(crate) struct SyncFolder {
    pub id: String,
    /// Absent for folders that no longer have a name in the payload — should
    /// not normally happen for folders we care about, but guard against it
    /// rather than panicking.
    #[serde(default)]
    pub name: Option<String>,
    /// Parent folder id ("location"). Absent only for the account's
    /// absolute root (id `11`), which has no parent.
    #[serde(default)]
    pub l: Option<String>,
    /// Discriminates mail folders from calendars/contacts/tasks/etc.
    /// Absent for some system folders (Inbox, Trash, ...) that are
    /// nonetheless mail folders — do not treat an absent `view` as
    /// "not mail" on its own. See the phase 1 spec doc for the current
    /// filtering approach.
    #[serde(default)]
    pub view: Option<String>,
    #[serde(default)]
    pub folder: Vec<SyncFolder>,
}

#[derive(Debug, Deserialize)]
pub(crate) struct Deleted {
    /// Comma-separated ids, present regardless of `typed`. Logged for
    /// debugging when non-empty (see `sync_folder_hierarchy.rs`); prefer
    /// `folder` below to know which of these ids are actually folders.
    #[serde(default)]
    pub ids: Option<String>,
    /// Only present when the request was sent with `typed: 1`. Each entry's
    /// `ids` is a comma-separated list of deleted folder ids.
    #[serde(default)]
    pub folder: Vec<IdsBlock>,
}

#[derive(Debug, Deserialize)]
pub(crate) struct IdsBlock {
    pub ids: String,
}

impl IdsBlock {
    /// Splits the comma-separated id list into individual ids.
    pub(crate) fn split(&self) -> impl Iterator<Item = &str> {
        self.ids.split(',').map(str::trim).filter(|s| !s.is_empty())
    }
}

// --- GetMsgRequest / GetMsgResponse -----------------------------------------
//
// Validated empirically against a real Carbonio instance (see the phase 1
// spec doc): `GetMsgRequest` without `raw` returns Carbonio's *parsed*
// representation (structured subject/participants/decoded body parts, no
// raw MIME source). Sending `raw: 1` switches the response to the raw
// RFC822 MIME source in `m[0].content._content`, which is what's needed to
// write the message into Thunderbird's local store (the same way EWS/Graph
// fetch raw content rather than a parsed view).

#[derive(Debug, Serialize)]
pub(crate) struct GetMsgRequest {
    #[serde(rename = "_jsns")]
    pub jsns: &'static str,
    pub m: GetMsgSpec,
}

/// See [`AuthRequestBody`] for why this wrapping is needed.
#[derive(Debug, Serialize)]
pub(crate) struct GetMsgRequestBody {
    #[serde(rename = "GetMsgRequest")]
    pub get_msg_request: GetMsgRequest,
}

#[derive(Debug, Serialize)]
pub(crate) struct GetMsgSpec {
    pub id: String,
    /// Always sent as `1` by this client: see the module note above.
    pub raw: u8,
}

#[derive(Debug, Deserialize)]
pub(crate) struct GetMsgResponseBody {
    #[serde(rename = "GetMsgResponse")]
    pub get_msg_response: GetMsgResponse,
}

#[derive(Debug, Deserialize)]
pub(crate) struct GetMsgResponse {
    pub m: Vec<RawMessage>,
}

#[derive(Debug, Deserialize)]
pub(crate) struct RawMessage {
    pub id: String,
    /// Parent folder id ("location"), same convention as [`SyncFolder::l`].
    #[serde(default)]
    pub l: Option<String>,
    pub content: RawContent,
}

#[derive(Debug, Deserialize)]
pub(crate) struct RawContent {
    #[serde(rename = "_content")]
    pub content: String,
}

// --- SearchRequest / SearchResponse -----------------------------------------
//
// Validated empirically against a real Carbonio instance (see the phase 1
// spec doc): SearchRequest (as opposed to SyncRequest, which only gives raw
// ids) returns actual message metadata - subject, date, sender, flags, size -
// suitable for populating a folder's message list.
//
// Confirmed: `query: "inid:<folder id>"` reliably scopes results to a single
// folder by id, avoiding the ambiguity of `in:<name>` when folder names are
// duplicated (e.g. this account has two folders literally named "Archive"
// and "Archives"). Pagination is via `limit`/`offset`, with `more` in the
// response indicating whether further results exist beyond the current page.

#[derive(Debug, Serialize)]
pub(crate) struct SearchRequest {
    #[serde(rename = "_jsns")]
    pub jsns: &'static str,
    pub types: &'static str,
    pub query: String,
    pub limit: u32,
    pub offset: u32,
    #[serde(rename = "sortBy")]
    pub sort_by: &'static str,
}

/// See [`AuthRequestBody`] for why this wrapping is needed.
#[derive(Debug, Serialize)]
pub(crate) struct SearchRequestBody {
    #[serde(rename = "SearchRequest")]
    pub search_request: SearchRequest,
}

#[derive(Debug, Deserialize)]
pub(crate) struct SearchResponseBody {
    #[serde(rename = "SearchResponse")]
    pub search_response: SearchResponse,
}

#[derive(Debug, Deserialize)]
pub(crate) struct SearchResponse {
    #[serde(default)]
    pub m: Vec<SearchMessage>,
    /// Whether more results exist beyond the requested `limit`/`offset`.
    #[serde(default)]
    pub more: bool,
}

/// A single message summary as returned by `SearchRequest`.
///
/// Scope note: messages that are meeting invitations carry a large `inv`
/// field with full iCal data (timezones, recurrence rules...) - deliberately
/// not modeled here, since phase 1 only needs the fields common to every
/// message regardless of its kind. `serde` drops unknown fields by default,
/// so `inv` (and anything else not listed below) is simply ignored.
#[derive(Debug, Clone, Deserialize)]
pub(crate) struct SearchMessage {
    pub id: String,
    /// Subject. Optional defensively; not expected to actually be absent in
    /// practice.
    #[serde(default)]
    pub su: Option<String>,
    /// Date received, in milliseconds since epoch.
    pub d: i64,
    /// Flags string (e.g. contains `u` for unread; absent entirely for a
    /// read message - confirmed empirically). Not a fixed-width bitfield,
    /// each character is an independent flag - see
    /// `MessageSummary::is_read` for how this is interpreted.
    #[serde(default)]
    pub f: Option<String>,
    /// Size in bytes.
    #[serde(default)]
    pub s: Option<u64>,
    #[serde(default)]
    pub e: Vec<SearchParticipant>,
}

#[derive(Debug, Clone, Deserialize)]
pub(crate) struct SearchParticipant {
    /// Email address.
    #[serde(default)]
    pub a: Option<String>,
    /// Personal/display name.
    #[serde(default)]
    pub p: Option<String>,
    /// Participant type: `"f"` for from, `"t"`/`"c"`/`"b"` for to/cc/bcc
    /// (only `"f"` is used for phase 1's purposes).
    #[serde(default)]
    pub t: Option<String>,
}
