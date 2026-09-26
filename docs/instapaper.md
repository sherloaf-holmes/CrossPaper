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
  signs each request and passes it on to Instapaper. Article images go through
  the reader the same way.
- Articles are saved in `/Instapaper/` and listed on the Home screen under
  **Articles**, with the site, the date you saved them and your progress.
- An article counts as finished when you mark it finished: accept the
  "Mark as Finished?" prompt at 99%, or use **Mark as Finished** from the
  article's long-press menu.

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

- Up to 15 images per article, resized to fit a 480 × 720 area. Images that
  fail to download are dropped from the article.
- The reader handles one web request at a time, so each article takes a few
  seconds. Keep the page open until the sync log says it's done.
- Instapaper articles are never moved by **Move Finished Books to Read Folder**,
  because the sync finds them by their location.
- Credentials are stored in `/.crosspoint/instapaper.json`, obfuscated with the
  device's hardware ID (the same scheme as KOReader Sync). This keeps casual
  readers of the SD card out, but it isn't encryption.
