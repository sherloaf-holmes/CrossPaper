#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

/**
 * OAuth-signed HTTPS client for the Instapaper Full API.
 *
 * The web portal's browser code cannot call Instapaper itself (no CORS headers
 * on api/1), so the device relays a small allowlist of methods. Responses are
 * streamed through a caller-supplied sink so article HTML never has to fit in
 * RAM; the device never parses it.
 */
class InstapaperClient {
 public:
  enum Error : uint8_t {
    OK = 0,
    NOT_CONFIGURED,
    NOT_LOGGED_IN,
    METHOD_NOT_ALLOWED,
    LOW_MEMORY,
    NETWORK_ERROR,
    AUTH_FAILED,
    BAD_RESPONSE,
  };

  // Plain function pointers + context instead of std::function: this sits on
  // the web-server request path.
  struct ResponseSink {
    void* context;
    // Called exactly once per response, before the first body chunk (or with
    // no body at all). Return false to abort.
    bool (*begin)(void* context, int httpStatus, const char* contentType);
    // Called for each body chunk. Return false to abort.
    bool (*write)(void* context, const uint8_t* data, size_t len);
  };

  // xAuth exchange: trades username/password for an OAuth token and stores the
  // token in INSTAPAPER_CREDS. The password is not stored. timestamp is Unix
  // seconds (the browser's clock; 0 falls back to the device clock).
  static Error login(const std::string& username, const std::string& password, uint32_t timestamp);

  // Signed POST to https://www.instapaper.com/api/1/<method> with the given
  // application/x-www-form-urlencoded body. On OK the sink has received the
  // whole upstream response, whatever its HTTP status.
  static Error call(std::string_view method, std::string_view formBody, uint32_t timestamp, const ResponseSink& sink);

  static bool isAllowedMethod(std::string_view method);
  static const char* errorString(Error error);
};
