/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Validated empirically against a real Carbonio instance (see the phase 1
//! spec doc): `GetMsgRequest` sent with `raw: 1` returns the raw RFC822 MIME
//! source of the message, which is what's returned here.

use crate::error::{Error, Result};
use crate::soap;
use crate::types::{GetMsgRequest, GetMsgResponseBody, GetMsgSpec};

use super::CarbonioClient;

/// A single fetched message: its raw MIME source plus the metadata needed to
/// place it correctly in the local store.
#[derive(Debug)]
pub struct Message {
    pub id: String,
    /// Parent folder id. `None` should not normally happen for a message
    /// that still exists server-side; guard against it rather than
    /// panicking, same approach as [`super::FolderChange`].
    pub folder_id: Option<String>,
    /// Raw RFC822 MIME source, suitable for writing directly into
    /// Thunderbird's local message store.
    pub raw_mime: String,
}

/// Fetches the raw RFC822 MIME source of a single message.
pub(super) async fn get_message(client: &CarbonioClient, message_id: &str) -> Result<Message> {
    let token = client.auth_token().await?;

    let request = GetMsgRequest {
        jsns: "urn:zimbraMail",
        m: GetMsgSpec {
            id: message_id.to_string(),
            raw: 1,
        },
    };

    let response: GetMsgResponseBody =
        soap::send(&client.http_client, &client.endpoint, Some(&token), request).await?;

    let message = response
        .get_msg_response
        .m
        .into_iter()
        .next()
        .ok_or_else(|| Error::UnexpectedResponse {
            context: "GetMsgResponse".to_string(),
            details: "no message in response".to_string(),
        })?;

    Ok(Message {
        id: message.id,
        folder_id: message.l,
        raw_mime: message.content.content,
    })
}
