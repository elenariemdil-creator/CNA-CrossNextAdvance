#include "DictionaryWordSelectActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Memory.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <algorithm>
#include <cctype>
#include <string_view>

#include "CrossPointSettings.h"
#include "DictionaryDefinitionActivity.h"
#include "components/UITheme.h"
#include "util/HtmlToPlainText.h"

namespace {

constexpr unsigned long POPUP_DURATION_MS = 1500;
constexpr size_t MAX_MULTIWORD_TERMS = 8;
constexpr size_t MAX_MULTIWORD_DEFINITION_BYTES = 16 * 1024;

void indexBuildYield(void*) { vTaskDelay(1); }

}  // namespace

void DictionaryWordSelectActivity::onEnter() {
  Activity::onEnter();
  fontId = SETTINGS.getReaderFontId();
  lookupDictionary = translate ? SETTINGS.translationDictionaryName : SETTINGS.dictionaryName;
  std::vector<DictionaryEntry> dictionaries;
  DictionaryRegistry::discover(dictionaries);
  dictionaries.erase(std::remove_if(dictionaries.begin(), dictionaries.end(), [](const DictionaryEntry& entry) {
                       return entry.name.size() >= sizeof(SETTINGS.dictionaryName);
                     }),
                     dictionaries.end());
  if (dictionaries.size() > 1) {
    auto picker = makeUniqueNoThrow<DictionaryPickerActivity>(renderer, mappedInput, lookupDictionary, translate);
    if (picker) {
      startActivityForResult(std::move(picker), [this](const ActivityResult& result) {
        if (result.isCancelled) {
          finish();
          return;
        }
        const auto* selected = std::get_if<FilePathResult>(&result.data);
        if (!selected || selected->path.empty()) {
          finish();
          return;
        }
        lookupDictionary = selected->path;
        popupMsg = StrId::STR_DICT_LOOKING_UP;
        popup = Popup::Busy;
        lookupPending = true;
        requestUpdate();
      });
      return;
    }
  }
  lookupPending = true;
  popupMsg = StrId::STR_DICT_LOOKING_UP;
  popup = Popup::Busy;
  requestUpdate();
}

