#include "InstapaperClient.h"

#include <Arduino.h>
#include <Logging.h>
#ifdef SIMULATOR
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#else
#include <SecureHttpClient.h>
#endif

#include <cstdio>
#include <ctime>
#include <memory>
#include <vector>
#ifdef SIMULATOR
#include <random>
#endif

#include "InstapaperCredentialStore.h"
#include "OAuth1.h"

namespace {

constexpr char API_BASE[] = "https://www.instapaper.com/api/1/";
constexpr char ACCESS_TOKEN_METHOD[] = "oauth/access_token";

// Methods the web portal may relay. Everything else is refused so the device
// cannot be used as a general-purpose signer for the user's account.
constexpr const char* ALLOWED_METHODS[] = {
    "bookmarks/list", "bookmarks/get_text", "bookmarks/archive", "bookmarks/update_read_progress", "folders/list",
};

// Same floors as KOReaderSyncClient: the wolfSSL handshake needs working heap.
constexpr uint32_t MIN_FREE_HEAP_FOR_TLS = 35000;
constexpr uint32_t MIN_MAX_ALLOC_HEAP_FOR_TLS = 20000;
constexpr uint32_t HTTP_TIMEOUT_MS = 30000;
// The xAuth reply is one short form-encoded line.
constexpr size_t MAX_TOKEN_REPLY = 1024;

bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_HEAP_FOR_TLS || maxAllocHeap < MIN_MAX_ALLOC_HEAP_FOR_TLS) {
    LOG_ERR("IPS", "Insufficient heap for TLS: %u free (need %u), %u max alloc (need %u)", freeHeap,
            MIN_FREE_HEAP_FOR_TLS, maxAllocHeap, MIN_MAX_ALLOC_HEAP_FOR_TLS);
    return true;
  }
  return false;
}

uint32_t randomWord() {
#ifdef SIMULATOR
  static std::random_device device;
  return device();
#else
  return esp_random();
#endif
}

std::string makeNonce() {
  char nonce[17];
  snprintf(nonce, sizeof(nonce), "%08lx%08lx", static_cast<unsigned long>(randomWord()),
           static_cast<unsigned long>(randomWord()));
  return nonce;
}

uint32_t resolveTimestamp(uint32_t timestamp) {
  if (timestamp != 0) return timestamp;
  return static_cast<uint32_t>(time(nullptr));
}

const char* fallbackContentType(std::string_view method) {
  return method == "bookmarks/get_text" ? "text/html; charset=utf-8" : "application/json";
}

InstapaperClient::Error signedPost(std::string_view method, const std::vector<oauth1::Param>& params,
                                   const oauth1::Credentials& creds, uint32_t timestamp,
                                   const InstapaperClient::ResponseSink& sink) {
  if (insufficientHeap()) return InstapaperClient::LOW_MEMORY;

  std::string url = API_BASE;
  url.append(method.data(), method.size());
  const std::string body = oauth1::encodeFormBody(params);
  const std::string authorization =
      oauth1::authorizationHeader("POST", url, params, creds, makeNonce(), resolveTimestamp(timestamp));

  LOG_DBG("IPS", "POST %s (%u body bytes, heap %u)", url.c_str(), static_cast<unsigned>(body.size()),
          static_cast<unsigned>(ESP.getFreeHeap()));

#ifdef SIMULATOR
  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  http.begin(secureClient, url.c_str());
  http.addHeader("Authorization", authorization.c_str());
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  const int status = http.POST(String(body.c_str()));
  if (status <= 0) {
    LOG_ERR("IPS", "Request failed: %d", status);
    http.end();
    return InstapaperClient::NETWORK_ERROR;
  }
  const String response = http.getString();
  http.end();
  if (!sink.begin(sink.context, status, fallbackContentType(method))) return InstapaperClient::NETWORK_ERROR;
  if (response.length() > 0 &&
      !sink.write(sink.context, reinterpret_cast<const uint8_t*>(response.c_str()), response.length())) {
    return InstapaperClient::NETWORK_ERROR;
  }
  return InstapaperClient::OK;
#else
  freeink::SecureHttpClient http;
  // SecureNet does not expose a CA bundle yet; same trade-off as KOSync/OPDS.
  http.setInsecure();
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setReuse(false);
  if (!http.begin(url)) {
    LOG_ERR("IPS", "Bad URL: %s", url.c_str());
    return InstapaperClient::NETWORK_ERROR;
  }
  http.setUserAgent("CrossInk-ESP32-" CROSSINK_VERSION);
  http.addHeader("Authorization", authorization);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  // SecureHttpClient only sends Content-Length for non-empty bodies; POST
  // without a length is rejected by some front ends.
  if (body.empty()) http.addHeader("Content-Length", "0");

  struct StreamState {
    freeink::SecureHttpClient* http;
    const InstapaperClient::ResponseSink* sink;
    std::string_view method;
    bool started;
  } state{&http, &sink, method, false};

  const int status = http.sendRequest(
      "POST", reinterpret_cast<const uint8_t*>(body.data()), body.size(),
      [&state](const uint8_t* data, size_t len) {
        if (!state.started) {
          state.started = true;
          const std::string contentType = state.http->getHeader("content-type");
          if (!state.sink->begin(state.sink->context, state.http->getStatus(),
                                 contentType.empty() ? fallbackContentType(state.method) : contentType.c_str())) {
            return false;
          }
        }
        return state.sink->write(state.sink->context, data, len);
      });
  if (status <= 0) {
    LOG_ERR("IPS", "Request failed (heap %u)", static_cast<unsigned>(ESP.getFreeHeap()));
    http.end();
    return InstapaperClient::NETWORK_ERROR;
  }
  if (!state.started) {
    const std::string contentType = http.getHeader("content-type");
    state.started = true;
    if (!sink.begin(sink.context, status, contentType.empty() ? fallbackContentType(method) : contentType.c_str())) {
      http.end();
      return InstapaperClient::NETWORK_ERROR;
    }
  }
  const bool complete = http.responseComplete() && !http.callbackAborted();
  http.end();
  if (!complete) {
    LOG_ERR("IPS", "Response incomplete for %.*s", static_cast<int>(method.size()), method.data());
    return InstapaperClient::NETWORK_ERROR;
  }
  return InstapaperClient::OK;
#endif
}

