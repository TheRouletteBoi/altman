#include "hba.h"

#include <uECC.h>
#include <sodium.h>
#include <nlohmann/json.hpp>

#include "common.h"
#include "console/console.h"
#include "network/http.h"
#include "data.h"

#include <chrono>
#include <unordered_map>
#include <shared_mutex>

namespace Roblox::Hba {

    namespace {
        std::string base64Encode(const uint8_t *data, size_t len, bool urlSafe = false) {
            int variant = urlSafe ? sodium_base64_VARIANT_URLSAFE_NO_PADDING : sodium_base64_VARIANT_ORIGINAL;

            size_t encodedLen = sodium_base64_encoded_len(len, variant);
            std::string out(encodedLen, '\0');
            sodium_bin2base64(out.data(), encodedLen, data, len, variant);
            if (!out.empty() && out.back() == '\0') {
                out.pop_back();
            }
            return out;
        }

        std::vector<uint8_t> base64Decode(const std::string &input, bool urlSafe = false) {
            int variant = urlSafe ? sodium_base64_VARIANT_URLSAFE_NO_PADDING : sodium_base64_VARIANT_ORIGINAL;

            std::vector<uint8_t> out(input.size());
            size_t decodedLen = 0;
            if (sodium_base642bin(
                    out.data(),
                    out.size(),
                    input.c_str(),
                    input.size(),
                    nullptr,
                    &decodedLen,
                    nullptr,
                    variant
                )
                != 0) {
                return {};
            }
            out.resize(decodedLen);
            return out;
        }

        int uccRng(uint8_t *dest, unsigned size) {
            randombytes_buf(dest, size);
            return 1;
        }

        static const uint8_t kDerPrefix[]
            = {0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x02, 0x01,
               0x06, 0x08, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};

        std::string encodePublicKeyDer(const uint8_t *pubKey64) {
            std::vector<uint8_t> der;
            der.insert(der.end(), std::begin(kDerPrefix), std::end(kDerPrefix));
            der.push_back(0x04);
            der.insert(der.end(), pubKey64, pubKey64 + 64);
            return base64Encode(der.data(), der.size(), false);
        }

        struct CachedKeyPair {
                std::string publicKeyBase64;
                std::vector<uint8_t> privateKey;
        };

        std::shared_mutex g_keyMutex;
        std::unordered_map<std::string, CachedKeyPair> g_keyCache;

        void saveKeyPairToAccount(
            const std::string &cookie,
            const std::string &pubKeyB64,
            const std::vector<uint8_t> &privKey
        ) {
            std::string privB64 = base64Encode(privKey.data(), privKey.size(), false);
            std::string encryptedPriv = Data::encryptLocalData(privB64).value_or("");

            std::unique_lock lock(g_accountsMutex);
            for (auto &account: g_accounts) {
                if (account.cookie != cookie) {
                    continue;
                }
                account.hbaPublicKey = pubKeyB64;
                account.hbaPrivateKey = encryptedPriv;
                break;
            }

            Data::SaveAccounts();
        }

    } // namespace

    ApiResult<KeyPair> getOrCreateKeyPair(const std::string &cookie) {
        {
            std::shared_lock lock(g_keyMutex);
            auto it = g_keyCache.find(cookie);
            if (it != g_keyCache.end()) {
                return KeyPair {it->second.publicKeyBase64, it->second.privateKey};
            }
        }

        {
            std::shared_lock accLock(g_accountsMutex);
            for (const auto &account: g_accounts) {
                if (account.cookie != cookie) {
                    continue;
                }
                if (account.hbaPublicKey.empty() || account.hbaPrivateKey.empty()) {
                    break;
                }

                std::string privB64 = account.hbaPrivateKey;

                if (!privB64.empty()) {
                    auto rawKey = base64Decode(privB64, false);
                    if (rawKey.size() == 32) {
                        std::unique_lock keyLock(g_keyMutex);
                        g_keyCache[cookie] = {account.hbaPublicKey, rawKey};
                        return KeyPair {account.hbaPublicKey, rawKey};
                    }
                }
                break;
            }
        }

        uECC_set_rng(uccRng);
        const uECC_Curve curve = uECC_secp256r1();

        std::vector<uint8_t> privKey(32);
        uint8_t pubKey[64];

        if (!uECC_make_key(pubKey, privKey.data(), curve)) {
            LOG_ERROR("[HBA] Failed to generate P-256 keypair");
            return std::unexpected(ApiError::Unknown);
        }

        std::string pubKeyB64 = encodePublicKeyDer(pubKey);

        {
            std::unique_lock lock(g_keyMutex);
            g_keyCache[cookie] = {pubKeyB64, privKey};
        }

        saveKeyPairToAccount(cookie, pubKeyB64, privKey);

        return KeyPair {pubKeyB64, privKey};
    }

