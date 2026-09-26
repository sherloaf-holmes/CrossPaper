#include "InstapaperWebApi.h"

#include <ArduinoJson.h>
#ifdef SIMULATOR
#include <ArduinoJsonStringCompat.h>
#endif
#include <FsHelpers.h>
#include <HalStorage.h>
#include <InstapaperClient.h>
#include <InstapaperCredentialStore.h>
#include <Logging.h>
#include <WebServer.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "HttpDownloader.h"
#include "InstapaperArticleStore.h"
#include "RecentBooksStore.h"
#include "activities/home/BookActions.h"

namespace {

// Articles are small; this only guards against a runaway or hostile image URL.
constexpr size_t MAX_IMAGE_BYTES = 1536 * 1024;

void sendJsonError(WebServer& server, int status, const char* message) {
  JsonDocument doc;
  doc["error"] = message;
  String body;
  serializeJson(doc, body);
  server.send(status, "application/json", body);
}

int statusForClientError(InstapaperClient::Error error) {
  switch (error) {
    case InstapaperClient::OK:
      return 200;
    case InstapaperClient::NOT_CONFIGURED:
    case InstapaperClient::NOT_LOGGED_IN:
    case InstapaperClient::AUTH_FAILED:
      return 401;
    case InstapaperClient::METHOD_NOT_ALLOWED:
      return 403;
    case InstapaperClient::LOW_MEMORY:
      return 503;
    case InstapaperClient::NETWORK_ERROR:
    case InstapaperClient::BAD_RESPONSE:
      return 502;
  }
  return 500;
}

// Instapaper needs internet access, which hotspot mode does not give the browser.
bool rejectInApMode(WebServer& server, bool apMode) {
  if (!apMode) return false;
  sendJsonError(server, 409, "Join a Wi-Fi network to sync with Instapaper (hotspot mode has no internet)");
  return true;
}

bool parseJsonBody(WebServer& server, JsonDocument& doc) {
  if (!server.hasArg("plain")) {
    sendJsonError(server, 400, "Missing JSON body");
    return false;
  }
  const String body = server.arg("plain");
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    sendJsonError(server, 400, "Invalid JSON");
    return false;
  }
  return true;
}

uint32_t timestampArg(WebServer& server) {
  if (!server.hasArg("ts")) return 0;
  return static_cast<uint32_t>(strtoul(server.arg("ts").c_str(), nullptr, 10));
}

void handleStatus(WebServer& server, bool apMode) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");

  JsonDocument doc;
  char output[768];
  doc["apMode"] = apMode;
  doc["hasConsumerKey"] = INSTAPAPER_CREDS.hasConsumer();
  doc["consumerKey"] = INSTAPAPER_CREDS.getConsumerKey();
  doc["loggedIn"] = INSTAPAPER_CREDS.isLoggedIn();
  doc["username"] = INSTAPAPER_CREDS.getUsername();
  doc["maxArticles"] = InstapaperArticleStore::MAX_ARTICLES;
  size_t written = serializeJson(doc, output, sizeof(output));
  // Reopen the object so the article array can be streamed entry by entry
  // instead of building one large document in RAM.
  if (written == 0 || written >= sizeof(output)) {
    server.sendContent("{\"articles\":[]}");
    server.sendContent("");
    return;
  }
  output[written - 1] = ',';
  server.sendContent(output, written);
  server.sendContent("\"articles\":[");

  bool first = true;
  for (const InstapaperArticle& article : INSTAPAPER_ARTICLES.getArticles()) {
    doc.clear();
    doc["id"] = article.id;
    doc["title"] = article.title;
    doc["site"] = article.site;
    doc["savedAt"] = article.savedAt;
    doc["path"] = article.path;
    const bool exists = Storage.exists(article.path.c_str());
    const InstapaperReadingState reading = exists ? loadInstapaperReadingState(article.path) : InstapaperReadingState{};
    doc["missing"] = !exists;
    doc["finished"] = reading.finished;
    doc["percent"] = reading.percent;

    written = serializeJson(doc, output, sizeof(output));
    if (written >= sizeof(output)) {
      LOG_ERR("IPW", "Skipping oversized article entry %u", static_cast<unsigned>(article.id));
      continue;
    }
    if (!first) server.sendContent(",");
    server.sendContent(output, written);
    first = false;
  }
  server.sendContent("]}");
  server.sendContent("");
}

