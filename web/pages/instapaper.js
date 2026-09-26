// Instapaper sync. The browser does the heavy work here (HTML cleanup, image
// resizing, EPUB packaging) so the device only relays API calls and stores
// files. Instapaper sends no CORS headers, so every API call and image fetch
// goes through the device (/api/instapaper/call and /api/instapaper/image).
// The device serves one request at a time, so requests are made in sequence.

const API = "/api/instapaper";
const ARTICLE_FOLDER = "/Instapaper";
// Images are probed (~1 KB) at layout and decoded one at a time as their page
// renders, so device memory does not grow with the count; the cap only bounds
// EPUB size and sync time.
const DEFAULT_MAX_IMAGES = 30;
const MAX_IMAGES_LIMIT = 100;
const IMAGE_FETCH_CONCURRENCY = 4;
const DIRECT_IMAGE_TIMEOUT_MS = 10000;
const IMAGE_MAX_WIDTH = 480;
const IMAGE_MAX_HEIGHT = 720;
const XHTML_NS = "http://www.w3.org/1999/xhtml";
const OPTIONS_KEY = "crossink.instapaper.options";

let state = null;
let syncing = false;
let editingKey = false;

const $ = (id) => document.getElementById(id);

function nowTs() {
  return Math.floor(Date.now() / 1000);
}

function xmlEscape(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;")
    .replace(/'/g, "&apos;");
}

function formatDate(unixSeconds) {
  if (!unixSeconds) return "";
  return new Date(unixSeconds * 1000).toLocaleDateString(undefined, { day: "numeric", month: "short", year: "numeric" });
}

function hostOf(url) {
  try {
    return new URL(url).hostname.replace(/^www\./, "");
  } catch (e) {
    return "";
  }
}

// ---------------------------------------------------------------- device API

async function readError(res) {
  const text = await res.text();
  try {
    const data = JSON.parse(text);
    if (data && data.error) return data.error;
    // Instapaper errors: [{"type":"error","error_code":1240,"message":"..."}]
    if (Array.isArray(data) && data[0] && data[0].message) return data[0].message + " (" + data[0].error_code + ")";
    if (data && data.message) return data.message;
  } catch (e) {
    // Not JSON.
  }
  return text || "HTTP " + res.status;
}

async function postJson(path, body) {
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body || {}),
  });
  if (!res.ok) throw new Error(await readError(res));
  return res;
}

// Signed Instapaper API call relayed by the device. The body is sent as
// text/plain so the device receives the form-encoded parameters verbatim.
async function instapaper(method, params) {
  const body = new URLSearchParams(params || {}).toString();
  const res = await fetch(API + "/call?m=" + encodeURIComponent(method) + "&ts=" + nowTs(), {
    method: "POST",
    headers: { "Content-Type": "text/plain" },
    body,
  });
  if (!res.ok) throw new Error(await readError(res));
  return res.text();
}

function bookmarksFrom(listText) {
  const data = JSON.parse(listText);
  // api/1 returns a typed array; newer responses use {bookmarks: [...]}.
  const items = Array.isArray(data) ? data : data.bookmarks || [];
  return items.filter((item) => item && (item.type === undefined || item.type === "bookmark") && item.bookmark_id);
}

async function loadState() {
  const res = await fetch(API);
  if (!res.ok) throw new Error(await readError(res));
  state = await res.json();
  render();
}

// ---------------------------------------------------------------- rendering

function setMessage(el, text, ok) {
  el.replaceChildren();
  if (!text) return;
  const div = document.createElement("div");
  div.className = ok ? "status-ok" : "status-err";
  div.textContent = text;
  el.appendChild(div);
}

function render() {
  const s = state;
  $("apWarning").hidden = !s.apMode;
  const showKeyForm = !s.hasConsumerKey || editingKey;
  $("keyForm").hidden = !showKeyForm;
  $("cancelKey").hidden = !s.hasConsumerKey;
  $("keyHelp").open = !s.hasConsumerKey;
  if (showKeyForm && !$("consumerKey").value) $("consumerKey").value = s.consumerKey || "";
  $("loginForm").hidden = showKeyForm || s.loggedIn;
  $("loggedIn").hidden = showKeyForm || !s.loggedIn;
  $("syncCard").hidden = !s.loggedIn || s.apMode;

  if (!s.hasConsumerKey) {
    $("accountStatus").textContent = "Add your Instapaper API key to get started.";
  } else if (!s.loggedIn) {
    $("accountStatus").textContent = "API key saved. Log in to your Instapaper account.";
  } else {
    $("accountStatus").textContent = "Logged in as " + (s.username || "Instapaper user") + ".";
  }

  renderArticles();
}