    ApiResult<std::string> fetchServerNonce(const std::string &cookie) {
        auto resp = HttpClient::get(
            "https://apis.roblox.com/hba-service/v1/getServerNonce",
            {
                {"Cookie",  ".ROBLOSECURITY=" + cookie},
                {"Accept",  "application/json"        },
                {"Origin",  "https://www.roblox.com"  },
                {"Referer", "https://www.roblox.com/" }
        }
        );

        if (resp.status_code < 200 || resp.status_code >= 300) {
            LOG_ERROR("[HBA] Failed to fetch server nonce: HTTP {}", resp.status_code);
            return std::unexpected(httpStatusToError(resp.status_code));
        }

        try {
            auto j = nlohmann::json::parse(resp.text);
            if (j.is_string()) {
                return j.get<std::string>();
            }
            return resp.text;
        } catch (...) {
            return resp.text;
        }
    }

    ApiResult<std::string> fetchClientAssertion(const std::string &cookie) {
        const std::string url = "https://auth.roblox.com/v1/client-assertion/";

        auto tokenResult = buildBoundAuthToken(cookie, url, "");
        if (!tokenResult) {
            LOG_ERROR("[HBA] Failed to build bound auth token for client assertion");
            return std::unexpected(tokenResult.error());
        }

        auto resp = HttpClient::get(
            url,
            {
                {"Cookie",             ".ROBLOSECURITY=" + cookie},
                {"Accept",             "application/json"        },
                {"Origin",             "https://www.roblox.com"  },
                {"Referer",            "https://www.roblox.com/" },
                {"x-bound-auth-token", *tokenResult           }
            }
        );

        if (resp.status_code < 200 || resp.status_code >= 300) {
            LOG_ERROR("[HBA] Failed to fetch client assertion: HTTP {}", resp.status_code);
            return std::unexpected(httpStatusToError(resp.status_code));
        }

        try {
            auto j = nlohmann::json::parse(resp.text);
            if (!j.contains("clientAssertion") || !j["clientAssertion"].is_string()) {
                return std::unexpected(ApiError::InvalidResponse);
            }
            return j["clientAssertion"].get<std::string>();
        } catch (...) {
            return std::unexpected(ApiError::ParseError);
        }
    }

    ApiResult<std::string> buildSecureAuthIntent(const std::string &cookie) {
        auto kpResult = getOrCreateKeyPair(cookie);
        if (!kpResult) {
            return std::unexpected(kpResult.error());
        }

        auto nonceResult = fetchServerNonce(cookie);
        if (!nonceResult) {
            return std::unexpected(nonceResult.error());
        }

        const auto &kp = *kpResult;
        const std::string &serverNonce = *nonceResult;

        int64_t timestamp
            = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                  .count();

        std::string timestampStr = std::to_string(timestamp);

        uint8_t hash[32];
        crypto_hash_sha256(hash, reinterpret_cast<const uint8_t *>(timestampStr.data()), timestampStr.size());

        uECC_set_rng(uccRng);
        const uECC_Curve curve = uECC_secp256r1();
        uint8_t sig[64];

        if (!uECC_sign(kp.privateKey.data(), hash, sizeof(hash), sig, curve)) {
            LOG_ERROR("[HBA] Failed to sign timestamp");
            return std::unexpected(ApiError::Unknown);
        }

        std::string sigBase64 = base64Encode(sig, sizeof(sig), false);

        nlohmann::json intent = {
            {"clientPublicKey",      kp.publicKeyBase64},
            {"clientEpochTimestamp", timestamp         },
            {"serverNonce",          serverNonce       },
            {"saiSignature",         sigBase64         }
        };

        return intent.dump();
    }

    ApiResult<std::string>
    buildBoundAuthToken(const std::string &cookie, const std::string &url, const std::string &body) {
        auto kpResult = getOrCreateKeyPair(cookie);
        if (!kpResult) {
            return std::unexpected(kpResult.error());
        }

        const auto &kp = *kpResult;

        int64_t timestamp
            = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                  .count();

        uint8_t bodyHash[32];
        crypto_hash_sha256(bodyHash, reinterpret_cast<const uint8_t *>(body.data()), body.size());

        std::string bodyHashB64 = base64Encode(bodyHash, sizeof(bodyHash), false);
        std::string timestampStr = std::to_string(timestamp);

        uECC_set_rng(uccRng);
        const uECC_Curve curve = uECC_secp256r1();

        uint8_t hashOfHash[32];
        crypto_hash_sha256(hashOfHash, bodyHash, sizeof(bodyHash));

        uint8_t sig1[64];
        if (!uECC_sign(kp.privateKey.data(), hashOfHash, sizeof(hashOfHash), sig1, curve)) {
            LOG_ERROR("[HBA] Failed to sign body hash");
            return std::unexpected(ApiError::Unknown);
        }

        uint8_t tsHash[32];
        crypto_hash_sha256(tsHash, reinterpret_cast<const uint8_t *>(timestampStr.data()), timestampStr.size());

        uint8_t sig2[64];
        if (!uECC_sign(kp.privateKey.data(), tsHash, sizeof(tsHash), sig2, curve)) {
            LOG_ERROR("[HBA] Failed to sign timestamp");
            return std::unexpected(ApiError::Unknown);
        }

        std::string token = std::format(
            "v1|{}|{}|{}|{}",
            bodyHashB64,
            timestampStr,
            base64Encode(sig1, sizeof(sig1), false),
            base64Encode(sig2, sizeof(sig2), false)
        );

        return token;
    }

} // namespace Roblox::Hba
