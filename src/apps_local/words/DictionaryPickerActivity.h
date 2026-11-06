#pragma once

#include <string>
#include <vector>

#include "activities/UiListActivity.h"
#include "util/DictionaryRegistry.h"

class DictionaryPickerActivity final : public UiListActivity {
 public:
  DictionaryPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string selected,
                           bool translation);
  void onEnter() override;

 protected:
  int listCount() const override { return static_cast<int>(entries.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;

 private:
  std::string selected;
  bool translation;
  std::vector<DictionaryEntry> entries;
  std::vector<freeink::ui::ListItem> rows;
};