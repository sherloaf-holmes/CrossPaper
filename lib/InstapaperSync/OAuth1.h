#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * Minimal OAuth 1.0a (HMAC-SHA1) signing for the Instapaper Full API.
 *
 * Pure C++ with no Arduino/ESP-IDF dependencies so it builds in the native
 * test suite and the simulator. SHA-1 is implemented here because mbedtls is
 * not available in the simulator build; it runs on stack buffers (no heap).
 */
namespace oauth1 {

constexpr size_t SHA1_DIGEST_SIZE = 20;

struct Sha1 {
  uint32_t state[5];
  uint64_t bitLength;
  uint8_t block[64];
  size_t blockLen;

  Sha1();
  void update(const uint8_t* data, size_t len);
  void update(std::string_view text) { update(reinterpret_cast<const uint8_t*>(text.data()), text.size()); }
  void finish(uint8_t out[SHA1_DIGEST_SIZE]);

 private:
  void processBlock(const uint8_t* chunk);
};

void hmacSha1(std::string_view key, std::string_view message, uint8_t out[SHA1_DIGEST_SIZE]);

std::string base64Encode(const uint8_t* data, size_t len);

// RFC 3986 percent-encoding: everything except ALPHA / DIGIT / "-" / "." / "_" / "~".
void appendPercentEncoded(std::string& out, std::string_view in);
std::string percentEncode(std::string_view in);

// Decodes application/x-www-form-urlencoded text ('+' is a space). Invalid
// escapes are kept literally.
std::string formDecode(std::string_view in);

struct Param {
  std::string key;
  std::string value;
};

// Splits "a=1&b=2" into decoded key/value pairs. Empty segments are skipped.
void parseFormEncoded(std::string_view body, std::vector<Param>& out);

// Re-encodes params as an RFC 3986 form body ("a=1&b=2"), in the given order.
std::string encodeFormBody(const std::vector<Param>& params);

struct Credentials {
  std::string_view consumerKey;
  std::string_view consumerSecret;
  std::string_view token;        // empty during the xAuth access_token exchange
  std::string_view tokenSecret;  // empty during the xAuth access_token exchange
};

// Signature base string per RFC 5849 §3.4.1: METHOD&url&sorted-params, where
// params include the body params and the oauth_* protocol params.
std::string signatureBaseString(std::string_view method, std::string_view url, std::vector<Param> params);

// Base64 HMAC-SHA1 of the base string with key "consumerSecret&tokenSecret".
std::string signature(std::string_view baseString, std::string_view consumerSecret, std::string_view tokenSecret);

// Builds the full "OAuth ..." Authorization header value for a request whose
// form-encoded body params are bodyParams (already decoded).
std::string authorizationHeader(std::string_view method, std::string_view url, const std::vector<Param>& bodyParams,
                                const Credentials& creds, std::string_view nonce, uint32_t timestamp);

}  // namespace oauth1
