#include "InstapaperArticlesActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>

#include "BookActions.h"
#include "FileBrowserActionActivity.h"
#include "InstapaperArticleStore.h"
#include "MappedInputManager.h"
#include "RecentBookProgress.h"
#include "RecentBooksStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/TouchHeaderBackButton.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;
constexpr fui::ActionId ACTION_ROW = 1;

std::string formatSavedDate(uint32_t savedAt) {
  if (savedAt == 0) return "";
  const time_t t = static_cast<time_t>(savedAt);
  tm local{};
  if (!localtime_r(&t, &local)) return "";
  char buf[40];  // sized for any int fields so the format cannot truncate
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
  return buf;
}
}  // namespace

InstapaperArticlesActivity::InstapaperArticlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("InstapaperArticles", renderer, mappedInput),
      uiTarget(makeUiTarget(renderer)),
      app(uiTarget, uiTarget.deviceContext()) {}

void InstapaperArticlesActivity::loadRows() {
  rows.clear();
  const auto& articles = INSTAPAPER_ARTICLES.getArticles();
  rows.reserve(articles.size());
  for (const InstapaperArticle& article : articles) {
    if (!Storage.exists(article.path.c_str())) continue;
    Row row;
    row.id = article.id;
    row.savedAt = article.savedAt;
    row.path = article.path;
    row.title = article.title.empty() ? article.path : article.title;
    const std::string date = formatSavedDate(article.savedAt);
    row.subtitle = article.site;
    if (!date.empty()) row.subtitle += row.subtitle.empty() ? date : ", " + date;
    const InstapaperReadingState reading = loadInstapaperReadingState(article.path);
    row.finished = reading.finished;
    row.value = row.finished ? tr(STR_INSTAPAPER_FINISHED) : RecentBookProgress::formatPercent(reading.percent);
    rows.push_back(std::move(row));
  }
  // Unfinished first, newest first within each group.
  std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
    if (a.finished != b.finished) return !a.finished;
    return a.savedAt > b.savedAt;
  });
  // Rows hold their own copies; free the store until the next visit.
  INSTAPAPER_ARTICLES.release();
}

void InstapaperArticlesActivity::onRowEvent(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<InstapaperArticlesActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int16_t>(self->rows.size())) return;
  self->selectorIndex = static_cast<size_t>(event.value);
  self->app.clearTapFlash();
  if (event.longPress) {
    self->showActionMenu(self->selectorIndex);
    return;
  }
  self->onSelectBook(self->rows[self->selectorIndex].path);
}

void InstapaperArticlesActivity::onEnter() {
  Activity::onEnter();
  loadRows();
  selectorIndex = 0;
  uiReady = false;
  visibleRows = 1;
  topIndex = 0;
  applySharedUiTheme(app, uiTarget);
  app.on(ACTION_ROW, &InstapaperArticlesActivity::onRowEvent, this);
  app.setScreen(&InstapaperArticlesActivity::listScreen, this);
  requestUpdate();
}

void InstapaperArticlesActivity::onExit() {
  Activity::onExit();
  std::vector<Row>().swap(rows);
}

void InstapaperArticlesActivity::loop() {
  if (TouchHeaderBackButton::wasTapped(mappedInput, renderer)) {
    onGoHome();
    return;
  }
  const int listSize = static_cast<int>(rows.size());

  // Swallow input after a long-press until Confirm is released, so the release
  // does not also open the article.
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) longPressFired = false;
    return;
  }

  if (!rows.empty() && selectorIndex < rows.size() && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    showActionMenu(selectorIndex, true);
    return;
  }

  if (uiReady) {
    const fui::InputSnapshot snap = touchSnapshotFrom(mappedInput);
    if (snap.touchPressed || snap.touchReleased) {
      const auto event = app.route(snap);
      if (app.invalidated()) requestUpdate();
      if (event) return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!rows.empty() && selectorIndex < rows.size()) {
      onSelectBook(rows[selectorIndex].path);
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }

  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? visibleRows : -visibleRows;
    const int next = scrollListBy(topIndex, delta, visibleRows, listSize);
    if (next != topIndex) {
      topIndex = next;
      requestUpdate();
    }
    return;
  }

  const auto moveSelection = [this, listSize](const int index) {
    selectorIndex = static_cast<size_t>(index);
    topIndex = followListSelection(static_cast<int>(selectorIndex), topIndex, visibleRows, listSize);
    requestUpdate();
  };
  buttonNavigator.onNextRelease([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize));
  });
  buttonNavigator.onPreviousRelease([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize));
  });
  buttonNavigator.onNextContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, visibleRows));
  });
  buttonNavigator.onPreviousContinuous([this, listSize, &moveSelection] {
    moveSelection(ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, visibleRows));
  });
}

