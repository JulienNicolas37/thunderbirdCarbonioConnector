/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! UNVALIDATED: unlike `authenticate.rs` and `sync_folder_hierarchy.rs`,
//! this has not been checked against a real Carbonio server yet (we only
//! ran `carbonio_explorer.py auth/folders/sync`, never `message`). The
//! request shape below is a reasonable guess based on the documented
//! `GetMsgRequest` (`m: {id: ...}`), but the response shape — in particular
//! how to retrieve the raw MIME content, as opposed to Carbonio's parsed
//! representation — needs to be confirmed the same way folder sync was,
//! before this is trustworthy enough to build C++ integration on top of.
//!
//! Suggested next step: extend `carbonio_explorer.py`'s `message` command
//! usage to capture a real response, update
//! `docs/carbonio-thunderbird-connecteur-phase1-specification.md`, then
//! replace `serde_json::Value` in `types.rs`'s `GetMsgResponse` with a typed
//! struct and flesh this out to return the actual MIME bytes rather than raw
//! JSON.

use crate::error::Result;
use crate::soap;
use crate::types::{GetMsgRequest, GetMsgResponseBody, GetMsgSpec};

use super::CarbonioClient;

pub(super) async fn get_message(
    client: &CarbonioClient,
    message_id: &str,
) -> Result<serde_json::Value> {
    let token = client.auth_token().await?;

    let request = GetMsgRequest {
        jsns: "urn:zimbraMail",
        m: GetMsgSpec {
            id: message_id.to_string(),
        },
    };

    let response: GetMsgResponseBody =
        soap::send(&client.http_client, &client.endpoint, Some(&token), request).await?;

    response
        .get_msg_response
        .m
        .into_iter()
        .next()
        .ok_or_else(|| crate::error::Error::UnexpectedResponse {
            context: "GetMsgResponse".to_string(),
            details: "no message in response".to_string(),
        })
}
