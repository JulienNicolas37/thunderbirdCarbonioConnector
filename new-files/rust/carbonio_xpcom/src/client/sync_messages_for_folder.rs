/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use crate::error::Result;
use crate::soap;
use crate::types::{SearchMessage, SearchRequest, SearchRequestBody, SearchResponseBody};

use super::CarbonioClient;

/// Safety cap on the number of pages fetched for a single folder, in case
/// `more` were to ever misbehave (e.g. a server bug reporting `more: true`
/// forever) - avoids an unbounded loop. 10,000 messages per folder (at 100
/// per page) should comfortably cover phase 1's real-world testing.
const MAX_PAGES: u32 = 100;
const PAGE_SIZE: u32 = 100;

/// A single message's metadata, as needed to populate a local folder's
/// message list (subject, sender, date, read state, size) - not the
/// message's content, which is fetched separately and on demand via
/// [`super::CarbonioClient::get_message`].
#[derive(Debug, Clone)]
pub struct MessageSummary {
    pub id: String,
    pub subject: String,
    /// Date received, in milliseconds since epoch (as given by Carbonio -
    /// left in this unit here; converting to whatever `nsIMsgDBHdr` expects
    /// is the C++ side's responsibility).
    pub date_ms: i64,
    pub from_address: Option<String>,
    pub from_display_name: Option<String>,
    pub is_read: bool,
    pub size: u64,
}

/// Result of a [`CarbonioClient::sync_messages_for_folder`] call.
pub struct MessageListResult {
    pub messages: Vec<MessageSummary>,
}

/// Fetches the full list of message summaries for a single folder.
///
/// Confirmed empirically (see the phase 1 spec doc): `query: "inid:<id>"`
/// reliably scopes results to a single folder by id. Paginates internally
/// using `limit`/`offset` until the server reports no more results (`more:
/// false`), so callers get the complete list in one call rather than having
/// to manage pagination themselves.
pub(super) async fn sync_messages_for_folder(
    client: &CarbonioClient,
    folder_id: &str,
) -> Result<MessageListResult> {
    let token = client.auth_token().await?;

    let mut messages = Vec::new();
    let mut offset = 0u32;

    for _ in 0..MAX_PAGES {
        let request = SearchRequestBody {
            search_request: SearchRequest {
                jsns: "urn:zimbraMail",
                types: "message",
                query: format!("inid:{folder_id}"),
                limit: PAGE_SIZE,
                offset,
                sort_by: "dateDesc",
            },
        };

        let response: SearchResponseBody =
            soap::send(&client.http_client, &client.endpoint, Some(&token), request).await?;

        let search_response = response.search_response;
        let page_len = search_response.m.len();

        messages.extend(search_response.m.into_iter().map(to_summary));

        if !search_response.more || page_len == 0 {
            break;
        }

        offset += PAGE_SIZE;
    }

    Ok(MessageListResult { messages })
}

fn to_summary(m: SearchMessage) -> MessageSummary {
    let from = m.e.iter().find(|p| p.t.as_deref() == Some("f"));

    MessageSummary {
        id: m.id,
        subject: m.su.unwrap_or_default(),
        date_ms: m.d,
        from_address: from.and_then(|p| p.a.clone()),
        from_display_name: from.and_then(|p| p.p.clone()),
        // Confirmed empirically: the flags string contains 'u' for an
        // unread message, and is either absent or lacks 'u' for a read one.
        is_read: !m.f.as_deref().unwrap_or("").contains('u'),
        size: m.s.unwrap_or(0),
    }
}
