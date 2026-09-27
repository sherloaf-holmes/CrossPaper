#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "OAuth1.h"

namespace {

std::string hex(const uint8_t* data, size_t len) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < len; ++i) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0x0F];
  }
  return out;
}

std::string sha1Hex(const std::string& input) {
  oauth1::Sha1 sha;
  sha.update(input);
  uint8_t digest[oauth1::SHA1_DIGEST_SIZE];
  sha.finish(digest);
  return hex(digest, sizeof(digest));
}

std::string hmacHex(const std::string& key, const std::string& message) {
  uint8_t digest[oauth1::SHA1_DIGEST_SIZE];
  oauth1::hmacSha1(key, message, digest);
  return hex(digest, sizeof(digest));
}

// Twitter's published OAuth 1.0a signing walkthrough.
const std::vector<oauth1::Param> kTwitterParams = {
    {"status", "Hello Ladies + Gentlemen, a signed OAuth request!"},
    {"include_entities", "true"},
    {"oauth_consumer_key", "xvz1evFS4wEEPTGEFPHBog"},
    {"oauth_nonce", "kYjzVBB8Y0ZFabxSWbWovY3uYSQ2pTgmZeNu2VS4cg"},
    {"oauth_signature_method", "HMAC-SHA1"},
    {"oauth_timestamp", "1318622958"},
    {"oauth_token", "370773112-GmHxMAgYyLbNEtIKZeRNFsMKPR9EyMZeS9weJAEb"},
    {"oauth_version", "1.0"},
};
constexpr char kTwitterUrl[] = "https://api.twitter.com/1.1/statuses/update.json";
constexpr char kTwitterConsumerSecret[] = "kAcSOqF21Fu85e7zjz7ZN2U4ZRhfV3WpwPAoE3Z7kBw";
constexpr char kTwitterTokenSecret[] = "LswwdoUaIvS8ltyTt5jkRh4J50vUPVVHtR2YPi5kE";

}  // namespace

