#pragma once

#include <expected>
#include <string>
#include "common.h"

namespace Roblox::Hba {

    struct KeyPair {
            std::string publicKeyBase64;
            std::vector<uint8_t> privateKey;
    };

    ApiResult<KeyPair> getOrCreateKeyPair(const std::string &cookie);

    // GET https://apis.roblox.com/hba-service/v1/getServerNonce
    ApiResult<std::string> fetchServerNonce(const std::string &cookie);

    // GET https://auth.roblox.com/v1/client-assertion/
    ApiResult<std::string> fetchClientAssertion(const std::string &cookie);

    // Build the full secureAuthenticationIntent JSON string
    ApiResult<std::string> buildSecureAuthIntent(const std::string &cookie);

    ApiResult<std::string>
    buildBoundAuthToken(const std::string &cookie, const std::string &url, const std::string &body = {});

} // namespace Roblox::Hba
