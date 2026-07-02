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
    
    // Запрещаем копирование
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    
    // Инициализация таблицы
    void Initialize();
    
    // Добавление рекорда
    void AddRecord(const model::RetiredPlayer& player);
    
    // Получение рекордов с пагинацией
    std::vector<model::RetiredPlayer> GetRecords(size_t start, size_t max_items);
    
    // Проверка, существует ли игрок с таким именем (для обновления рекорда)
    bool PlayerExists(const std::string& name);
    
    // Обновление рекорда игрока
    void UpdateRecord(const model::RetiredPlayer& player);
    
private:
    std::unique_ptr<pqxx::connection> connection_;
    std::mutex mutex_;
    
    void CreateTableIfNotExists();
    void EnsureIndexes();
};

} // namespace db