function renderArticles() {
  const el = $("articles");
  el.replaceChildren();
  const articles = (state && state.articles) || [];
  if (articles.length === 0) {
    const p = document.createElement("p");
    p.className = "empty";
    p.textContent = "No articles on the device yet.";
    el.appendChild(p);
    return;
  }
  const sorted = [...articles].sort((a, b) => (b.savedAt || 0) - (a.savedAt || 0));
  for (const a of sorted) {
    const row = document.createElement("div");
    row.className = "article";
    const info = document.createElement("div");
    info.className = "article-info";
    const title = document.createElement("div");
    title.className = "article-title";
    title.textContent = a.title || "Untitled";
    const meta = document.createElement("div");
    meta.className = "meta";
    meta.textContent = [a.site, formatDate(a.savedAt)].filter(Boolean).join(" · ");
    info.append(title, meta);

    const badge = document.createElement("span");
    badge.className = "badge";
    if (a.missing) {
      badge.textContent = "Deleted on device";
    } else if (a.finished) {
      badge.className += " done";
      badge.textContent = "Finished";
    } else if (a.percent > 0) {
      badge.textContent = Math.round(a.percent) + "%";
    } else {
      badge.textContent = "Unread";
    }
    row.append(info, badge);
    el.appendChild(row);
  }
}

// ---------------------------------------------------------------- options

function loadOptions() {
  try {
    const saved = JSON.parse(localStorage.getItem(OPTIONS_KEY) || "{}");
    if (typeof saved.archive === "boolean") $("optArchive").checked = saved.archive;
    if (typeof saved.progress === "boolean") $("optProgress").checked = saved.progress;
    if (typeof saved.maxImages === "number") $("optMaxImages").value = saved.maxImages;
    if (saved.max) $("optMax").value = saved.max;
    if (saved.folder) $("optFolder").dataset.saved = saved.folder;
  } catch (e) {
    // Storage unavailable: keep defaults.
  }
}

function readOptions() {
  const max = Math.max(1, Math.min(state.maxArticles || 50, parseInt($("optMax").value, 10) || 20));
  // 0 is a valid choice (no images), so it cannot use the `|| default` pattern.
  const imagesInput = parseInt($("optMaxImages").value, 10);
  const maxImages = Number.isNaN(imagesInput)
    ? DEFAULT_MAX_IMAGES
    : Math.max(0, Math.min(MAX_IMAGES_LIMIT, imagesInput));
  const options = {
    archive: $("optArchive").checked,
    progress: $("optProgress").checked,
    maxImages,
    max,
    folder: $("optFolder").value || "unread",
  };
  try {
    localStorage.setItem(OPTIONS_KEY, JSON.stringify(options));
  } catch (e) {
    // Storage unavailable: options just are not remembered.
  }
  return options;
}

async function loadFolders() {
  const select = $("optFolder");
  try {
    const folders = JSON.parse(await instapaper("folders/list"));
    for (const f of Array.isArray(folders) ? folders : []) {
      if (!f.folder_id) continue;
      const opt = document.createElement("option");
      opt.value = String(f.folder_id);
      opt.textContent = f.display_title || f.title || "Folder " + f.folder_id;
      select.appendChild(opt);
    }
  } catch (e) {
    // Folder list is optional; Unread/Starred still work.
  }
  if (select.dataset.saved) select.value = select.dataset.saved;
  if (!select.value) select.value = "unread";
}

// ---------------------------------------------------------------- EPUB build

const DROP_ELEMENTS =
  "script,style,noscript,iframe,object,embed,form,input,button,select,textarea,video,audio,canvas,svg,link,meta,source,template,dialog";
const KEEP_ATTRIBUTES = {
  a: ["href"],
  img: ["src", "alt"],
  td: ["colspan", "rowspan"],
  th: ["colspan", "rowspan"],
  ol: ["start"],
};

function pickImageSource(img, baseUrl) {
  let src = img.getAttribute("src") || img.getAttribute("data-src") || "";
  const srcset = img.getAttribute("srcset") || img.getAttribute("data-srcset");
  if ((!src || src.startsWith("data:image/gif")) && srcset) {
    // Take the first candidate; the image is resized anyway.
    src = srcset.split(",")[0].trim().split(/\s+/)[0];
  }
  if (!src) return "";
  try {
    return new URL(src, baseUrl).href;
  } catch (e) {
    return "";
  }
}

// host -> Promise<boolean>: whether the first direct (CORS) fetch from that
// host got through. Later images from the same host wait for that answer, so a
// host that refuses CORS is only tried directly once per page session.
const directHostProbes = new Map();
// The device serves one request at a time, so relayed images queue up here
// while direct fetches keep running in parallel.
let relayQueue = Promise.resolve();

function relayImage(url) {
  const run = relayQueue.then(async () => {
    const res = await fetch(API + "/image?url=" + encodeURIComponent(url));
    if (!res.ok) throw new Error(await readError(res));
    return res.blob();
  });
  relayQueue = run.catch(() => {});
  return run;
}

// Many image CDNs send CORS headers, so the browser can fetch those images
// itself without the slow trip through the device.
async function fetchImageDirect(url) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), DIRECT_IMAGE_TIMEOUT_MS);
  try {
    const res = await fetch(url, {
      mode: "cors",
      credentials: "omit",
      referrerPolicy: "no-referrer",
      signal: controller.signal,
    });
    if (!res.ok) {
      // The host answered (CORS allowed it); relaying would get the same error.
      const err = new Error("HTTP " + res.status);
      err.final = true;
      throw err;
    }
    return await res.blob();
  } finally {
    clearTimeout(timer);
  }
}

async function fetchImageBlob(url, counts) {
  if (url.startsWith("data:")) return (await fetch(url)).blob();
  const host = hostOf(url);
  const probe = directHostProbes.get(host);
  let tryDirect = true;
  let attempt = null;
  if (probe) {
    tryDirect = await probe;
  } else {
    attempt = fetchImageDirect(url);
    // An HTTP error still proves CORS works for this host.
    directHostProbes.set(
      host,
      attempt.then(
        () => true,
        (e) => e.final === true
      )
    );
  }
  if (tryDirect) {
    try {
      const blob = await (attempt || fetchImageDirect(url));
      counts.direct++;
      return blob;
    } catch (e) {
      if (e.final) throw e;
    }
  }
  const blob = await relayImage(url);
  counts.relayed++;
  return blob;
}

// Resizes to the screen and converts to grayscale baseline JPEG, which the
// device decodes quickly and which keeps article EPUBs small.
async function toDeviceJpeg(blob) {
  const bitmap = await createImageBitmap(blob);
  try {
    // Skip tracking pixels and spacers.
    if (bitmap.width < 16 || bitmap.height < 16) return null;
    const scale = Math.min(1, IMAGE_MAX_WIDTH / bitmap.width, IMAGE_MAX_HEIGHT / bitmap.height);
    const width = Math.max(1, Math.round(bitmap.width * scale));
    const height = Math.max(1, Math.round(bitmap.height * scale));
    const canvas = document.createElement("canvas");
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext("2d");
    ctx.fillStyle = "#fff";
    ctx.fillRect(0, 0, width, height);
    ctx.drawImage(bitmap, 0, 0, width, height);
    const pixels = ctx.getImageData(0, 0, width, height);
    const d = pixels.data;
    for (let i = 0; i < d.length; i += 4) {
      const gray = Math.round(0.299 * d[i] + 0.587 * d[i + 1] + 0.114 * d[i + 2]);
      d[i] = d[i + 1] = d[i + 2] = gray;
    }
    ctx.putImageData(pixels, 0, 0);
    return await new Promise((resolve) => canvas.toBlob(resolve, "image/jpeg", 0.8));
  } finally {
    bitmap.close();
  }
}

async function embedImages(doc, zip, baseUrl, log, maxImages) {
  const candidates = [];
  for (const img of [...doc.querySelectorAll("img")]) {
    const src = candidates.length < maxImages ? pickImageSource(img, baseUrl) : "";
    if (!src) {
      img.remove();
      continue;
    }
    candidates.push({ img, src });
  }
  if (candidates.length === 0) return [];

  // A few workers at once: direct fetches overlap, relayed ones still queue.
  const counts = { direct: 0, relayed: 0, failed: 0 };
  const jpegs = new Array(candidates.length).fill(null);
  let next = 0;
  const worker = async () => {
    while (next < candidates.length) {
      const i = next++;
      try {
        jpegs[i] = await toDeviceJpeg(await fetchImageBlob(candidates[i].src, counts));
      } catch (e) {
        counts.failed++;
      }
    }
  };
  await Promise.all(Array.from({ length: Math.min(IMAGE_FETCH_CONCURRENCY, candidates.length) }, worker));

  const images = [];
  candidates.forEach(({ img }, i) => {
    if (!jpegs[i]) {
      img.remove();
      return;
    }
    const href = "images/img" + (images.length + 1) + ".jpg";
    zip.file("OEBPS/" + href, jpegs[i]);
    images.push({ id: "img" + (images.length + 1), href });
    const alt = img.getAttribute("alt") || "";
    for (const attr of [...img.attributes]) img.removeAttribute(attr.name);
    img.setAttribute("src", href);
    img.setAttribute("alt", alt);
  });
  log(
    "  Images: " + counts.direct + " direct, " + counts.relayed + " via device" +
      (counts.failed ? ", " + counts.failed + " failed" : "")
  );
  return images;
}

