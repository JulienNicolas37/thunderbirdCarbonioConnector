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
//! Only folder hierarchy sync and message retrieval are implemented. There is
//! no XPCOM bridge yet (i.e. no `#[xpcom::xpcom(implement(...))]` type): that
//! requires an `ICarbonioClient` XPIDL interface, which is defined alongside
//! the C++ scaffold (`mailnews/protocols/carbonio/`). Once that interface
//! exists, `lib.rs` will grow a bridge type analogous to EWS's
//! `XpcomEwsBridge`, exposing the methods implemented in [`client`] to C++.
//!
//! Until then, this crate's logic can be reviewed and iterated on on its own,
//! and is unit-testable independently of the XPCOM bridge.

pub mod client;
pub mod error;
mod soap;
mod types;

pub use client::CarbonioClient;
pub use error::Error;
