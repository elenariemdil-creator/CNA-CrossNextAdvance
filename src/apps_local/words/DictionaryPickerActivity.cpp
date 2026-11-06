#include "DictionaryPickerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

DictionaryPickerActivity::DictionaryPickerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   std::string selected, const bool translation)
    : UiListActivity("DictionaryPicker", renderer, mappedInput), selected(std::move(selected)),
      translation(translation) {}

void DictionaryPickerActivity::onEnter() {
  UiListActivity::onEnter();
  DictionaryRegistry::discover(entries);
  entries.erase(std::remove_if(entries.begin(), entries.end(), [](const DictionaryEntry& entry) {
                  return entry.name.size() >= sizeof(SETTINGS.dictionaryName);
                }),
                entries.end());
  rows.reserve(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    fui::ListItem row;
    row.label = entries[i].name.c_str();
    row.actionValue = static_cast<int16_t>(i);
    rows.push_back(row);
    if (entries[i].name == selected) nav.selected = static_cast<int>(i);
  }
  requestUpdate();
}

const char* DictionaryPickerActivity::headerTitle() const {
  return I18N.get(translation ? StrId::STR_WORD_TRANSLATION_DICT : StrId::STR_WORD_DEFINITION_DICT);
}

void DictionaryPickerActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  if (rows.empty()) return;
  fui::ListProps props;
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props);
  props.items = rows.data();
  screen.list(props);
}

void DictionaryPickerActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  setResult(FilePathResult{entries[static_cast<size_t>(index)].name});
  finish();
}