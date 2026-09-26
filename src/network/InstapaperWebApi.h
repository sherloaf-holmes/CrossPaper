#pragma once

class WebServer;

/**
 * Web portal endpoints for Instapaper sync (/api/instapaper/*).
 *
 * The browser does the heavy work (HTML cleanup, image resizing, EPUB
 * packaging). The device only stores credentials, relays OAuth-signed calls to
 * Instapaper (the API sends no CORS headers, so the browser cannot call it
 * directly), and keeps the index of synced articles.
 */
namespace InstapaperWebApi {

void registerRoutes(WebServer& server, bool apMode);

// Frees the in-memory stores when the web server stops.
void release();

}  // namespace InstapaperWebApi
