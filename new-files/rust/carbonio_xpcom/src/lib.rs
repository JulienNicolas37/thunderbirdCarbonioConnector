/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

//! `carbonio_xpcom` — Rust engine for a native Thunderbird connector talking
//! to Carbonio's SOAP-over-JSON API (`urn:zimbraSoap`).
//!
//! This crate is modelled after `ews_xpcom`, and reuses the protocol-agnostic
//! building blocks provided by `protocol_shared` (safe XPCOM listener
//! wrappers, the generic `ProtocolClient`/`DoOperation` traits, HTTP
//! transport via `moz_http`).
//!
//! # Status (phase 1 — read-only)
//!
//! The XPCOM bridge below (`XpcomCarbonioBridge`, implementing
//! `ICarbonioClient`) connects [`client::CarbonioClient`] to
//! `CarbonioIncomingServer` (C++). Folder hierarchy sync, message list sync,
//! and single-message fetch are all wired up end to end.
//!
//! Still missing for a usable read path: a message display protocol/service
//! (mirroring EWS's `ExchangeService`/`ExchangeProtocolHandler`) so
//! double-clicking a message actually opens it - `getMessage` delivers the
//! raw MIME source correctly, but nothing in the C++ layer calls it yet.
//!
//! UNVERIFIED: unlike the rest of this crate, this bridge could not be
//! checked against the real `xpcom`/`moz_task` crates before being handed
//! over (no local Gecko toolchain) — expect a few rounds of real compiler
//! errors before this builds.

use std::cell::OnceCell;
use std::ffi::c_void;
use std::sync::Arc;

use nserror::{
    NS_ERROR_ALREADY_INITIALIZED, NS_ERROR_INVALID_ARG, NS_ERROR_NOT_INITIALIZED, NS_OK, nsresult,
};
use cstr::cstr;
use nsstring::{nsACString, nsCString, nsString};
use protocol_shared::{
    client::ProtocolClient,
    safe_xpcom::{SafeListener, SafeUri, SafeUrlListener},
};
use url::Url;
use xpcom::{
    RefPtr,
    interfaces::{
        ICarbonioFolderListener, ICarbonioMessageFetchListener, ICarbonioMessageListListener,
        nsIMsgIncomingServer, nsIStringInputStream, nsIURI, nsIUrlListener,
    },
    nsIID, xpcom_method,
};

use client::{CarbonioClient, FolderChange, MessageSummary};

pub mod client;
pub mod error;
mod soap;
mod types;

pub use error::Error;

/// Creates a new instance of the XPCOM/Carbonio bridge interface
/// [`XpcomCarbonioBridge`]. Referenced by `components.conf` under the
/// `@mozilla.org/messenger/carbonio-client;1` contract.
///
/// # SAFETY
/// `iid` must be a reference to a valid `nsIID` object, `result` must point
/// to valid memory, and `result` must not be used until the return value is
/// checked.
#[allow(non_snake_case)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn NS_CreateCarbonioClient(
    iid: &nsIID,
    result: *mut *mut c_void,
) -> nsresult {
    let instance = XpcomCarbonioBridge::allocate(InitXpcomCarbonioBridge {
        client: OnceCell::default(),
    });

    unsafe { instance.QueryInterface(iid, result) }
}

/// `XpcomCarbonioBridge` provides an XPCOM interface implementation for
/// mediating between the C++ consumer (`CarbonioIncomingServer`) and the
/// async Rust Carbonio client.
#[xpcom::xpcom(implement(ICarbonioClient), atomic)]
pub(crate) struct XpcomCarbonioBridge {
    client: OnceCell<Arc<CarbonioClient>>,
}

impl XpcomCarbonioBridge {
    xpcom_method!(running => GetRunning() -> bool);
    fn running(&self) -> Result<bool, nsresult> {
        Ok(self.client.get().is_some())
    }

    xpcom_method!(idle => GetIdle() -> bool);
    fn idle(&self) -> Result<bool, nsresult> {
        // Phase 1 has no persistent connection or background task to be
        // "idle" between - report idle whenever initialized, same as
        // running. Revisit once there's an actual notion of in-flight work
        // to distinguish (e.g. once biff/periodic sync exists).
        Ok(self.client.get().is_some())
    }

