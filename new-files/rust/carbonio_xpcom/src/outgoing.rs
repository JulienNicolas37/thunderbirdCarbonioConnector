/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! Outgoing (sending) server for Carbonio - a phase 1 shell.
//!
//! Wires up `@mozilla.org/messenger/outgoing/server;1?type=carbonio` via
//! `protocol_shared::generic_outgoing::OutgoingServer` (see that module and
//! `patches/exchange/001-add-generic-outgoing-server-interface.patch` in the
//! connector project for why a protocol-neutral module was added rather than
//! reusing EWS's `IExchangeOutgoingServer`/`outgoing.rs`).
//!
//! This only exists to satisfy the XPCOM contract so account creation can
//! complete - [`CarbonioOutgoingClient::send_message`] always fails with
//! `NS_ERROR_NOT_IMPLEMENTED`. Sending is planned to go through Carbonio's
//! native SOAP/REST API instead of this nsIMsgOutgoingServer path - see
//! docs/limitations-connues.md, "Hors périmètre phase 1".

use std::ffi::c_void;
use std::sync::Arc;

use nserror::nsresult;
use protocol_shared::client::ProtocolClient;
use protocol_shared::error::ProtocolError;
use protocol_shared::generic_outgoing::OutgoingServer;
use protocol_shared::safe_xpcom::SafeMsgOutgoingListener;
use protocol_shared::safe_xpcom::uri::SafeUri;
use xpcom::{interfaces::nsIMsgOutgoingServer, nsIID};

/// A shell client for the Carbonio outgoing server. Satisfies
/// [`SendCapableClient`](protocol_shared::outgoing::SendCapableClient) so the
/// generic `OutgoingServer` machinery can be reused, but never actually sends
/// anything - see the module-level doc comment.
struct CarbonioOutgoingClient;

impl ProtocolClient for CarbonioOutgoingClient {
    async fn shutdown(self: Arc<Self>) {
        // Nothing to shut down: no connection, no background task, no state.
    }
}

impl protocol_shared::outgoing::SendCapableClient for CarbonioOutgoingClient {
    async fn send_message(
        self: Arc<Self>,
        _mime_content: String,
        _message_id: String,
        _should_request_dsn: bool,
        _bcc_recipients: Vec<protocol_shared::outgoing::OwnedMailbox>,
        listener: SafeMsgOutgoingListener,
        server_uri: SafeUri,
    ) {
        log::warn!(
            "CarbonioOutgoingClient::send_message called, but sending is not \
             implemented yet (phase 1 shell) - see docs/limitations-connues.md"
        );

        let err = ProtocolError::XpCom(nserror::NS_ERROR_NOT_IMPLEMENTED);
        if let Err(cb_err) = listener.on_failure(&err, (server_uri, None::<String>).into()) {
            log::warn!("send_message failure callback failed: {cb_err}");
        }
    }
}

#[unsafe(no_mangle)]
pub(crate) unsafe extern "C" fn nsCarbonioOutgoingServerConstructor(
    iid: &nsIID,
    result: *mut *mut c_void,
) -> nsresult {
    let instance_result = OutgoingServer::new(|_server| Ok(CarbonioOutgoingClient));

    match instance_result {
        Ok(instance) => unsafe { instance.QueryInterface(iid, result) },
        Err(rv) => rv,
    }
}