function cleanDocument(doc, title) {
  doc.querySelectorAll(DROP_ELEMENTS).forEach((el) => el.remove());
  // picture wrappers keep only their <img>.
  doc.querySelectorAll("picture").forEach((pic) => pic.replaceWith(...pic.childNodes));

  for (const el of doc.body.querySelectorAll("*")) {
    const keep = KEEP_ATTRIBUTES[el.localName] || [];
    for (const attr of [...el.attributes]) {
      if (!keep.includes(attr.name)) el.removeAttribute(attr.name);
    }
    if (el.localName === "a") {
      const href = el.getAttribute("href") || "";
      if (!/^https?:/i.test(href)) el.removeAttribute("href");
    }
  }

  // Characters that are not allowed in XML would make the chapter unparseable.
  const walker = doc.createTreeWalker(doc.body, NodeFilter.SHOW_TEXT);
  for (let node = walker.nextNode(); node; node = walker.nextNode()) {
    node.nodeValue = node.nodeValue.replace(/[\u0000-\u0008\u000B\u000C\u000E-\u001F￾￿]/g, "");
  }

  // We write our own title header, so drop a leading duplicate.
  const first = doc.body.firstElementChild;
  if (first && /^h[12]$/.test(first.localName) && first.textContent.trim() === title.trim()) first.remove();
}

function buildChapter(article, sourceBody, lang) {
  const xdoc = document.implementation.createDocument(XHTML_NS, "html", null);
  const html = xdoc.documentElement;
  html.setAttribute("lang", lang);
  const el = (name, text) => {
    const node = xdoc.createElementNS(XHTML_NS, name);
    if (text !== undefined) node.textContent = text;
    return node;
  };

  const head = el("head");
  head.appendChild(el("title", article.title));
  const css = el("link");
  css.setAttribute("rel", "stylesheet");
  css.setAttribute("type", "text/css");
  css.setAttribute("href", "style.css");
  head.appendChild(css);

  const body = el("body");
  body.appendChild(el("h1", article.title));
  const meta = el("p", [article.site, formatDate(article.savedAt)].filter(Boolean).join(" · "));
  meta.setAttribute("class", "article-meta");
  body.appendChild(meta);
  for (const child of [...sourceBody.childNodes]) body.appendChild(xdoc.importNode(child, true));

  html.append(head, body);
  return '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE html>\n' + new XMLSerializer().serializeToString(xdoc);
}

function buildOpf(article, images, lang) {
  const modified = new Date().toISOString().replace(/\.\d+Z$/, "Z");
  const saved = article.savedAt ? new Date(article.savedAt * 1000).toISOString().slice(0, 10) : "";
  const imageItems = images
    .map((img) => `    <item id="${img.id}" href="${img.href}" media-type="image/jpeg"/>`)
    .join("\n");
  return `<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:instapaper:${article.id}</dc:identifier>
    <dc:title>${xmlEscape(article.title)}</dc:title>
    <dc:creator>${xmlEscape(article.site || "Instapaper")}</dc:creator>
    <dc:language>${xmlEscape(lang)}</dc:language>
    ${saved ? `<dc:date>${saved}</dc:date>` : ""}
    <dc:source>${xmlEscape(article.url || "")}</dc:source>
    <meta property="dcterms:modified">${modified}</meta>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
    <item id="css" href="style.css" media-type="text/css"/>
    <item id="article" href="article.xhtml" media-type="application/xhtml+xml"/>
${imageItems}
  </manifest>
  <spine toc="ncx">
    <itemref idref="article"/>
  </spine>
</package>`;
}

function buildNav(article) {
  return `<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
<head><title>${xmlEscape(article.title)}</title></head>
<body><nav epub:type="toc"><ol><li><a href="article.xhtml">${xmlEscape(article.title)}</a></li></ol></nav></body>
</html>`;
}