void DictionaryWordSelectActivity::performLookup() {
  {
    RenderLock lock;
    popupMsg = StrId::STR_DICT_LOOKING_UP;
    popup = Popup::Busy;
  }
  if (!dictOpenAttempted) {
    dictOpenAttempted = true;
    dictOpenOk = dict.open(lookupDictionary.c_str());
    // needsIndex() opens and validates the .qidx sidecar, so ask it once per
    // open rather than once per word: the answer only changes when we build
    // the sidecar ourselves, which is handled below.
    dictNeedsIndex = dictOpenOk && dict.needsIndex();
  }
  {
    RenderLock lock;
    popupMsg = dictNeedsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP;
  }
  requestUpdateAndWait();  // paint the page + busy popup before blocking on SD

  bool ok = dictOpenOk;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (ok && dictNeedsIndex) {
    ok = dict.buildIndex(&indexBuildYield, nullptr, &indexResult);
    dictNeedsIndex = !ok;  // a successful build leaves the sidecar fresh; a failed one retries
  }

  std::string definition;
  std::string headword;
  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  bool found = ok && dict.lookup(lookupText.c_str(), definition, headword, &result);
  bool combinedMultiwordDefinition = false;

  if (ok && !found && result == Dictionary::LookupResult::NotFound) {
    const std::string_view query(lookupText);
    const bool hasWhitespace = std::any_of(query.begin(), query.end(), [](const unsigned char c) {
      return std::isspace(c) != 0;
    });
    if (hasWhitespace) {
      std::string combined;
      std::string termDefinition;
      std::string termHeadword;
      const bool htmlDefinitions = dict.definitionsAreHtml();
      size_t termCount = 0;
      size_t position = 0;
      bool lookupFailed = false;
      while (position < query.size() && termCount < MAX_MULTIWORD_TERMS) {
        while (position < query.size() && std::isspace(static_cast<unsigned char>(query[position]))) ++position;
        const size_t start = position;
        while (position < query.size() && !std::isspace(static_cast<unsigned char>(query[position]))) ++position;
        if (start == position) continue;
        ++termCount;

        const std::string term(query.substr(start, position - start));
        Dictionary::LookupResult termResult = Dictionary::LookupResult::NotFound;
        if (!dict.lookup(term.c_str(), termDefinition, termHeadword, &termResult)) {
          if (termResult != Dictionary::LookupResult::NotFound) {
            result = termResult;
            lookupFailed = true;
            break;
          }
          continue;
        }

        std::string plainDefinition = htmlDefinitions ? htmlToPlainText(termDefinition) : termDefinition;
        const std::string_view label = termHeadword.empty() ? std::string_view(term) : std::string_view(termHeadword);
        const size_t separatorBytes = combined.empty() ? 0 : 2;
        const size_t prefixBytes = separatorBytes + label.size() + 1;
        if (combined.size() + prefixBytes >= MAX_MULTIWORD_DEFINITION_BYTES) break;
        const size_t available = MAX_MULTIWORD_DEFINITION_BYTES - combined.size() - prefixBytes;
        if (plainDefinition.size() > available) {
          if (available <= 3) break;
          size_t clipped = available - 3;
          while (clipped > 0 && (static_cast<unsigned char>(plainDefinition[clipped]) & 0xC0) == 0x80) --clipped;
          plainDefinition.resize(clipped);
          plainDefinition += "...";
        }
        if (separatorBytes) combined += "\n\n";
        combined.append(label.data(), label.size());
        combined += '\n';
        combined += plainDefinition;
      }

      if (lookupFailed) {
        definition.clear();
        headword.clear();
        found = false;
      } else if (!combined.empty()) {
        definition = std::move(combined);
        headword = lookupText;
        found = true;
        combinedMultiwordDefinition = true;
        result = Dictionary::LookupResult::Found;
      }
    }
  }

  if (found) {
    vocabulary::CaptureNotice notice;
    if (captureRequested && !source) {
      notice.attempted = true;
      notice.status = vocabulary::Capture::Failed;
    }
    {
      RenderLock lock;
      popup = Popup::None;
      page.reset();
    }
    auto activity = makeUniqueNoThrow<DictionaryDefinitionActivity>(
      renderer, mappedInput, std::move(headword), std::move(definition),
      combinedMultiwordDefinition ? false : dict.definitionsAreHtml(), notice);
    if (!activity) {
      LOG_ERR("DICT", "OOM: definition activity");
      popup = Popup::Error;
      popupMsg = StrId::STR_DICT_LOW_MEMORY;
      popupTime = millis();
      requestUpdate();
      return;
    }
    if (source) {
      source->purpose = translate ? vocabulary::Purpose::Language : vocabulary::Purpose::Vocabulary;
      if (!vocabulary::copy(source->surface, sizeof(source->surface), lookupText.c_str())) source->surface[0] = 0;
      activity->offerVocabulary(std::move(source),
                                lookupDictionary.c_str(), sourceLanguage.c_str());
    }
    startActivityForResult(std::move(activity), [this](const ActivityResult&) { finish(); });
    return;
  }
  // Name the failure: a genuine miss is "Not found"; a word that WAS found but
  // couldn't be read is a real error — and we distinguish decompression from a
  // low-memory allocation from a generic read error.
  if (!ok) {
    popup = Popup::Error;
    // An index build allocates a scan buffer, so it fails the same way lookups
    // do on a fragmented heap — name that rather than a generic error.
    switch (indexResult) {
      case Dictionary::IndexResult::LowMemory:
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::IndexResult::ReadError:
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::IndexResult::Ok:
      default:
        popupMsg = translate ? StrId::STR_WORD_TRANSLATION_SETUP : StrId::STR_WORD_DEFINITION_SETUP;
        break;
    }
  } else {
    switch (result) {
      case Dictionary::LookupResult::Decompress:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_DECOMPRESS_ERROR;
        break;
      case Dictionary::LookupResult::LowMemory:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_LOW_MEMORY;
        break;
      case Dictionary::LookupResult::ReadError:
        popup = Popup::Error;
        popupMsg = StrId::STR_DICT_READ_FAILED;
        break;
      case Dictionary::LookupResult::NotFound:
      default:
        popup = Popup::NotFound;
        popupMsg = StrId::STR_DICT_NOT_FOUND;
        break;
    }
  }
  popupTime = millis();
  requestUpdate();
}

void DictionaryWordSelectActivity::loop() {
  if (lookupPending) {
    lookupPending = false;
    performLookup();
    return;
  }
  if (popup == Popup::NotFound || popup == Popup::Error) {
    if (millis() - popupTime >= POPUP_DURATION_MS) {
      ActivityResult res;
      res.isCancelled = true;
      setResult(std::move(res));
      finish();
    }
  }
}

void DictionaryWordSelectActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto* fcm = renderer.getFontCacheManager();
  auto scope = fcm->createPrewarmScope();
  if (page) page->render(renderer, fontId, marginLeft, marginTop);
  scope.endScanAndPrewarm();
  if (page) page->render(renderer, fontId, marginLeft, marginTop);

  if (popup != Popup::None) {
    GUI.drawPopup(renderer, I18N.get(popupMsg));
    return;
  }
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
