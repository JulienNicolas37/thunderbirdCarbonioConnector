/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOCLIENT_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOCLIENT_H_

#include "nsID.h"

extern "C" {
// Instantiates a new ICarbonioClient with a Rust implementation
// (`NS_CreateCarbonioClient` in `rust/carbonio_xpcom/src/lib.rs`).
MOZ_EXPORT nsresult NS_CreateCarbonioClient(REFNSIID aIID, void** aResult);
}

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOCLIENT_H_
