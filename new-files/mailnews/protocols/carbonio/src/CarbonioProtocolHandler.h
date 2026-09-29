/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOPROTOCOLHANDLER_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOPROTOCOLHANDLER_H_

#include "nsIProtocolHandler.h"

/**
 * Registers the "x-moz-carbonio" scheme, used for channel URIs that a
 * docShell (or the I/O service) can load to stream a message's raw content
 * out of the local store. Modeled after EWS's `ExchangeProtocolHandler`,
 * trimmed to a single scheme (no Graph-style dual-scheme dispatch needed)
 * and no attachment/part-specific content-disposition logic (phase 1 scope).
 */
class CarbonioProtocolHandler : public nsIProtocolHandler {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPROTOCOLHANDLER

  CarbonioProtocolHandler();

 protected:
  virtual ~CarbonioProtocolHandler();
};

extern "C" {
nsresult NS_CreateCarbonioProtocolHandler(REFNSIID aIID, void** aResult);
}

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOPROTOCOLHANDLER_H_
