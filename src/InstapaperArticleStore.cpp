#include "InstapaperArticleStore.h"

#include <Epub.h>
#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "activities/home/BookActions.h"
#include "activities/home/RecentBookProgress.h"

void InstapaperArticleStore::toJson(JsonDocument& doc) const {
  JsonArray arr = doc["articles"].to<JsonArray>();
  for (const auto& article : articles) {
    JsonObject obj = arr.add<JsonObject>();
    obj["id"] = article.id;
    obj["savedAt"] = article.savedAt;
    obj["title"] = article.title;
    obj["site"] = article.site;
    obj["path"] = article.path;
  }
}

bool InstapaperArticleStore::fromJson(JsonVariantConst doc) {
  articles.clear();
  JsonArrayConst arr = doc["articles"].as<JsonArrayConst>();
  articles.reserve(std::min(arr.size(), MAX_ARTICLES));
  for (JsonObjectConst obj : arr) {
    if (articles.size() >= MAX_ARTICLES) break;
    InstapaperArticle article;
    article.id = obj["id"] | 0u;
    article.savedAt = obj["savedAt"] | 0u;
    article.title = obj["title"] | "";
    article.site = obj["site"] | "";
    article.path = obj["path"] | "";
    if (article.id == 0 || article.path.empty()) continue;
    articles.push_back(std::move(article));
  }
  return true;
}

bool InstapaperArticleStore::saveToFile() const {
  std::lock_guard<std::mutex> lock(storeMutex);
  if (articles.empty()) {
    if (Storage.exists(getFilePath()) && !Storage.remove(getFilePath())) {
      LOG_ERR("IPA", "Failed to remove empty article index");
      return false;
    }
    return true;
  }
  JsonDocument doc;
  toJson(doc);
  return PersistableStoreBase::writeDocToFileAtomically(getFilePath(), doc);
}

bool InstapaperArticleStore::upsert(const InstapaperArticle& article) {
  ensureLoaded();
  auto it = std::find_if(articles.begin(), articles.end(),
                         [&](const InstapaperArticle& existing) { return existing.id == article.id; });
  if (it != articles.end()) {
    *it = article;
  } else {
    if (articles.size() >= MAX_ARTICLES) {
      LOG_ERR("IPA", "Article index full (%u)", static_cast<unsigned>(MAX_ARTICLES));
      return false;
    }
    articles.push_back(article);
  }
  if (!saveToFile()) LOG_ERR("IPA", "Failed to save article index");
  return true;
}

bool InstapaperArticleStore::removeById(uint32_t id) {
  ensureLoaded();
  auto it = std::find_if(articles.begin(), articles.end(),
                         [id](const InstapaperArticle& article) { return article.id == id; });
  if (it == articles.end()) return false;
  articles.erase(it);
  if (!saveToFile()) LOG_ERR("IPA", "Failed to save article index");
  return true;
}

const InstapaperArticle* InstapaperArticleStore::findById(uint32_t id) const {
  ensureLoaded();
  auto it = std::find_if(articles.begin(), articles.end(),
                         [id](const InstapaperArticle& article) { return article.id == id; });
  return it == articles.end() ? nullptr : &*it;
}

void InstapaperArticleStore::release() {
  std::vector<InstapaperArticle>().swap(articles);
  loadAttempted_ = false;
}

bool InstapaperArticleStore::hasAnyArticles() { return Storage.exists(getFilePath()); }

bool isInstapaperArticlePath(const std::string& path) {
  constexpr size_t prefixLen = sizeof(INSTAPAPER_FOLDER) - 1;
  return path.size() > prefixLen && path.compare(0, prefixLen, INSTAPAPER_FOLDER) == 0 && path[prefixLen] == '/';
}

InstapaperReadingState loadInstapaperReadingState(const std::string& path) {
  InstapaperReadingState state;
  // The reader creates the cache folder on first open; before that there is
  // nothing to read, and each missing-file read would log an open failure.
  if (!Storage.exists(Epub::cachePathForFilePath(path, "/.crosspoint").c_str())) return state;
  state.finished = BookActions::isBookCompleted(path);
  RecentBook book;
  book.path = path;
  state.percent = RecentBookProgress::loadCachedEpubPercent(book);
  return state;
}
