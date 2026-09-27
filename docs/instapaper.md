---
title: Instapaper Sync
nav_order: 16
---

# Instapaper Sync

CrossPaper can download your unread Instapaper articles as EPUBs and archive
them in Instapaper once you finish them on the device. Syncing happens from
the web portal, so you type your login on a laptop or phone rather than on the
reader.

## How it works

- Your browser does the heavy work: it cleans up each article, resizes images
  to grayscale, and packages an EPUB. The reader only stores files.
- Instapaper doesn't let web pages on other sites call its API, so the reader
  signs each request and passes it on to Instapaper.
- The browser downloads article images itself when the image's site allows it,
  several at a time. Images from sites that block this go through the reader,
  one at a time, which is slower.
- Articles are saved in `/Instapaper/` and listed on the Home screen under
  **Articles**, with the site, the date you saved them and your progress.
- An article counts as finished when you mark it finished: accept the
  "Mark as Finished?" prompt when you page past the last page, or use
  **Mark as Finished** from the article's long-press menu.

Nothing syncs in the background. Instapaper is only contacted while you have
the sync page open.

## One-time setup

1. Sign in at [instapaper.com/developers](https://www.instapaper.com/developers)
   and create an application. Leave it in **Owner Only** mode; you don't need
   to submit it for review.
2. Copy the consumer key and secret. The secret is shown only once, when you
   create the application.
3. On the reader, open **File Transfer → Join Network**. Hotspot mode can't be
   used, because the browser needs internet access.
4. In the browser, open the address shown on the reader and go to **Articles**.
5. Enter the consumer key and secret, then log in with your Instapaper
   account. Your password is sent to Instapaper once to get a sign-in token.
   Only the token is stored on the reader.

Each person needs their own API key. Instapaper revokes keys that are shared
publicly, and only a key's owner can download article text without a paid
Instaparser plan.

## Syncing

Press **Sync now**. For each article on the reader, the sync:

1. Archives it in Instapaper and deletes it from the reader if it's finished
   (while **Archive articles marked finished** is ticked).
2. Sends its reading progress to Instapaper if it's unfinished (while that
   option is ticked).

It then downloads new articles from the chosen folder until the reader holds
the chosen number of articles (at most 50).

If you delete an article on the reader without finishing it, the next sync
forgets it. If it's still unread in Instapaper, a later sync downloads it
again.

## Limits

- Up to 30 images per article by default, resized to fit a 480 × 720 area.
  Change this with **Max images per article** (0–100); 0 skips images for the
  fastest sync. Images that fail to download are dropped from the article. The
  sync log shows how many of each article's images came directly and how many
  through the reader.
- The reader handles one web request at a time, so each article takes a few
  seconds. Keep the page open until the sync log says it's done.
- Instapaper articles are never moved by **Move Finished Books to Read Folder**,
  because the sync finds them by their location.
- Credentials are stored in `/.crosspoint/instapaper.json`, obfuscated with the
  device's hardware ID (the same scheme as KOReader Sync). This keeps casual
  readers of the SD card out, but it isn't encryption.

## Web API

These endpoints back the `/instapaper` page. Instapaper's API sends no CORS
headers, so the page cannot call it directly: the device signs and relays calls
instead. Calls that reach the internet return `409` in hotspot mode. The
consumer secret and OAuth token are never returned.

### `GET /api/instapaper`

Account state and the articles currently on the device. `finished` comes from
the book's reading stats, and `percent` is the cached reading progress (`-1`
when unknown). `missing` means the EPUB was deleted on the device.

```json
{
  "apMode": false,
  "hasConsumerKey": true,
  "consumerKey": "abc123",
  "loggedIn": true,
  "username": "reader@example.com",
  "maxArticles": 50,
  "articles": [
    {
      "id": 1234567,
      "title": "An Article",
      "site": "example.com",
      "savedAt": 1790000000,
      "path": "/Instapaper/1234567-an-article.epub",
      "missing": false,
      "finished": false,
      "percent": 42.5
    }
  ]
}
```

### `POST /api/instapaper/config`

Saves the user's own Instapaper API application key. If `consumerSecret` is
left out, the saved secret is kept. Changing the key logs out.

```bash
curl -X POST -H "Content-Type: application/json" \
  -d '{"consumerKey":"abc123","consumerSecret":"s3cret"}' \
  http://crosspoint.local/api/instapaper/config
```

### `POST /api/instapaper/login`

Exchanges `username` and `password` for an OAuth token (xAuth). Only the token
is stored. `ts` is the browser's Unix time, used to sign the request in case the
device clock is wrong.

```json
{"username": "reader@example.com", "password": "...", "ts": 1790000000}
```

### `POST /api/instapaper/logout`

Forgets the OAuth token. The API key is kept.

### `POST /api/instapaper/call?m=<method>&ts=<unix>`

Signs the request and relays it to `https://www.instapaper.com/api/1/<method>`.
The request body is the form-encoded Instapaper parameters, sent as
`text/plain`. Allowed methods: `bookmarks/list`, `bookmarks/get_text`,
`bookmarks/archive`, `bookmarks/update_read_progress` and `folders/list`.
Instapaper's status code and body are streamed back unchanged, marked with
`X-Instapaper-Relay: 1`. Errors from the device itself return JSON
`{"error": "..."}` without that header.

```bash
curl -X POST -H "Content-Type: text/plain" -d 'bookmark_id=1234567' \
  "http://crosspoint.local/api/instapaper/call?m=bookmarks/get_text&ts=$(date +%s)"
```

### `GET /api/instapaper/image?url=<http(s) url>`

Streams one article image through the device, so the page can resize it
despite the image host's CORS policy. Responses are capped at 1.5 MB.

### `POST /api/instapaper/articles`

Adds an uploaded EPUB to the article index. `path` must be an existing `.epub`
inside `/Instapaper/`.

```json
{"id": 1234567, "title": "An Article", "site": "example.com", "savedAt": 1790000000,
 "path": "/Instapaper/1234567-an-article.epub"}
```

### `POST /api/instapaper/remove`

Deletes an article's EPUB, its cache, bookmarks and clippings, and removes it
from the index and Recent Books.

```json
{"id": 1234567}
```
