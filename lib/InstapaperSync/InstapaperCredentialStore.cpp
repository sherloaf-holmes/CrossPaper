#include "InstapaperCredentialStore.h"

#include <Logging.h>
#include <ObfuscationUtils.h>

namespace {
std::string readObfuscated(JsonVariantConst doc, const char* key) {
  obfuscation::DecodeStatus status = obfuscation::DecodeStatus::INVALID;
  std::string value = obfuscation::deobfuscateFromBase64(doc[key] | "", &status);
  if (status == obfuscation::DecodeStatus::INVALID) {
    LOG_ERR("IPS", "Ignoring unreadable %s", key);
    return "";
  }
  return value;
}
}  // namespace

void InstapaperCredentialStore::toJson(JsonDocument& doc) const {
  // Serialize fields directly: the getters lazy-load, and saveToFile() already
  // holds the store mutex.
  doc["consumerKey"] = consumerKey;
  doc["consumerSecret_obf"] = obfuscation::obfuscateToBase64(consumerSecret);
  doc["username"] = username;
  doc["token_obf"] = obfuscation::obfuscateToBase64(token);
  doc["tokenSecret_obf"] = obfuscation::obfuscateToBase64(tokenSecret);
}

bool InstapaperCredentialStore::fromJson(JsonVariantConst doc) {
  consumerKey = doc["consumerKey"] | "";
  consumerSecret = readObfuscated(doc, "consumerSecret_obf");
  username = doc["username"] | "";
  token = readObfuscated(doc, "token_obf");
  tokenSecret = readObfuscated(doc, "tokenSecret_obf");
  return true;
}

void InstapaperCredentialStore::setConsumer(const std::string& key, const std::string& secret) {
  ensureLoaded();
  // A different application cannot use tokens issued to the previous one.
  if (key != consumerKey) clearToken();
  consumerKey = key;
  consumerSecret = secret;
}

void InstapaperCredentialStore::setToken(const std::string& user, const std::string& oauthToken,
                                         const std::string& oauthTokenSecret) {
  ensureLoaded();
  username = user;
  token = oauthToken;
  tokenSecret = oauthTokenSecret;
}

void InstapaperCredentialStore::clearToken() {
  ensureLoaded();
  username.clear();
  token.clear();
  tokenSecret.clear();
}

bool InstapaperCredentialStore::hasConsumer() const {
  ensureLoaded();
  return !consumerKey.empty() && !consumerSecret.empty();
}

bool InstapaperCredentialStore::isLoggedIn() const {
  ensureLoaded();
  return hasConsumer() && !token.empty() && !tokenSecret.empty();
}