    xpcom_method!(initialize => Initialize(
        endpoint: *const nsACString,
        server: *const nsIMsgIncomingServer));
    fn initialize(
        &self,
        endpoint: &nsACString,
        server: &nsIMsgIncomingServer,
    ) -> Result<(), nsresult> {
        let endpoint_url = Url::parse(&endpoint.to_utf8()).or(Err(NS_ERROR_INVALID_ARG))?;

        // Read credentials directly from the incoming server for now. See
        // the TODO on `CarbonioClient` for why this may move to a
        // trait-based approach (mirroring `protocol_shared`'s
        // `AuthenticationProvider`) in a later phase.
        let mut username = nsCString::new();
        unsafe { server.GetUsername(&raw mut *username) }.to_result()?;

        // `GetPassword` alone only returns whatever's already cached (empty
        // in our case, since this account was set up by hand rather than
        // through a wizard that would have prompted for one) - it never
        // shows a dialog. `GetPasswordWithUI` is the scriptable method that
        // actually prompts the user and caches the result in the password
        // manager for next time.
        let mut password = nsString::new();
        let prompt_string = nsString::from(&format!(
            "Enter your Carbonio password for {}:",
            username.to_utf8()
        ));
        let prompt_title = nsString::from("Carbonio Account");
        unsafe {
            server.GetPasswordWithUI(&raw const *prompt_string, &raw const *prompt_title, &raw mut *password)
        }
        .to_result()?;

        let client = CarbonioClient::new(endpoint_url, username.to_string(), password.to_string());

        self.client
            .set(Arc::new(client))
            .or(Err(NS_ERROR_ALREADY_INITIALIZED))?;

        Ok(())
    }

    xpcom_method!(shutdown => Shutdown());
    fn shutdown(&self) -> Result<(), nsresult> {
        let client = self.client()?;
        moz_task::spawn_local("shutdown", async move {
            client.shutdown().await;
        })
        .detach();
        Ok(())
    }

    xpcom_method!(check_connectivity => CheckConnectivity(listener: *const nsIUrlListener) -> *const nsIURI);
    fn check_connectivity(&self, listener: &nsIUrlListener) -> Result<RefPtr<nsIURI>, nsresult> {
        let client = self.client()?;

        let uri = SafeUri::new(client.endpoint().to_string())?;
        let safe_listener = SafeUrlListener::new(listener);

        let task_uri = uri.clone();
        moz_task::spawn_local("check_connectivity", async move {
            match client.check_connectivity().await {
                Ok(()) => {
                    if let Err(err) = safe_listener.on_success(task_uri) {
                        log::warn!("check_connectivity success callback failed: {err}");
                    }
                }
                Err(err) => {
                    if let Err(cb_err) = safe_listener.on_failure(&err, task_uri) {
                        log::warn!("check_connectivity failure callback failed: {cb_err}");
                    }
                }
            }
        })
        .detach();

        Ok(uri.into())
    }

    xpcom_method!(sync_folder_hierarchy => SyncFolderHierarchy(
        listener: *const ICarbonioFolderListener,
        sync_state_token: *const nsACString));
    fn sync_folder_hierarchy(
        &self,
        listener: &ICarbonioFolderListener,
        sync_state_token: &nsACString,
    ) -> Result<(), nsresult> {
        let sync_state_token = if sync_state_token.is_empty() {
            None
        } else {
            Some(sync_state_token.to_utf8().into_owned())
        };

        log::info!(
            "sync_folder_hierarchy invoked (sync_state_token: {:?})",
            sync_state_token
        );

        let client = self.client()?;
        let listener = RefPtr::new(listener);

        moz_task::spawn_local("sync_folder_hierarchy", async move {
            deliver_folder_sync(client, listener, sync_state_token).await;
        })
        .detach();

        Ok(())
    }

