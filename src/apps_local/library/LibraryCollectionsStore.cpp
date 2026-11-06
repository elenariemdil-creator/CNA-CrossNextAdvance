#include "LibraryCollectionsStore.h"

#include <Logging.h>

#include <algorithm>
#include <utility>

namespace libraryui {

void CollectionsStore::toJson(JsonDocument& doc) const {
  auto array = doc["collections"].to<JsonArray>();
  for (const auto& collection : entries) {
    auto object = array.add<JsonObject>();
    object["name"] = collection.name;
    auto hashes = object["books"].to<JsonArray>();
    for (const uint64_t hash : collection.bookHashes) hashes.add(hash);
  }
}

bool CollectionsStore::fromJson(JsonVariantConst doc) {
  entries.clear();
  auto array = doc["collections"].as<JsonArrayConst>();
  entries.reserve(std::min<size_t>(array.size(), MAX_COLLECTIONS));
  bool skipped = false;
  for (JsonObjectConst object : array) {
    if (entries.size() == MAX_COLLECTIONS) {
      skipped = true;
      break;
    }
    const char* name = object["name"] | "";
    if (!name || !*name || std::char_traits<char>::length(name) > MAX_NAME_BYTES) {
      skipped = true;
      continue;
    }
    if (std::any_of(entries.begin(), entries.end(), [name](const Collection& item) { return item.name == name; })) {
      skipped = true;
      continue;
    }

    Collection collection;
    collection.name = name;
    auto hashes = object["books"].as<JsonArrayConst>();
    collection.bookHashes.reserve(std::min<size_t>(hashes.size(), MAX_BOOKS_PER_COLLECTION));
    for (JsonVariantConst value : hashes) {
      if (collection.bookHashes.size() == MAX_BOOKS_PER_COLLECTION) {
        skipped = true;
        break;
      }
      if (!value.is<uint64_t>()) {
        skipped = true;
        continue;
      }
      const auto hash = value.as<uint64_t>();
      if (std::find(collection.bookHashes.begin(), collection.bookHashes.end(), hash) == collection.bookHashes.end()) {
        collection.bookHashes.push_back(hash);
      } else {
        skipped = true;
      }
    }
    entries.push_back(std::move(collection));
  }
  if (skipped) LOG_ERR("LIB", "ignored invalid or over-capacity collection data");
  return true;
}

bool CollectionsStore::create(const std::string& name) {
  if (name.empty() || name.size() > MAX_NAME_BYTES || entries.size() >= MAX_COLLECTIONS ||
      std::any_of(entries.begin(), entries.end(), [&name](const Collection& item) { return item.name == name; }))
    return false;
  entries.push_back({name, {}});
  if (saveToFile()) return true;
  entries.pop_back();
  return false;
}

bool CollectionsStore::remove(const size_t collection) {
  if (collection >= entries.size()) return false;
  Collection removed = std::move(entries[collection]);
  entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(collection));
  if (saveToFile()) return true;
  entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(collection), std::move(removed));
  return false;
}

bool CollectionsStore::addBook(const size_t collection, const uint64_t pathHash) {
  if (collection >= entries.size()) return false;
  auto& hashes = entries[collection].bookHashes;
  if (std::find(hashes.begin(), hashes.end(), pathHash) != hashes.end()) return true;
  if (hashes.size() >= MAX_BOOKS_PER_COLLECTION) return false;
  hashes.push_back(pathHash);
  if (saveToFile()) return true;
  hashes.pop_back();
  return false;
}

bool CollectionsStore::removeBook(const size_t collection, const uint64_t pathHash) {
  if (collection >= entries.size()) return false;
  auto& hashes = entries[collection].bookHashes;
  auto found = std::find(hashes.begin(), hashes.end(), pathHash);
  if (found == hashes.end()) return false;
  const auto position = static_cast<size_t>(found - hashes.begin());
  hashes.erase(found);
  if (saveToFile()) return true;
  hashes.insert(hashes.begin() + static_cast<std::ptrdiff_t>(position), pathHash);
  return false;
}

bool CollectionsStore::contains(const size_t collection, const uint64_t pathHash) const {
  if (collection >= entries.size()) return false;
  const auto& hashes = entries[collection].bookHashes;
  return std::find(hashes.begin(), hashes.end(), pathHash) != hashes.end();
}

}  // namespace libraryui
