/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use std::time::{Duration, Instant};

use crate::error::{Error, Result};
use crate::soap;
use crate::types::{AuthAccount, AuthRequest, AuthRequestBody, AuthResponseBody};

use super::{CarbonioClient, Session};

/// Safety margin subtracted from the server-provided token lifetime, so this
/// client proactively re-authenticates slightly before the token actually
/// expires rather than finding out via a failed request. Arbitrary but
/// generous given tokens live for ~48h (confirmed empirically); tune down if
/// this turns out to cause unnecessary re-logins in practice.
const EXPIRY_SAFETY_MARGIN: Duration = Duration::from_secs(60);

/// Returns a valid auth token for `client`, logging in first if there is no
/// cached session, or if the cached one is at/past its safety-margined
/// expiry.
///
/// TODO(carbonio-phase-1): Carbonio's refresh behavior at/near token expiry
/// hasn't been tested yet (see the phase 1 spec doc's "Statut" section) —
/// this always performs a full re-login with the account's password rather
/// than trying a lighter-weight refresh. Revisit if Carbonio turns out to
/// support one.
pub(super) async fn ensure_session(client: &CarbonioClient) -> Result<String> {
    {
        let session = client.session.lock().await;
        if let Some(session) = session.as_ref() {
            if Instant::now() < session.expires_at {
                return Ok(session.token.clone());
            }
        }
    }

    login(client).await
}

async fn login(client: &CarbonioClient) -> Result<String> {
    let mut session = client.session.lock().await;

    // Another task might have logged in already while we were waiting for
    // the lock; re-check before making a redundant network request.
    if let Some(session) = session.as_ref() {
        if Instant::now() < session.expires_at {
            return Ok(session.token.clone());
        }
    }

    let request = AuthRequestBody {
        auth_request: AuthRequest {
            jsns: "urn:zimbraAccount",
            csrf_token_secured: true,
            persist_auth_token_cookie: true,
            account: AuthAccount {
                by: "name",
                content: client.username.clone(),
            },
            password: client.password.clone(),
        },
    };

    // No auth token to send for the auth request itself.
    let response: AuthResponseBody =
        soap::send(&client.http_client, &client.endpoint, None, request).await?;

    let token = response
        .auth_response
        .auth_token
        .into_iter()
        .next()
        .ok_or_else(|| Error::UnexpectedResponse {
            context: "AuthResponse".to_string(),
            details: "missing authToken".to_string(),
        })?
        .content;

    let lifetime = Duration::from_millis(response.auth_response.lifetime);
    let expires_at = Instant::now() + lifetime.saturating_sub(EXPIRY_SAFETY_MARGIN);

    *session = Some(Session {
        token: token.clone(),
        expires_at,
    });

    Ok(token)
}