function buildNcx(article) {
  return `<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
  <head><meta name="dtb:uid" content="urn:instapaper:${article.id}"/></head>
  <docTitle><text>${xmlEscape(article.title)}</text></docTitle>
  <navMap>
    <navPoint id="article" playOrder="1"><navLabel><text>${xmlEscape(article.title)}</text></navLabel><content src="article.xhtml"/></navPoint>
  </navMap>
</ncx>`;
}

const CONTAINER_XML = `<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>`;

const ARTICLE_CSS = `img { max-width: 100%; height: auto; }
.article-meta { font-size: 0.85em; margin-bottom: 1.5em; }
figcaption { font-size: 0.85em; }
pre { white-space: pre-wrap; }`;

async function buildEpub(article, log, maxImages = DEFAULT_MAX_IMAGES) {
  const doc = new DOMParser().parseFromString(article.html, "text/html");
  const lang = (doc.documentElement.getAttribute("lang") || "en").split(/[-_]/)[0].toLowerCase() || "en";

  const zip = new JSZip();
  // mimetype must be the first entry and stored uncompressed.
  zip.file("mimetype", "application/epub+zip", { compression: "STORE" });
  zip.file("META-INF/container.xml", CONTAINER_XML);

  const images = await embedImages(doc, zip, article.url, log, maxImages);
  cleanDocument(doc, article.title);

  zip.file("OEBPS/article.xhtml", buildChapter(article, doc.body, lang));
  zip.file("OEBPS/style.css", ARTICLE_CSS);
  zip.file("OEBPS/nav.xhtml", buildNav(article));
  zip.file("OEBPS/toc.ncx", buildNcx(article));
  zip.file("OEBPS/content.opf", buildOpf(article, images, lang));

  const blob = await zip.generateAsync({
    type: "blob",
    mimeType: "application/epub+zip",
    compression: "DEFLATE",
    compressionOptions: { level: 6 },
  });
  return { blob, imageCount: images.length };
}

// ASCII-only so the device's filename sanitizer leaves it unchanged.
function articleFilename(article) {
  const slug = article.title
    .normalize("NFKD")
    .replace(/[̀-ͯ]/g, "")
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 48)
    .replace(/-+$/, "");
  return article.id + "-" + (slug || "article") + ".epub";
}

async function ensureArticleFolder() {
  // URL-encoded rather than multipart so every server build parses the fields.
  const form = new URLSearchParams({ name: ARTICLE_FOLDER.slice(1), path: "/" });
  const res = await fetch("/mkdir", { method: "POST", body: form });
  if (res.ok) return;
  const text = await res.text();
  if (!/already exists/i.test(text)) throw new Error("Could not create " + ARTICLE_FOLDER + ": " + text);
}

async function uploadEpub(blob, filename) {
  const form = new FormData();
  form.append("file", new File([blob], filename, { type: "application/epub+zip" }));
  const res = await fetch("/upload?path=" + encodeURIComponent(ARTICLE_FOLDER), { method: "POST", body: form });
  const text = await res.text();
  if (res.ok) {
    const match = text.match(/File uploaded successfully: (.+)$/);
    return match ? match[1].trim() : filename;
  }
  // Left over from an interrupted sync: the file is there, just not indexed.
  if (/already exists/i.test(text)) return filename;
  throw new Error(text || "Upload failed");
}

// ---------------------------------------------------------------- sync

function makeLogger() {
  const list = $("log");
  list.hidden = false;
  list.replaceChildren();
  return (text, kind) => {
    const li = document.createElement("li");
    li.textContent = text;
    if (kind) li.className = kind;
    list.appendChild(li);
    list.scrollTop = list.scrollHeight;
  };
}

async function syncFinishedArticles(options, log) {
  for (const a of state.articles) {
    const label = '"' + (a.title || a.id) + '"';
    if (a.missing) {
      // Deleted on the device without being archived. Drop the index entry;
      // it is downloaded again next time if it is still unread in Instapaper.
      await postJson(API + "/remove", { id: a.id });
      log("Forgot " + label + " (deleted on device)");
      continue;
    }
    if (a.finished) {
      if (!options.archive) continue;
      await instapaper("bookmarks/archive", { bookmark_id: a.id });
      await postJson(API + "/remove", { id: a.id });
      log("Archived " + label + " and removed it from the device", "ok");
      continue;
    }
    if (options.progress && a.percent > 0) {
      await instapaper("bookmarks/update_read_progress", {
        bookmark_id: a.id,
        progress: Math.min(1, a.percent / 100).toFixed(4),
        progress_timestamp: nowTs(),
      });
    }
  }
}

