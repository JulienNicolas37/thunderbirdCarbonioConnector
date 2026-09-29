/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOOUTGOINGSERVER_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOOUTGOINGSERVER_H_

#include "nsID.h"

extern "C" {
// Instantiates a new nsIMsgOutgoingServer for Carbonio with a Rust
// implementation, that also implements IProtocolOutgoingServer.
//
// Phase 1: a shell implementation only, satisfying the XPCOM contract so
// account creation can complete - sending isn't implemented yet (SendMail
// errors with NS_ERROR_NOT_IMPLEMENTED). See docs/limitations-connues.md
// ("Hors périmètre phase 1") for the decision to send through Carbonio's
// native SOAP/REST API rather than through this outgoing-server path, once
// that's implemented.
MOZ_EXPORT nsresult nsCarbonioOutgoingServerConstructor(REFNSIID aIID,
                                                        void** aResult);
}  // extern "C"

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOOUTGOINGSERVER_H_
