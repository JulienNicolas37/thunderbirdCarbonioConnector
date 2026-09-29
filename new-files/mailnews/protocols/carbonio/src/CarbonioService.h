/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOSERVICE_H_
#define COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOSERVICE_H_

#include "nsIMsgMessageService.h"

/**
 * The Carbonio implementation of `nsIMsgMessageService` - the protocol-
 * agnostic, UI-facing entry point for message actions (displaying,
 * streaming, ...).
 *
 * Modeled after EWS's `ExchangeService`. `CopyMessage(s)`/
 * `SaveMessageToDisk`/`Search`/`StreamHeaders`/`IsMsgInMemCache` are explicit
 * `NS_ERROR_NOT_IMPLEMENTED` stubs (phase 1 is read-only display, not
 * copy/move/search).
 */
class CarbonioService : public nsIMsgMessageService,
                        public nsIMsgMessageFetchPartService {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIMSGMESSAGESERVICE
  NS_DECL_NSIMSGMESSAGEFETCHPARTSERVICE

  CarbonioService();

 protected:
  virtual ~CarbonioService();

 private:
  /**
   * Extracts the message key as a string from a message URI, expected in
   * the form: carbonio-message://{user}@{server}/{Path/To/Folder}#{MessageKey}
   */
  nsresult MsgKeyStringFromMessageURI(nsIURI* uri, nsACString& msgKey);

  /**
   * Extracts the message key and folder path from a URI used by a Carbonio
   * message channel, expected in the form:
   * x-moz-carbonio://{user}@{server}/{Path/To/Folder}/{MessageKey}
   */
  nsresult MsgKeyStringFromChannelURI(nsIURI* uri, nsACString& msgKey,
                                      nsACString& folderURIPath);

  // Retrieves the message header matching the provided URI (either form
  // above).
  nsresult MsgHdrFromUri(nsIURI* uri, nsIMsgDBHdr** _retval);

  // Opens a channel for the given (already "x-moz-carbonio") URI and starts
  // it, so its content streams to `streamListener`.
  nsresult FetchMessage(nsIURI* uri, nsIStreamListener* streamListener);
};

#endif  // COMM_MAILNEWS_PROTOCOLS_CARBONIO_SRC_CARBONIOSERVICE_H_
