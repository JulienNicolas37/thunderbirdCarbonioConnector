/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CarbonioProtocolHandler.h"

#include "CarbonioMessageChannel.h"

nsresult NS_CreateCarbonioProtocolHandler(REFNSIID aIID, void** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;
  RefPtr<CarbonioProtocolHandler> instance(new CarbonioProtocolHandler());
  return instance->QueryInterface(aIID, aResult);
}

NS_IMPL_ISUPPORTS(CarbonioProtocolHandler, nsIProtocolHandler)

CarbonioProtocolHandler::CarbonioProtocolHandler() = default;
CarbonioProtocolHandler::~CarbonioProtocolHandler() = default;

NS_IMETHODIMP CarbonioProtocolHandler::GetScheme(nsACString& aScheme) {
  aScheme.AssignLiteral("x-moz-carbonio");
  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolHandler::NewChannel(nsIURI* aURI,
                                                   nsILoadInfo* aLoadinfo,
                                                   nsIChannel** _retval) {
  RefPtr<CarbonioMessageChannel> channel = new CarbonioMessageChannel(aURI);
  MOZ_TRY(channel->SetLoadInfo(aLoadinfo));

  channel.forget(_retval);

  return NS_OK;
}

NS_IMETHODIMP CarbonioProtocolHandler::AllowPort(int32_t port,
                                                  const char* scheme,
                                                  bool* _retval) {
  // We control the entire lifetime of message URIs from creation to
  // loading, so we should never encounter a port we don't expect.
  MOZ_ASSERT_UNREACHABLE("call to AllowPort on internal protocol");

  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = false;

  return NS_OK;
}