    xpcom_method!(get_message => GetMessage(
        listener: *const ICarbonioMessageFetchListener,
        id: *const nsACString));
    fn get_message(
        &self,
        listener: &ICarbonioMessageFetchListener,
        id: &nsACString,
    ) -> Result<(), nsresult> {
        let client = self.client()?;
        let listener = RefPtr::new(listener);
        let message_id = id.to_utf8().into_owned();

        moz_task::spawn_local("get_message", async move {
            deliver_message_fetch(client, listener, message_id).await;
        })
        .detach();

        Ok(())
    }

    xpcom_method!(sync_messages_for_folder => SyncMessagesForFolder(
        listener: *const ICarbonioMessageListListener,
        folder_id: *const nsACString));
    fn sync_messages_for_folder(
        &self,
        listener: &ICarbonioMessageListListener,
        folder_id: &nsACString,
    ) -> Result<(), nsresult> {
        let folder_id = folder_id.to_utf8().into_owned();
        let client = self.client()?;
        let listener = RefPtr::new(listener);

        moz_task::spawn_local("sync_messages_for_folder", async move {
            deliver_message_list_sync(client, listener, folder_id).await;
        })
        .detach();

        Ok(())
    }

    /// Gets a clone of the client if initialized, or
    /// [`NS_ERROR_NOT_INITIALIZED`] otherwise.
    fn client(&self) -> Result<Arc<CarbonioClient>, nsresult> {
        self.client.get().cloned().ok_or(NS_ERROR_NOT_INITIALIZED)
    }
}

/// Drives a folder hierarchy sync to completion and reports the results
/// through `listener`, translating [`FolderChange`] entries into the
/// corresponding `ICarbonioFolderListener` calls.
async fn deliver_folder_sync(
    client: Arc<CarbonioClient>,
    listener: RefPtr<ICarbonioFolderListener>,
    sync_state_token: Option<String>,
) {
    let is_initial_sync = sync_state_token.is_none();

    let result = client.sync_folder_hierarchy(sync_state_token).await;

    let sync_result = match result {
        Ok(sync_result) => sync_result,
        Err(err) => {
            log::error!("folder hierarchy sync failed: {err}");
            let status: nsresult = (&err).into();
            report_xpcom_error(unsafe { listener.OnOperationFailure(status) }, "OnOperationFailure");
            return;
        }
    };

    log::info!(
        "folder hierarchy sync succeeded: {} change(s) (initial sync: {is_initial_sync})",
        sync_result.changes.len()
    );

    if is_initial_sync {
        // The mailbox root's Carbonio id is always "1" ("USER_ROOT") -
        // confirmed empirically, see the connector repo's phase 1 spec doc.
        // Unlike EWS, Carbonio doesn't hand us this id dynamically as part
        // of the sync response, so it's hardcoded here rather than read
        // from `sync_result`.
        let root_id = nsCString::from("1");
        report_xpcom_error(
            unsafe { listener.OnNewRootFolder(&*root_id) },
            "OnNewRootFolder",
        );
    }

    for change in sync_result.changes {
        match change {
            FolderChange::Upserted { id, parent_id, name, is_mail_folder } => {
                let id = nsCString::from(id);
                let parent_id = nsCString::from(parent_id.unwrap_or_default());
                let name = nsCString::from(name);
                report_xpcom_error(
                    unsafe {
                        listener.OnFolderUpserted(&*id, &*parent_id, &*name, is_mail_folder)
                    },
                    &format!("OnFolderUpserted(id={id}, parentId={parent_id}, name={name})"),
                );
            }
            FolderChange::Deleted { id } => {
                let id = nsCString::from(id);
                report_xpcom_error(unsafe { listener.OnFolderDeleted(&*id) }, "OnFolderDeleted");
            }
        }
    }

    if let Some(token) = sync_result.new_sync_token {
        let token = nsCString::from(token);
        report_xpcom_error(
            unsafe { listener.OnSyncStateTokenChanged(&*token) },
            "OnSyncStateTokenChanged",
        );
    }

    report_xpcom_error(unsafe { listener.OnSuccess() }, "OnSuccess");
}

