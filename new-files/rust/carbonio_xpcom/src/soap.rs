/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Low-level transport: building a SOAP-over-JSON envelope, sending it, and
//! decoding the response (including detecting an application-level
//! `Body.Fault`, which Carbonio can return with an HTTP 200 status).

use serde::{Serialize, de::DeserializeOwned};
use url::Url;

use moz_http::Client;

use crate::error::{Error, Result};
use crate::types::{Context, Envelope, Header, ResponseEnvelope};

/// Sends a single SOAP-over-JSON request and returns its decoded body.
///
/// `endpoint` should be the full URL to the server's SOAP endpoint (i.e.
/// `https://<host>/service/soap`, confirmed empirically — see the phase 1
/// spec doc). `auth_token`, if present, is embedded in
/// `Header.context.authToken`, which is how Carbonio expects it for the SOAP
/// API (as opposed to an HTTP header or a cookie, which is REST-API-only).
pub(crate) async fn send<Req, Res>(
    http_client: &Client,
    endpoint: &Url,
    auth_token: Option<&str>,
    request_body: Req,
) -> Result<Res>
where
    Req: Serialize,
    Res: DeserializeOwned,
{
    let envelope = Envelope {
        header: Header {
            context: Context {
                jsns: "urn:zimbra",
                auth_token: auth_token.map(str::to_string),
            },
        },
        body: request_body,
        jsns: "urn:zimbraSoap",
    };

    let payload = serde_json::to_string(&envelope)?;

    log::debug!("--> POST {endpoint} : {payload}");

    let response = http_client
        .post(endpoint)?
        .header("Content-Type", "application/json")
        .body(payload.as_str(), "application/json")
        .send()
        .await?;

    let status = response.status()?;
    let raw_body = response.body();

    log::debug!(
        "<-- HTTP {status} : {}",
        String::from_utf8_lossy(raw_body)
    );

    // Carbonio can return an application-level Fault with an HTTP 200, so we
    // always try to decode the body before checking the status code.
    let decoded: ResponseEnvelope<Res> =
        serde_json::from_slice(raw_body).map_err(|err| Error::UnexpectedResponse {
            context: format!("decoding response from {endpoint}"),
            details: err.to_string(),
        })?;

    if let Some(fault) = decoded.body.fault {
        let code = fault
            .detail
            .and_then(|d| d.error)
            .and_then(|e| e.code)
            .unwrap_or_else(|| "unknown".to_string());

        return Err(Error::Fault {
            code,
            message: fault.reason.text,
        });
    }

    match decoded.body.content {
        Some(content) => Ok(content),
        None => {
            // No Fault, but also no recognizable body content: either an
            // HTTP-level error with an unrelated body, or a response shape
            // we didn't anticipate.
            if status.is_client_error() || status.is_server_error() {
                Err(Error::UnexpectedResponse {
                    context: format!("request to {endpoint}"),
                    details: format!("HTTP {status} with no decodable Fault"),
                })
            } else {
                Err(Error::UnexpectedResponse {
                    context: format!("request to {endpoint}"),
                    details: "response body did not match the expected shape".to_string(),
                })
            }
        }
    }
}