void handleConfig(WebServer& server) {
  JsonDocument doc;
  if (!parseJsonBody(server, doc)) return;

  const std::string key = doc["consumerKey"] | "";
  if (key.empty()) {
    sendJsonError(server, 400, "Consumer key is required");
    return;
  }
  // As with OPDS/Wi-Fi passwords, an absent secret keeps the stored one.
  const bool hasSecret = doc["consumerSecret"].is<const char*>();
  const std::string secret = hasSecret ? std::string(doc["consumerSecret"] | "") : INSTAPAPER_CREDS.getConsumerSecret();
  if (secret.empty()) {
    sendJsonError(server, 400, "Consumer secret is required");
    return;
  }

  INSTAPAPER_CREDS.setConsumer(key, secret);
  if (!INSTAPAPER_CREDS.saveToFile()) {
    sendJsonError(server, 500, "Failed to save credentials");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLogin(WebServer& server, bool apMode) {
  if (rejectInApMode(server, apMode)) return;
  JsonDocument doc;
  if (!parseJsonBody(server, doc)) return;

  const std::string username = doc["username"] | "";
  const std::string password = doc["password"] | "";
  const uint32_t ts = doc["ts"] | 0u;
  if (username.empty()) {
    sendJsonError(server, 400, "Username is required");
    return;
  }

  const InstapaperClient::Error err = InstapaperClient::login(username, password, ts);
  if (err != InstapaperClient::OK) {
    sendJsonError(server, statusForClientError(err), InstapaperClient::errorString(err));
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleLogout(WebServer& server) {
  INSTAPAPER_CREDS.clearToken();
  if (!INSTAPAPER_CREDS.saveToFile()) {
    sendJsonError(server, 500, "Failed to save credentials");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

struct RelayContext {
  WebServer* server;
  bool started;
};

bool relayBegin(void* context, int httpStatus, const char* contentType) {
  auto& relay = *static_cast<RelayContext*>(context);
  relay.started = true;
  // Lets the page tell Instapaper's own error replies apart from device errors.
  relay.server->sendHeader("X-Instapaper-Relay", "1");
  relay.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  relay.server->send(httpStatus, contentType, "");
  return true;
}

bool relayWrite(void* context, const uint8_t* data, size_t len) {
  WebServer& server = *static_cast<RelayContext*>(context)->server;
  server.sendContent(reinterpret_cast<const char*>(data), len);
  return static_cast<bool>(server.client().connected());
}

// Body is the form-encoded Instapaper parameters, sent as text/plain so the
// WebServer keeps it verbatim in arg("plain") instead of parsing it.
void handleCall(WebServer& server, bool apMode) {
  if (rejectInApMode(server, apMode)) return;
  const String method = server.arg("m");
  if (!InstapaperClient::isAllowedMethod(method.c_str())) {
    sendJsonError(server, 403, "Method not allowed");
    return;
  }

  const String body = server.hasArg("plain") ? server.arg("plain") : String();
  RelayContext relay{&server, false};
  const InstapaperClient::ResponseSink sink{&relay, relayBegin, relayWrite};
  const InstapaperClient::Error err = InstapaperClient::call(
      method.c_str(), std::string_view(body.c_str(), body.length()), timestampArg(server), sink);
  if (relay.started) {
    if (err != InstapaperClient::OK) {
      // Headers are already out; drop the connection so the page sees a failed
      // fetch rather than a silently truncated body.
      server.client().stop();
      return;
    }
    server.sendContent("");
    return;
  }
  sendJsonError(server, statusForClientError(err), InstapaperClient::errorString(err));
}

void handleImage(WebServer& server, bool apMode) {
  if (rejectInApMode(server, apMode)) return;
  const String url = server.arg("url");
  if (!url.startsWith("https://") && !url.startsWith("http://")) {
    sendJsonError(server, 400, "Only http(s) image URLs are allowed");
    return;
  }

  struct ImageRelay {
    WebServer* server;
    size_t sent;
    bool started;
  } relay{&server, 0, false};

  HttpDownloader::DownloadOptions options(false, false, nullptr, 0, HttpDownloader::Transport::WOLFSSL);
  const HttpDownloader::DownloadError err = HttpDownloader::streamUrl(
      url.c_str(),
      [&relay](const uint8_t* data, size_t len) {
        if (relay.sent + len > MAX_IMAGE_BYTES) {
          LOG_ERR("IPW", "Image exceeds %u bytes", static_cast<unsigned>(MAX_IMAGE_BYTES));
          return false;
        }
        if (!relay.started) {
          relay.started = true;
          // The browser sniffs the real format when decoding.
          relay.server->sendHeader("Cache-Control", "no-store");
          relay.server->setContentLength(CONTENT_LENGTH_UNKNOWN);
          relay.server->send(200, "application/octet-stream", "");
        }
        relay.server->sendContent(reinterpret_cast<const char*>(data), len);
        relay.sent += len;
        return static_cast<bool>(relay.server->client().connected());
      },
      nullptr, "", "", options);

  if (relay.started) {
    if (err != HttpDownloader::OK) {
      server.client().stop();
      return;
    }
    server.sendContent("");
    return;
  }
  sendJsonError(server, 502, "Image download failed");
}

void handleRegisterArticle(WebServer& server) {
  JsonDocument doc;
  if (!parseJsonBody(server, doc)) return;

  InstapaperArticle article;
  article.id = doc["id"] | 0u;
  article.savedAt = doc["savedAt"] | 0u;
  article.title = doc["title"] | "";
  article.site = doc["site"] | "";
  article.path = doc["path"] | "";

  if (article.id == 0 || !isInstapaperArticlePath(article.path) || !FsHelpers::hasEpubExtension(article.path) ||
      article.path.find("..") != std::string::npos) {
    sendJsonError(server, 400, "Invalid article");
    return;
  }
  if (!Storage.exists(article.path.c_str())) {
    sendJsonError(server, 404, "Article file not found");
    return;
  }
  if (!INSTAPAPER_ARTICLES.upsert(article)) {
    sendJsonError(server, 507, "Article list is full");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleRemoveArticle(WebServer& server) {
  JsonDocument doc;
  if (!parseJsonBody(server, doc)) return;
  const uint32_t id = doc["id"] | 0u;
  const InstapaperArticle* article = INSTAPAPER_ARTICLES.findById(id);
  if (!article) {
    sendJsonError(server, 404, "Unknown article");
    return;
  }

  // Same sequence as deleting a book from Recents: cache, bookmarks and
  // clippings first, then the file, then path-keyed lists.
  const std::string path = article->path;
  if (Storage.exists(path.c_str())) {
    BookActions::clearFileMetadata(path);
    if (!Storage.remove(path.c_str())) {
      LOG_ERR("IPW", "Failed to delete %s", path.c_str());
      sendJsonError(server, 500, "Failed to delete article file");
      return;
    }
  }
  RECENT_BOOKS.removeByPath(path);
  INSTAPAPER_ARTICLES.removeById(id);
  LOG_INF("IPW", "Removed article %u", static_cast<unsigned>(id));
  server.send(200, "application/json", "{\"ok\":true}");
}

}  // namespace

namespace InstapaperWebApi {

void registerRoutes(WebServer& server, bool apMode) {
  WebServer* s = &server;
  server.on("/api/instapaper", HTTP_GET, [s, apMode] { handleStatus(*s, apMode); });
  server.on("/api/instapaper/config", HTTP_POST, [s] { handleConfig(*s); });
  server.on("/api/instapaper/login", HTTP_POST, [s, apMode] { handleLogin(*s, apMode); });
  server.on("/api/instapaper/logout", HTTP_POST, [s] { handleLogout(*s); });
  server.on("/api/instapaper/call", HTTP_POST, [s, apMode] { handleCall(*s, apMode); });
  server.on("/api/instapaper/image", HTTP_GET, [s, apMode] { handleImage(*s, apMode); });
  server.on("/api/instapaper/articles", HTTP_POST, [s] { handleRegisterArticle(*s); });
  server.on("/api/instapaper/remove", HTTP_POST, [s] { handleRemoveArticle(*s); });
}

void release() { INSTAPAPER_ARTICLES.release(); }

}  // namespace InstapaperWebApi
