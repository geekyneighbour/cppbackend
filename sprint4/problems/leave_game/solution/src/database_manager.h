#pragma once

#include <pqxx/pqxx>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include "model.h"

namespace db {

class DatabaseManager {
public:
    explicit DatabaseManager(const std::string& connection_string);
    ~DatabaseManager() = default;
    
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    
    void Initialize();
    void AddRecord(const model::RetiredPlayer& player);
    std::vector<model::RetiredPlayer> GetRecords(size_t start, size_t max_items);
    bool PlayerExists(const std::string& name);
    void UpdateRecord(const model::RetiredPlayer& player);
    
private:
    std::unique_ptr<pqxx::connection> connection_;
    std::mutex mutex_;
    
    void CreateTableIfNotExists();
    void EnsureIndexes();
};

} // namespace db