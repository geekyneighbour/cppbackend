#include "database_manager.h"
#include <stdexcept>
#include <iostream>

namespace db {

DatabaseManager::DatabaseManager(const std::string& connection_string) {
    try {
        connection_ = std::make_unique<pqxx::connection>(connection_string);
        if (!connection_->is_open()) {
            throw std::runtime_error("Failed to connect to database");
        }
        Initialize();
    } catch (const std::exception& e) {
        throw std::runtime_error("Database connection error: " + std::string(e.what()));
    }
}

void DatabaseManager::Initialize() {
    CreateTableIfNotExists();
    EnsureIndexes();
}

void DatabaseManager::CreateTableIfNotExists() {
    const std::string create_table = R"(
        CREATE TABLE IF NOT EXISTS retired_players (
            id SERIAL PRIMARY KEY,
            name VARCHAR(255) NOT NULL,
            score INTEGER NOT NULL DEFAULT 0,
            play_time DOUBLE PRECISION NOT NULL DEFAULT 0,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";
    
    try {
        pqxx::work txn(*connection_);
        txn.exec(create_table);
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Failed to create table: " << e.what() << std::endl;
        throw;
    }
}

void DatabaseManager::EnsureIndexes() {
    const std::vector<std::string> indexes = {
        "CREATE INDEX IF NOT EXISTS idx_score_play_time ON retired_players (score DESC, play_time ASC, name ASC);",
        "CREATE INDEX IF NOT EXISTS idx_name ON retired_players (name);"
    };
    
    try {
        pqxx::work txn(*connection_);
        for (const auto& idx : indexes) {
            txn.exec(idx);
        }
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Failed to create indexes: " << e.what() << std::endl;
        throw;
    }
}

void DatabaseManager::AddRecord(const model::RetiredPlayer& player) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Проверяем, есть ли уже игрок с таким именем
    if (PlayerExists(player.name)) {
        UpdateRecord(player);
        return;
    }
    
    const std::string query = R"(
        INSERT INTO retired_players (name, score, play_time)
        VALUES ($1, $2, $3);
    )";
    
    try {
        pqxx::work txn(*connection_);
        txn.exec_params(query, player.name, player.score, player.play_time);
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Failed to add record: " << e.what() << std::endl;
        throw;
    }
}

void DatabaseManager::UpdateRecord(const model::RetiredPlayer& player) {
    const std::string query = R"(
        UPDATE retired_players 
        SET score = $1, play_time = $2, created_at = CURRENT_TIMESTAMP
        WHERE name = $3;
    )";
    
    try {
        pqxx::work txn(*connection_);
        txn.exec_params(query, player.score, player.play_time, player.name);
        txn.commit();
    } catch (const std::exception& e) {
        std::cerr << "Failed to update record: " << e.what() << std::endl;
        throw;
    }
}

bool DatabaseManager::PlayerExists(const std::string& name) {
    const std::string query = "SELECT COUNT(*) FROM retired_players WHERE name = $1;";
    
    try {
        pqxx::work txn(*connection_);
        auto result = txn.exec_params(query, name);
        txn.commit();
        
        if (!result.empty()) {
            return result[0][0].as<int>() > 0;
        }
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Failed to check player existence: " << e.what() << std::endl;
        return false;
    }
}

std::vector<model::RetiredPlayer> DatabaseManager::GetRecords(size_t start, size_t max_items) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Ограничиваем max_items до 100
    if (max_items > 100) {
        throw std::invalid_argument("maxItems cannot exceed 100");
    }
    
    const std::string query = R"(
        SELECT name, score, play_time 
        FROM retired_players 
        ORDER BY score DESC, play_time ASC, name ASC 
        LIMIT $1 OFFSET $2;
    )";
    
    std::vector<model::RetiredPlayer> records;
    
    try {
        pqxx::work txn(*connection_);
        auto result = txn.exec_params(query, max_items, start);
        txn.commit();
        
        for (const auto& row : result) {
            model::RetiredPlayer player;
            player.name = row[0].as<std::string>();
            player.score = row[1].as<int>();
            player.play_time = row[2].as<double>();
            records.push_back(player);
        }
    } catch (const std::exception& e) {
        std::cerr << "Failed to get records: " << e.what() << std::endl;
        throw;
    }
    
    return records;
}

} // namespace db