/// Drives a single message fetch to completion and delivers its raw MIME
/// content to `listener` as an `nsIInputStream`, following the
/// start/data/stop callback sequence `ICarbonioMessageFetchListener`
/// expects.
async fn deliver_message_fetch(
    client: Arc<CarbonioClient>,
    listener: RefPtr<ICarbonioMessageFetchListener>,
    message_id: String,
) {
    log::info!("starting fetch for message {message_id}");
    report_xpcom_error(unsafe { listener.OnFetchStart() }, "OnFetchStart");

    let result = client.get_message(&message_id).await;

    let message = match result {
        Ok(message) => message,
        Err(err) => {
            log::error!("failed to fetch message {message_id}: {err}");
            let status: nsresult = (&err).into();
            report_xpcom_error(unsafe { listener.OnFetchStop(status) }, "OnFetchStop");
            return;
        }
    };

    log::info!(
        "fetched message {message_id}, handing {} byte(s) of MIME source to the listener",
        message.raw_mime.len()
    );

    match create_string_input_stream(&message.raw_mime) {
        Ok(stream) => {
            report_xpcom_error(
                unsafe { listener.OnFetchedDataAvailable(stream.coerce()) },
                "OnFetchedDataAvailable",
            );
            report_xpcom_error(unsafe { listener.OnFetchStop(NS_OK) }, "OnFetchStop");
            log::debug!("message {message_id} delivered successfully");
        }
        Err(status) => {
            log::error!("failed to create input stream for message {message_id}: {status}");
            report_xpcom_error(unsafe { listener.OnFetchStop(status) }, "OnFetchStop");
        }
    }
}

/// Wraps `data` in a freshly created `nsIStringInputStream`, ready to be
/// handed off as an `nsIInputStream` (e.g. via
/// `ICarbonioMessageFetchListener::OnFetchedDataAvailable`).
fn create_string_input_stream(data: &str) -> Result<RefPtr<nsIStringInputStream>, nsresult> {
    let stream = xpcom::create_instance::<nsIStringInputStream>(cstr!(
        "@mozilla.org/io/string-input-stream;1"
    ))
    .ok_or(nserror::NS_ERROR_FAILURE)?;

    let data = nsCString::from(data);
    unsafe { stream.SetUTF8Data(&*data) }.to_result()?;

    Ok(stream)
}

/// Drives a folder's message list sync to completion and reports the
/// results through `listener`, translating each [`MessageSummary`] into an
/// `ICarbonioMessageListListener::OnMessage` call.
async fn deliver_message_list_sync(
    client: Arc<CarbonioClient>,
    listener: RefPtr<ICarbonioMessageListListener>,
    folder_id: String,
) {
    let result = client.sync_messages_for_folder(&folder_id).await;

    let list_result = match result {
        Ok(list_result) => list_result,
        Err(err) => {
            log::error!("message list sync for folder {folder_id} failed: {err}");
            let status: nsresult = (&err).into();
            report_xpcom_error(unsafe { listener.OnOperationFailure(status) }, "OnOperationFailure");
            return;
        }
    };

    log::info!(
        "message list sync for folder {folder_id} succeeded: {} message(s)",
        list_result.messages.len()
    );

    for MessageSummary {
        id,
        subject,
        date_ms,
        from_address,
        from_display_name,
        is_read,
        size,
    } in list_result.messages
    {
        let id = nsCString::from(id);
        let subject = nsCString::from(subject);
        let from_address = nsCString::from(from_address.unwrap_or_default());
        let from_display_name = nsCString::from(from_display_name.unwrap_or_default());

        report_xpcom_error(
            unsafe {
                listener.OnMessage(
                    &*id,
                    &*subject,
                    date_ms,
                    &*from_address,
                    &*from_display_name,
                    is_read,
                    size,
                )
            },
            "OnMessage",
        );
    }

    report_xpcom_error(unsafe { listener.OnSuccess() }, "OnSuccess");
}

/// Logs a warning if an XPCOM call into a listener returned a failure.
/// Listener callbacks failing is generally not something we can act on
/// (there's no one further up the chain to propagate it to from an
/// already-detached async task), so this only logs rather than panicking or
/// silently ignoring it.
fn report_xpcom_error(status: nsresult, call_name: &str) {
    if let Err(err) = status.to_result() {
        log::warn!("{call_name} listener callback returned a failure: {err}");
    }
}
