#include "OAuth1.h"

#include <algorithm>
#include <cstring>

namespace oauth1 {

namespace {

constexpr uint32_t rotl(uint32_t value, int bits) { return (value << bits) | (value >> (32 - bits)); }

constexpr char BASE64_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
constexpr char HEX_DIGITS[] = "0123456789ABCDEF";

bool isUnreserved(unsigned char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
         c == '_' || c == '~';
}

int hexValue(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

Sha1::Sha1() : state{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u}, bitLength(0), blockLen(0) {}

void Sha1::processBlock(const uint8_t* chunk) {
  uint32_t w[80];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) | (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
           (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) | static_cast<uint32_t>(chunk[i * 4 + 3]);
  }
  for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
  for (int i = 0; i < 80; ++i) {
    uint32_t f;
    uint32_t k;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    const uint32_t temp = rotl(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rotl(b, 30);
    b = a;
    a = temp;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
}

void Sha1::update(const uint8_t* data, size_t len) {
  bitLength += static_cast<uint64_t>(len) * 8;
  while (len > 0) {
    const size_t take = std::min(len, sizeof(block) - blockLen);
    memcpy(block + blockLen, data, take);
    blockLen += take;
    data += take;
    len -= take;
    if (blockLen == sizeof(block)) {
      processBlock(block);
      blockLen = 0;
    }
  }
}

void Sha1::finish(uint8_t out[SHA1_DIGEST_SIZE]) {
  const uint64_t totalBits = bitLength;
  block[blockLen++] = 0x80;
  if (blockLen > 56) {
    memset(block + blockLen, 0, sizeof(block) - blockLen);
    processBlock(block);
    blockLen = 0;
  }
  memset(block + blockLen, 0, 56 - blockLen);
  for (int i = 0; i < 8; ++i) block[56 + i] = static_cast<uint8_t>(totalBits >> (56 - i * 8));
  processBlock(block);
  for (int i = 0; i < 5; ++i) {
    out[i * 4] = static_cast<uint8_t>(state[i] >> 24);
    out[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
    out[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
    out[i * 4 + 3] = static_cast<uint8_t>(state[i]);
  }
}

void hmacSha1(std::string_view key, std::string_view message, uint8_t out[SHA1_DIGEST_SIZE]) {
  uint8_t keyBlock[64] = {};
  if (key.size() > sizeof(keyBlock)) {
    Sha1 keyHash;
    keyHash.update(key);
    keyHash.finish(keyBlock);
  } else {
    memcpy(keyBlock, key.data(), key.size());
  }

  uint8_t pad[64];
  for (size_t i = 0; i < sizeof(pad); ++i) pad[i] = keyBlock[i] ^ 0x36;
  Sha1 inner;
  inner.update(pad, sizeof(pad));
  inner.update(message);
  uint8_t innerDigest[SHA1_DIGEST_SIZE];
  inner.finish(innerDigest);

  for (size_t i = 0; i < sizeof(pad); ++i) pad[i] = keyBlock[i] ^ 0x5c;
  Sha1 outer;
  outer.update(pad, sizeof(pad));
  outer.update(innerDigest, sizeof(innerDigest));
  outer.finish(out);
}

std::string base64Encode(const uint8_t* data, size_t len) {
  std::string out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  for (; i + 2 < len; i += 3) {
    const uint32_t n = (static_cast<uint32_t>(data[i]) << 16) | (static_cast<uint32_t>(data[i + 1]) << 8) | data[i + 2];
    out += BASE64_ALPHABET[(n >> 18) & 0x3F];
    out += BASE64_ALPHABET[(n >> 12) & 0x3F];
    out += BASE64_ALPHABET[(n >> 6) & 0x3F];
    out += BASE64_ALPHABET[n & 0x3F];
  }
  if (i < len) {
    uint32_t n = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) n |= static_cast<uint32_t>(data[i + 1]) << 8;
    out += BASE64_ALPHABET[(n >> 18) & 0x3F];
    out += BASE64_ALPHABET[(n >> 12) & 0x3F];
    out += (i + 1 < len) ? BASE64_ALPHABET[(n >> 6) & 0x3F] : '=';
    out += '=';
  }
  return out;
}

void appendPercentEncoded(std::string& out, std::string_view in) {
  for (const char ch : in) {
    const auto c = static_cast<unsigned char>(ch);
    if (isUnreserved(c)) {
      out += ch;
    } else {
      out += '%';
      out += HEX_DIGITS[c >> 4];
      out += HEX_DIGITS[c & 0x0F];
    }
  }
}

std::string percentEncode(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  appendPercentEncoded(out, in);
  return out;
}

std::string formDecode(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    const char c = in[i];
    if (c == '+') {
      out += ' ';
    } else if (c == '%' && i + 2 < in.size()) {
      const int hi = hexValue(in[i + 1]);
      const int lo = hexValue(in[i + 2]);
      if (hi < 0 || lo < 0) {
        out += c;
        continue;
      }
      out += static_cast<char>((hi << 4) | lo);
      i += 2;
    } else {
      out += c;
    }
  }
  return out;
}

void parseFormEncoded(std::string_view body, std::vector<Param>& out) {
  size_t start = 0;
  while (start <= body.size()) {
    size_t end = body.find('&', start);
    if (end == std::string_view::npos) end = body.size();
    const std::string_view segment = body.substr(start, end - start);
    if (!segment.empty()) {
      const size_t eq = segment.find('=');
      Param p;
      p.key = formDecode(segment.substr(0, eq));
      p.value = eq == std::string_view::npos ? std::string() : formDecode(segment.substr(eq + 1));
      out.push_back(std::move(p));
    }
    start = end + 1;
  }
}

std::string encodeFormBody(const std::vector<Param>& params) {
  std::string out;
  for (const Param& p : params) {
    if (!out.empty()) out += '&';
    appendPercentEncoded(out, p.key);
    out += '=';
    appendPercentEncoded(out, p.value);
  }
  return out;
}

std::string signatureBaseString(std::string_view method, std::string_view url, std::vector<Param> params) {
  // Sort by encoded key, then encoded value (RFC 5849 §3.4.1.3.2). Encoding
  // before sorting matters only for non-ASCII keys, which OAuth/Instapaper
  // never uses, so sorting the encoded forms directly is exact.
  for (Param& p : params) {
    p.key = percentEncode(p.key);
    p.value = percentEncode(p.value);
  }
  std::sort(params.begin(), params.end(), [](const Param& a, const Param& b) {
    return a.key == b.key ? a.value < b.value : a.key < b.key;
  });

  std::string normalized;
  for (const Param& p : params) {
    if (!normalized.empty()) normalized += '&';
    normalized += p.key;
    normalized += '=';
    normalized += p.value;
  }

  std::string base;
  base.reserve(method.size() + url.size() + normalized.size() * 3 / 2 + 8);
  for (const char c : method) base += static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
  base += '&';
  appendPercentEncoded(base, url);
  base += '&';
  appendPercentEncoded(base, normalized);
  return base;
}

std::string signature(std::string_view baseString, std::string_view consumerSecret, std::string_view tokenSecret) {
  std::string key = percentEncode(consumerSecret);
  key += '&';
  appendPercentEncoded(key, tokenSecret);
  uint8_t digest[SHA1_DIGEST_SIZE];
  hmacSha1(key, baseString, digest);
  return base64Encode(digest, sizeof(digest));
}

std::string authorizationHeader(std::string_view method, std::string_view url, const std::vector<Param>& bodyParams,
                                const Credentials& creds, std::string_view nonce, uint32_t timestamp) {
  const std::string timestampText = std::to_string(timestamp);

  std::vector<Param> oauthParams;
  oauthParams.reserve(6);
  oauthParams.push_back({"oauth_consumer_key", std::string(creds.consumerKey)});
  oauthParams.push_back({"oauth_nonce", std::string(nonce)});
  oauthParams.push_back({"oauth_signature_method", "HMAC-SHA1"});
  oauthParams.push_back({"oauth_timestamp", timestampText});
  if (!creds.token.empty()) oauthParams.push_back({"oauth_token", std::string(creds.token)});
  oauthParams.push_back({"oauth_version", "1.0"});

  std::vector<Param> all;
  all.reserve(oauthParams.size() + bodyParams.size());
  all.insert(all.end(), oauthParams.begin(), oauthParams.end());
  all.insert(all.end(), bodyParams.begin(), bodyParams.end());

  const std::string sig =
      signature(signatureBaseString(method, url, std::move(all)), creds.consumerSecret, creds.tokenSecret);

  std::string header = "OAuth ";
  for (const Param& p : oauthParams) {
    appendPercentEncoded(header, p.key);
    header += "=\"";
    appendPercentEncoded(header, p.value);
    header += "\", ";
  }
  header += "oauth_signature=\"";
  appendPercentEncoded(header, sig);
  header += '"';
  return header;
}

}  // namespace oauth1
