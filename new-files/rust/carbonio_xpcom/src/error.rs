/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use nserror::nsresult;
use protocol_shared::error::ProtocolError;

/// Errors that can occur while talking to a Carbonio server.
///
/// This wraps [`ProtocolError`] (shared across HTTPS-based mailnews
/// protocols) and adds variants specific to Carbonio's SOAP-over-JSON
/// envelope, such as an application-level `Fault` in an otherwise
/// HTTP-200 response, which is a normal way for Carbonio to report errors
/// (e.g. invalid credentials, expired auth token, unknown folder id).
#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error(transparent)]
    Protocol(#[from] ProtocolError),

    /// The server responded with a `Body.Fault` element. `code` is
    /// Carbonio's error code (e.g. `"account.AUTH_EXPIRED"`), `message` is
    /// the human-readable reason.
    #[error("Carbonio returned a fault ({code}): {message}")]
    Fault { code: String, message: String },

    /// The response's JSON structure didn't match what we expected (e.g. a
    /// missing field, or a field of the wrong shape). `context` should
    /// describe which request/response this occurred for.
    #[error("unexpected response shape while processing {context}: {details}")]
    UnexpectedResponse { context: String, details: String },

    #[error("failed to (de)serialize JSON: {0}")]
    Json(#[from] serde_json::Error),

    /// The client was asked to perform an operation before it had ever
    /// successfully authenticated, and doesn't have credentials to do so on
    /// its own (should not normally happen; indicates a bug in the caller).
    #[error("client has no valid session and cannot authenticate")]
    NotAuthenticated,
}

/// `moz_http::Error` converts to [`ProtocolError`] on its own (it has a
/// direct `#[from]` there), but Rust's `?` operator only performs a single
/// `From` hop — so without this, `moz_http_call()?` inside a function
/// returning `Result<_, Error>` wouldn't compile. This makes the two-hop
/// conversion (`moz_http::Error` -> `ProtocolError` -> `Error`) explicit.
impl From<moz_http::Error> for Error {
    fn from(value: moz_http::Error) -> Self {
        Error::Protocol(value.into())
    }
}

/// Required so [`Error`] can be used as the `Err` type in
/// [`protocol_shared::client::DoOperation`] implementations, which need to
/// convert errors into an [`nsresult`] to report failure across XPCOM.
impl From<&Error> for nsresult {
    fn from(value: &Error) -> Self {
        match value {
            Error::Protocol(err) => err.into(),
            Error::Fault { .. } => nserror::NS_ERROR_FAILURE,
            Error::UnexpectedResponse { .. } | Error::Json(_) => nserror::NS_ERROR_UNEXPECTED,
            Error::NotAuthenticated => nserror::NS_ERROR_NOT_INITIALIZED,
        }
    }
}

impl From<Error> for nsresult {
    fn from(value: Error) -> Self {
        (&value).into()
    }
}

/// Required by the same trait bound as above, to let generic code recover
/// the underlying [`moz_http::Error`] when there is one (e.g. to inspect a
/// `WWW-Authenticate` header), without needing to know about every other
/// error variant.
impl<'a> TryFrom<&'a Error> for &'a moz_http::Error {
    type Error = ();

    fn try_from(value: &'a Error) -> std::result::Result<Self, Self::Error> {
        match value {
            Error::Protocol(err) => err.try_into(),
            _ => Err(()),
        }
    }
}

pub type Result<T> = std::result::Result<T, Error>;
