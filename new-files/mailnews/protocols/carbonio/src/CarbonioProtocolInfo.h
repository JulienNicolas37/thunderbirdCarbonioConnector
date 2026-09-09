/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOPROTOCOLINFO_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOPROTOCOLINFO_H_

#include "nsIMsgProtocolInfo.h"

/**
 * Modeled directly after `ExchangeProtocolInfo` - this interface is
 * protocol-agnostic enough that phase 1's needs don't differ from EWS's.
 */
class CarbonioProtocolInfo : public nsIMsgProtocolInfo {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMSGPROTOCOLINFO

  CarbonioProtocolInfo();

 protected:
  virtual ~CarbonioProtocolInfo();
};

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOPROTOCOLINFO_H_
