#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>

/**
 * Instapaper API credentials on the SD card.
 *
 * Each user registers their own Instapaper API application (consumer key and
 * secret) and enters it through the web portal: publishing a shared key would
 * get it revoked. The account password is only used once for the xAuth token
 * exchange and is never stored; only the resulting OAuth token is kept.
 *
 * Secrets are XOR-obfuscated with the device MAC (see ObfuscationUtils), the
 * same scheme as koreader.json. Not encryption, just protection from casual
 * reading of the SD card.
 */
class InstapaperCredentialStore : public PersistableStore<InstapaperCredentialStore> {
 private:
  std::string consumerKey;
  std::string consumerSecret;
  std::string username;
  std::string token;
  std::string tokenSecret;

  InstapaperCredentialStore() = default;
  ~InstapaperCredentialStore() = default;

  friend class PersistableStore<InstapaperCredentialStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/instapaper.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  void setConsumer(const std::string& key, const std::string& secret);
  void setToken(const std::string& user, const std::string& oauthToken, const std::string& oauthTokenSecret);
  void clearToken();

  const std::string& getConsumerKey() const {
    ensureLoaded();
    return consumerKey;
  }
  const std::string& getConsumerSecret() const {
    ensureLoaded();
    return consumerSecret;
  }
  const std::string& getUsername() const {
    ensureLoaded();
    return username;
  }
  const std::string& getToken() const {
    ensureLoaded();
    return token;
  }
  const std::string& getTokenSecret() const {
    ensureLoaded();
    return tokenSecret;
  }

  bool hasConsumer() const;
  bool isLoggedIn() const;
};

#define INSTAPAPER_CREDS InstapaperCredentialStore::getInstance()
