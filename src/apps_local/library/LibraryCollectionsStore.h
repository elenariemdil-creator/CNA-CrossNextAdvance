#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace libraryui {

struct Collection {
  std::string name;
  std::vector<uint64_t> bookHashes;
};

class CollectionsStore : public PersistableStore<CollectionsStore> {
 public:
  static constexpr size_t MAX_COLLECTIONS = 8;
  static constexpr size_t MAX_BOOKS_PER_COLLECTION = 256;
  static constexpr size_t MAX_NAME_BYTES = 40;

  static CollectionsStore& getInstance() { return PersistableStore<CollectionsStore>::getLoadedInstance(); }
  static const char* getFilePath() { return "/.crosspoint/collections.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const std::vector<Collection>& collections() const { return entries; }
  bool create(const std::string& name);
  bool remove(size_t collection);
  bool addBook(size_t collection, uint64_t pathHash);
  bool removeBook(size_t collection, uint64_t pathHash);
  bool contains(size_t collection, uint64_t pathHash) const;

 private:
  CollectionsStore() = default;
  friend class PersistableStore<CollectionsStore>;

  std::vector<Collection> entries;
};

}  // namespace libraryui