struct TokenReply {
  int status = 0;
  std::string body;
};

bool tokenReplyBegin(void* context, int httpStatus, const char*) {
  static_cast<TokenReply*>(context)->status = httpStatus;
  return true;
}

bool tokenReplyWrite(void* context, const uint8_t* data, size_t len) {
  auto* reply = static_cast<TokenReply*>(context);
  if (reply->body.size() + len > MAX_TOKEN_REPLY) return false;
  reply->body.append(reinterpret_cast<const char*>(data), len);
  return true;
}

}  // namespace

bool InstapaperClient::isAllowedMethod(std::string_view method) {
  for (const char* allowed : ALLOWED_METHODS) {
    if (method == allowed) return true;
  }
  return false;
}

InstapaperClient::Error InstapaperClient::login(const std::string& username, const std::string& password,
                                                uint32_t timestamp) {
  if (!INSTAPAPER_CREDS.hasConsumer()) return NOT_CONFIGURED;

  std::vector<oauth1::Param> params;
  params.reserve(3);
  params.push_back({"x_auth_username", username});
  // Instapaper accounts may have no password; the API accepts any value then.
  params.push_back({"x_auth_password", password});
  params.push_back({"x_auth_mode", "client_auth"});

  const oauth1::Credentials creds{INSTAPAPER_CREDS.getConsumerKey(), INSTAPAPER_CREDS.getConsumerSecret(), {}, {}};
  TokenReply reply;
  const ResponseSink sink{&reply, tokenReplyBegin, tokenReplyWrite};
  const Error err = signedPost(ACCESS_TOKEN_METHOD, params, creds, timestamp, sink);
  if (err != OK) return err;

  if (reply.status == 401 || reply.status == 403) {
    LOG_ERR("IPS", "xAuth rejected (%d)", reply.status);
    return AUTH_FAILED;
  }
  if (reply.status != 200) {
    LOG_ERR("IPS", "xAuth unexpected status %d", reply.status);
    return BAD_RESPONSE;
  }

  std::vector<oauth1::Param> fields;
  oauth1::parseFormEncoded(reply.body, fields);
  std::string token;
  std::string tokenSecret;
  for (const oauth1::Param& field : fields) {
    if (field.key == "oauth_token") token = field.value;
    if (field.key == "oauth_token_secret") tokenSecret = field.value;
  }
  if (token.empty() || tokenSecret.empty()) {
    LOG_ERR("IPS", "xAuth reply missing token");
    return BAD_RESPONSE;
  }

  INSTAPAPER_CREDS.setToken(username, token, tokenSecret);
  if (!INSTAPAPER_CREDS.saveToFile()) {
    LOG_ERR("IPS", "Failed to save Instapaper token");
  }
  LOG_INF("IPS", "Logged in to Instapaper");
  return OK;
}

InstapaperClient::Error InstapaperClient::call(std::string_view method, std::string_view formBody, uint32_t timestamp,
                                               const ResponseSink& sink) {
  if (!isAllowedMethod(method)) return METHOD_NOT_ALLOWED;
  if (!INSTAPAPER_CREDS.hasConsumer()) return NOT_CONFIGURED;
  if (!INSTAPAPER_CREDS.isLoggedIn()) return NOT_LOGGED_IN;

  std::vector<oauth1::Param> params;
  oauth1::parseFormEncoded(formBody, params);
  const oauth1::Credentials creds{INSTAPAPER_CREDS.getConsumerKey(), INSTAPAPER_CREDS.getConsumerSecret(),
                                  INSTAPAPER_CREDS.getToken(), INSTAPAPER_CREDS.getTokenSecret()};
  return signedPost(method, params, creds, timestamp, sink);
}

const char* InstapaperClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "OK";
    case NOT_CONFIGURED:
      return "Instapaper API key not configured";
    case NOT_LOGGED_IN:
      return "Not logged in to Instapaper";
    case METHOD_NOT_ALLOWED:
      return "Method not allowed";
    case LOW_MEMORY:
      return "Device is low on memory";
    case NETWORK_ERROR:
      return "Network error talking to Instapaper";
    case AUTH_FAILED:
      return "Instapaper rejected the username or password";
    case BAD_RESPONSE:
      return "Unexpected response from Instapaper";
  }
  return "Unknown error";
}
