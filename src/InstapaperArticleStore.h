#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>
#include <vector>

struct InstapaperArticle {
  uint32_t id = 0;       // Instapaper bookmark_id
  uint32_t savedAt = 0;  // Unix time the bookmark was saved in Instapaper
  std::string title;
  std::string site;  // Source host, e.g. "example.com"
  std::string path;  // EPUB path on the SD card, under INSTAPAPER_FOLDER
};

/**
 * Index of Instapaper articles synced onto the SD card by the web portal.
 *
 * Holds display metadata only. Whether an article is finished is not stored
 * here: it comes from the book's own reading stats (BookActions::isBookCompleted),
 * so every existing "mark finished" path works without extra hooks.
 *
 * Loaded lazily and only by the web server and the Articles screen, which call
 * release() on exit so the list (up to MAX_ARTICLES * ~250 B) does not stay
 * resident while reading. The JSON file is deleted when the list is empty, so
 * Home can test for articles with a single exists() instead of loading it.
 */
class InstapaperArticleStore : public PersistableStore<InstapaperArticleStore> {
 private:
  std::vector<InstapaperArticle> articles;

  InstapaperArticleStore() = default;
  ~InstapaperArticleStore() = default;

  friend class PersistableStore<InstapaperArticleStore>;

 public:
  static constexpr size_t MAX_ARTICLES = 50;

  static const char* getFilePath() { return "/.crosspoint/instapaper_articles.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
  bool saveToFile() const;

  // Adds or replaces the entry with article.id. Returns false when the index is
  // full. Persists on success.
  bool upsert(const InstapaperArticle& article);

  // Removes the entry with this id. Returns true if one was removed. Persists.
  bool removeById(uint32_t id);

  const InstapaperArticle* findById(uint32_t id) const;

  const std::vector<InstapaperArticle>& getArticles() const {
    ensureLoaded();
    return articles;
  }

  // Frees the in-memory list; the next access reloads it from the SD card.
  void release();

  // Cheap check for Home: the file only exists while the list is non-empty.
  static bool hasAnyArticles();
};

// Folder the web portal uploads article EPUBs into.
constexpr char INSTAPAPER_FOLDER[] = "/Instapaper";

// True for paths inside INSTAPAPER_FOLDER. Such books must not be moved by
// "move finished to Read folder": their path is the link to the index entry.
bool isInstapaperArticlePath(const std::string& path);

// Reading state from the article's own book cache (reading stats + cached
// progress). A never-opened article has no cache folder yet, so it is reported
// unread without trying to open the missing stats/progress files.
struct InstapaperReadingState {
  bool finished = false;
  float percent = -1.0f;  // 0-100; negative when unknown
};
InstapaperReadingState loadInstapaperReadingState(const std::string& path);

#define INSTAPAPER_ARTICLES InstapaperArticleStore::getInstance()
