#include "database.hpp"
#include <iostream>

Database::Database(const std::string& connection_string) {
    conn_ = std::make_unique<pqxx::connection>(connection_string);
    if (!conn_->is_open()) {
        throw std::runtime_error("Failed to connect to database");
    }
}

void Database::create_table_if_not_exists() {
    pqxx::work txn(*conn_);
    
    txn.exec(
        "CREATE TABLE IF NOT EXISTS books ("
        "id SERIAL PRIMARY KEY, "
        "title VARCHAR(100) NOT NULL, "
        "author VARCHAR(100) NOT NULL, "
        "year INTEGER NOT NULL, "
        "ISBN CHAR(13) UNIQUE"
        ")"
    );
    
    txn.commit();
}

bool Database::add_book(const std::string& title, const std::string& author, 
                        int year, const std::string& isbn) {
    try {
        pqxx::work txn(*conn_);
        
        // Используем параметризованный запрос
        std::string query = 
            "INSERT INTO books (title, author, year, ISBN) "
            "VALUES ($1, $2, $3, $4)";
        
        if (isbn.empty()) {
            // Для NULL ISBN
            txn.exec_params(query, title, author, year, nullptr);
        } else {
            txn.exec_params(query, title, author, year, isbn);
        }
        
        txn.commit();
        return true;
        
    } catch (const pqxx::sql_error& e) {
        // Дублирующийся ISBN или другая SQL ошибка
        return false;
    } catch (const std::exception& e) {
        std::cerr << "Error adding book: " << e.what() << std::endl;
        return false;
    }
}

std::vector<Book> Database::get_all_books() {
    std::vector<Book> books;
    pqxx::read_transaction txn(*conn_);
    
    std::string query = 
        "SELECT id, title, author, year, ISBN FROM books "
        "ORDER BY year DESC, title ASC, author ASC, ISBN ASC";
    
    pqxx::result result = txn.exec(query);
    
    for (const auto& row : result) {
        Book book;
        book.id = row["id"].as<int>();
        book.title = row["title"].as<std::string>();
        book.author = row["author"].as<std::string>();
        book.year = row["year"].as<int>();
        
        if (row["ISBN"].is_null()) {
            book.isbn = "";
        } else {
            book.isbn = row["ISBN"].as<std::string>();
        }
        
        books.push_back(book);
    }
    
    return books;
}