async function downloadArticle(bookmark, log, options) {
  const article = {
    id: bookmark.bookmark_id,
    title: (bookmark.title || "").trim() || hostOf(bookmark.url) || "Untitled",
    url: bookmark.url || "",
    site: hostOf(bookmark.url),
    savedAt: bookmark.time || 0,
  };
  log('Downloading "' + article.title + '"...');
  article.html = await instapaper("bookmarks/get_text", { bookmark_id: article.id });
  const { blob, imageCount } = await buildEpub(article, log, options.maxImages);
  const filename = await uploadEpub(blob, articleFilename(article));
  await postJson(API + "/articles", {
    id: article.id,
    title: article.title,
    site: article.site,
    savedAt: article.savedAt,
    path: ARTICLE_FOLDER + "/" + filename,
  });
  const kb = Math.max(1, Math.round(blob.size / 1024));
  log('  Saved "' + article.title + '" (' + kb + " KB, " + imageCount + " images)", "ok");
}

async function sync() {
  if (syncing) return;
  syncing = true;
  $("syncBtn").disabled = true;
  const log = makeLogger();
  try {
    await loadState();
    const options = readOptions();

    await syncFinishedArticles(options, log);
    await loadState();

    const onDevice = state.articles.map((a) => a.id);
    const slots = options.max - onDevice.length;
    if (slots <= 0) {
      log("The device already has " + onDevice.length + " articles. Finish some, or raise the limit.");
      return;
    }

    await ensureArticleFolder();
    log("Checking Instapaper for new articles...");
    const listText = await instapaper("bookmarks/list", {
      limit: Math.min(500, slots + onDevice.length),
      folder_id: options.folder,
      have: onDevice.join(","),
    });
    const fresh = bookmarksFrom(listText)
      .filter((b) => !onDevice.includes(b.bookmark_id))
      .slice(0, slots);
    if (fresh.length === 0) log("No new articles.");

    let failures = 0;
    for (const bookmark of fresh) {
      try {
        await downloadArticle(bookmark, log, options);
      } catch (e) {
        failures++;
        log('  Failed "' + (bookmark.title || bookmark.bookmark_id) + '": ' + e.message, "err");
      }
    }
    log(failures ? "Sync finished with " + failures + " error(s)." : "Sync complete.", failures ? "err" : "ok");
  } catch (e) {
    log("Sync failed: " + e.message, "err");
  } finally {
    syncing = false;
    $("syncBtn").disabled = false;
    try {
      await loadState();
    } catch (e) {
      // Keep showing the last known list.
    }
  }
}

// ---------------------------------------------------------------- wiring

$("keyForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const body = { consumerKey: $("consumerKey").value.trim() };
  const secret = $("consumerSecret").value.trim();
  if (secret) body.consumerSecret = secret;
  try {
    await postJson(API + "/config", body);
    $("consumerSecret").value = "";
    editingKey = false;
    setMessage($("accountMsg"), "API key saved.", true);
    await loadState();
  } catch (err) {
    setMessage($("accountMsg"), err.message, false);
  }
});

$("cancelKey").addEventListener("click", () => {
  editingKey = false;
  render();
});

document.querySelectorAll(".change-key").forEach((btn) =>
  btn.addEventListener("click", () => {
    editingKey = true;
    setMessage($("accountMsg"), "");
    render();
  })
);

$("loginForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  setMessage($("accountMsg"), "Logging in...", true);
  try {
    await postJson(API + "/login", { username: $("username").value.trim(), password: $("password").value, ts: nowTs() });
    $("password").value = "";
    setMessage($("accountMsg"), "Logged in.", true);
    await loadState();
    loadFolders();
  } catch (err) {
    setMessage($("accountMsg"), err.message, false);
  }
});

$("logout").addEventListener("click", async () => {
  try {
    await postJson(API + "/logout");
    setMessage($("accountMsg"), "Logged out.", true);
    await loadState();
  } catch (err) {
    setMessage($("accountMsg"), err.message, false);
  }
});

$("syncBtn").addEventListener("click", sync);

(async () => {
  loadOptions();
  try {
    await loadState();
    if (state.loggedIn && !state.apMode) loadFolders();
  } catch (e) {
    $("accountStatus").textContent = "Could not reach the device: " + e.message;
  }
})();
