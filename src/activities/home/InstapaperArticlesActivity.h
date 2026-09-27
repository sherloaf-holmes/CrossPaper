#pragma once
#include <FreeInkApp.h>
#include <FreeInkUIGfxRenderer.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Lists Instapaper articles synced by the web portal, newest first, with a
// finished/progress marker. Syncing itself happens in the browser.
class InstapaperArticlesActivity final : public Activity {
 private:
  using UiApp = freeink::ui::FreeInkApp<20, 4>;

  // Display rows, copied out of INSTAPAPER_ARTICLES so the store can be
  // released while this screen is open.
  struct Row {
    uint32_t id = 0;
    uint32_t savedAt = 0;
    std::string path;
    std::string title;
    std::string subtitle;  // "site, date"
    std::string value;     // "Finished", "42%", or empty
    bool finished = false;
  };

  ButtonNavigator buttonNavigator;
  std::vector<Row> rows;
  size_t selectorIndex = 0;
  bool longPressFired = false;

  freeink::ui::GfxRendererTarget uiTarget;  // must precede `app`: the app holds a reference to it
  UiApp app;
  std::atomic<bool> uiReady{false};
  int visibleRows = 1;
  int topIndex = 0;

  static void listScreen(UiApp::ScreenType& screen, void* user);
  static void onRowEvent(const freeink::ui::ActionEvent& event, void* user);
  void buildListScreen(UiApp::ScreenType& screen);

  void loadRows();
  void reloadAfterAction();
  void showActionMenu(size_t index, bool ignoreInitialConfirmRelease = false);
  void promptDelete(const Row& row);

 public:
  explicit InstapaperArticlesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