void InstapaperArticlesActivity::reloadAfterAction() {
  loadRows();
  if (rows.empty()) {
    selectorIndex = 0;
  } else if (selectorIndex >= rows.size()) {
    selectorIndex = rows.size() - 1;
  }
  topIndex = followListSelection(static_cast<int>(selectorIndex), topIndex, visibleRows, static_cast<int>(rows.size()));
  requestUpdate(true);
}

void InstapaperArticlesActivity::promptDelete(const Row& row) {
  const std::string path = row.path;
  const uint32_t id = row.id;
  auto handler = [this, path, id](const ActivityResult& res) {
    if (res.isCancelled) return;
    BookActions::clearFileMetadata(path);
    if (!Storage.remove(path.c_str())) {
      LOG_ERR("IPA", "Failed to delete article: %s", path.c_str());
      return;
    }
    RECENT_BOOKS.removeByPath(path);
    INSTAPAPER_ARTICLES.removeById(id);
    reloadAfterAction();
  };
  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, row.title),
                         std::move(handler));
}

// Article-specific subset of the book action menu: syncing depends on the
// finished flag, and deleting also drops the index entry.
void InstapaperArticlesActivity::showActionMenu(const size_t index, const bool ignoreInitialConfirmRelease) {
  if (index >= rows.size()) return;
  const Row row = rows[index];
  std::vector<FileBrowserActionActivity::MenuItem> items;
  items.reserve(2);
  items.push_back(
      {FileBrowserAction::ToggleCompleted, row.finished ? StrId::STR_MARK_UNFINISHED : StrId::STR_MARK_FINISHED});
  items.push_back({FileBrowserAction::Delete, StrId::STR_DELETE});

  startActivityForResult(std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, row.title, std::move(items),
                                                                     ignoreInitialConfirmRelease),
                         [this, row](const ActivityResult& result) {
                           longPressFired = false;
                           if (result.isCancelled) return;
                           const auto* actionResult = std::get_if<FileBrowserActionResult>(&result.data);
                           if (!actionResult) {
                             LOG_ERR("IPA", "Article action result missing");
                             return;
                           }
                           const auto action = static_cast<FileBrowserAction>(actionResult->action);
                           if (action == FileBrowserAction::Delete) {
                             promptDelete(row);
                           } else if (action == FileBrowserAction::ToggleCompleted) {
                             bool completed = false;
                             if (BookActions::toggleBookCompleted(row.path, row.title, completed)) {
                               BookActions::drawToast(renderer,
                                                      completed ? tr(STR_MARKED_FINISHED) : tr(STR_MARKED_UNFINISHED));
                               delay(1000);
                             }
                             reloadAfterAction();
                           }
                         });
}

void InstapaperArticlesActivity::listScreen(UiApp::ScreenType& screen, void* user) {
  static_cast<InstapaperArticlesActivity*>(user)->buildListScreen(screen);
}

void InstapaperArticlesActivity::buildListScreen(UiApp::ScreenType& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + TouchHeaderBackButton::height(metrics, mappedInput)), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight), 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  if (rows.empty()) {
    screen.centeredText(tr(STR_INSTAPAPER_EMPTY), screen.theme().bodyText);
    return;
  }

  // Transient per-render: points into the rows' strings.
  std::vector<fui::ListItem> items;
  items.reserve(rows.size());
  for (const Row& row : rows) {
    fui::ListItem item;
    item.label = row.title.c_str();
    if (!row.subtitle.empty()) item.subtitle = row.subtitle.c_str();
    if (!row.value.empty()) item.value = row.value.c_str();
    item.icon = listIconFor(UITheme::getFileIcon(row.path), 32);
    item.actionValue = static_cast<int16_t>(items.size());
    items.push_back(item);
  }

  fui::ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(items.size());
  props.selectedIndex = static_cast<int16_t>(selectorIndex);
  props.action = ACTION_ROW;
  props.inputMask = static_cast<uint16_t>(fui::InputTouch | fui::InputLongPress);  // physical buttons stay in loop()
  props.iconSize = 28;
  props.labelText = screen.theme().bodyText;
  props.labelText.bold = true;
  const auto visible = configureUiList(props, screen.theme(), screen.body(), UiListRowType::WithSubtitle);
  visibleRows = visible > 0 ? visible : 1;
  topIndex = scrollListBy(topIndex, 0, visibleRows, static_cast<int>(rows.size()));  // clamp to range
  props.topIndex = static_cast<uint16_t>(topIndex);
  screen.list(props);
}

void InstapaperArticlesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const Rect header = TouchHeaderBackButton::headerRect(renderer, mappedInput);
  if (mappedInput.hasTouchHardware()) {
    TouchHeaderBackButton::draw(renderer, uiTarget, header, tr(STR_INSTAPAPER_ARTICLES), false);
  } else {
    GUI.drawHeader(renderer, header, tr(STR_INSTAPAPER_ARTICLES));
  }

  uiReady = false;
  app.render();
  uiReady = true;

  const auto labels =
      mappedInput.mapLabels(mappedInput.withBackArrow(tr(STR_HOME)), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