TEST(InstapaperOAuth, Sha1KnownVectors) {
  EXPECT_EQ(sha1Hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  EXPECT_EQ(sha1Hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
  EXPECT_EQ(sha1Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  EXPECT_EQ(sha1Hex(std::string(1000000, 'a')), "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(InstapaperOAuth, Sha1IncrementalMatchesOneShot) {
  const std::string text = "The quick brown fox jumps over the lazy dog, repeatedly, across block boundaries.";
  oauth1::Sha1 sha;
  for (char c : text) sha.update(reinterpret_cast<const uint8_t*>(&c), 1);
  uint8_t digest[oauth1::SHA1_DIGEST_SIZE];
  sha.finish(digest);
  EXPECT_EQ(hex(digest, sizeof(digest)), sha1Hex(text));
}

TEST(InstapaperOAuth, HmacSha1Rfc2202) {
  EXPECT_EQ(hmacHex(std::string(20, '\x0b'), "Hi There"), "b617318655057264e28bc0b6fb378c8ef146be00");
  EXPECT_EQ(hmacHex("Jefe", "what do ya want for nothing?"), "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");
  EXPECT_EQ(hmacHex(std::string(80, '\xaa'), "Test Using Larger Than Block-Size Key - Hash Key First"),
            "aa4ae5e15272d00e95705637ce8a3b55ed402112");
}

TEST(InstapaperOAuth, Base64) {
  const auto enc = [](const std::string& s) {
    return oauth1::base64Encode(reinterpret_cast<const uint8_t*>(s.data()), s.size());
  };
  EXPECT_EQ(enc(""), "");
  EXPECT_EQ(enc("f"), "Zg==");
  EXPECT_EQ(enc("fo"), "Zm8=");
  EXPECT_EQ(enc("foo"), "Zm9v");
  EXPECT_EQ(enc("foobar"), "Zm9vYmFy");
}

TEST(InstapaperOAuth, PercentEncodingIsRfc3986) {
  EXPECT_EQ(oauth1::percentEncode("Ladies + Gentlemen"), "Ladies%20%2B%20Gentlemen");
  EXPECT_EQ(oauth1::percentEncode("An encoded string!"), "An%20encoded%20string%21");
  EXPECT_EQ(oauth1::percentEncode("Dogs, Cats & Mice"), "Dogs%2C%20Cats%20%26%20Mice");
  EXPECT_EQ(oauth1::percentEncode("-._~"), "-._~");
  EXPECT_EQ(oauth1::percentEncode("\xE2\x98\x83"), "%E2%98%83");
}

TEST(InstapaperOAuth, FormDecodeAndParse) {
  EXPECT_EQ(oauth1::formDecode("a+b%20c%2Bd"), "a b c+d");
  EXPECT_EQ(oauth1::formDecode("bad%zzescape%"), "bad%zzescape%");

  std::vector<oauth1::Param> params;
  oauth1::parseFormEncoded("oauth_token=abc%3D&oauth_token_secret=x+y&&flag", params);
  ASSERT_EQ(params.size(), 3u);
  EXPECT_EQ(params[0].key, "oauth_token");
  EXPECT_EQ(params[0].value, "abc=");
  EXPECT_EQ(params[1].key, "oauth_token_secret");
  EXPECT_EQ(params[1].value, "x y");
  EXPECT_EQ(params[2].key, "flag");
  EXPECT_EQ(params[2].value, "");
}

TEST(InstapaperOAuth, FormBodyRoundTrip) {
  const std::vector<oauth1::Param> params = {{"have", "1,2,3"}, {"folder_id", "unread"}, {"q", "a b&c"}};
  const std::string body = oauth1::encodeFormBody(params);
  EXPECT_EQ(body, "have=1%2C2%2C3&folder_id=unread&q=a%20b%26c");
  std::vector<oauth1::Param> decoded;
  oauth1::parseFormEncoded(body, decoded);
  ASSERT_EQ(decoded.size(), params.size());
  for (size_t i = 0; i < params.size(); ++i) {
    EXPECT_EQ(decoded[i].key, params[i].key);
    EXPECT_EQ(decoded[i].value, params[i].value);
  }
}

TEST(InstapaperOAuth, SignatureBaseStringMatchesReference) {
  EXPECT_EQ(oauth1::signatureBaseString("post", kTwitterUrl, kTwitterParams),
            "POST&https%3A%2F%2Fapi.twitter.com%2F1.1%2Fstatuses%2Fupdate.json&include_entities%3Dtrue%26oauth_"
            "consumer_key%3Dxvz1evFS4wEEPTGEFPHBog%26oauth_nonce%3DkYjzVBB8Y0ZFabxSWbWovY3uYSQ2pTgmZeNu2VS4cg%26oauth_"
            "signature_method%3DHMAC-SHA1%26oauth_timestamp%3D1318622958%26oauth_token%3D370773112-"
            "GmHxMAgYyLbNEtIKZeRNFsMKPR9EyMZeS9weJAEb%26oauth_version%3D1.0%26status%3DHello%2520Ladies%2520%252B%"
            "2520Gentlemen%252C%2520a%2520signed%2520OAuth%2520request%2521");
}

TEST(InstapaperOAuth, SignatureMatchesReference) {
  const std::string base = oauth1::signatureBaseString("POST", kTwitterUrl, kTwitterParams);
  EXPECT_EQ(oauth1::signature(base, kTwitterConsumerSecret, kTwitterTokenSecret), "hCtSmYh+iHYCEqBWrE7C7hYmtUk=");
}

TEST(InstapaperOAuth, AuthorizationHeaderSignsBodyParams) {
  const std::vector<oauth1::Param> body = {
      {"status", "Hello Ladies + Gentlemen, a signed OAuth request!"},
      {"include_entities", "true"},
  };
  const oauth1::Credentials creds{"xvz1evFS4wEEPTGEFPHBog", kTwitterConsumerSecret,
                                  "370773112-GmHxMAgYyLbNEtIKZeRNFsMKPR9EyMZeS9weJAEb", kTwitterTokenSecret};
  const std::string header = oauth1::authorizationHeader("POST", kTwitterUrl, body, creds,
                                                         "kYjzVBB8Y0ZFabxSWbWovY3uYSQ2pTgmZeNu2VS4cg", 1318622958);
  EXPECT_EQ(header.rfind("OAuth ", 0), 0u);
  EXPECT_NE(header.find("oauth_token=\"370773112-GmHxMAgYyLbNEtIKZeRNFsMKPR9EyMZeS9weJAEb\""), std::string::npos);
  EXPECT_NE(header.find("oauth_signature=\"hCtSmYh%2BiHYCEqBWrE7C7hYmtUk%3D\""), std::string::npos);
  // Body params are signed but not repeated in the header.
  EXPECT_EQ(header.find("status="), std::string::npos);
}

TEST(InstapaperOAuth, XAuthHeaderOmitsEmptyToken) {
  const std::vector<oauth1::Param> body = {{"x_auth_username", "reader@example.com"}, {"x_auth_mode", "client_auth"}};
  const oauth1::Credentials creds{"key", "secret", {}, {}};
  const std::string header =
      oauth1::authorizationHeader("POST", "https://www.instapaper.com/api/1/oauth/access_token", body, creds, "n", 1);
  EXPECT_EQ(header.find("oauth_token="), std::string::npos);
  EXPECT_NE(header.find("oauth_consumer_key=\"key\""), std::string::npos);
}
