#include "postgres.h"

#include <pqxx/zview.hxx>
#include <iostream>

namespace postgres {

using namespace std::literals;
using pqxx::operator"" _zv;

// AuthorRepositoryImpl
void AuthorRepositoryImpl::Save(const domain::Author& author) {
    pqxx::work work{connection_};
    work.exec_params(
        R"(
INSERT INTO authors (id, name) VALUES ($1, $2)
ON CONFLICT (id) DO UPDATE SET name=$2;
)"_zv,
        author.GetId().ToString(), author.GetName());
    work.commit();
}

std::vector<ui::detail::AuthorInfo> AuthorRepositoryImpl::GetAllAuthors() {
    pqxx::work work{connection_};
    std::vector<ui::detail::AuthorInfo> authors;
    
    auto result = work.exec("SELECT id, name FROM authors ORDER BY name"_zv);
    for (const auto& row : result) {
        authors.push_back({
            row[0].as<std::string>(),
            row[1].as<std::string>()
        });
    }
    
    return authors;
}

// BookRepositoryImpl
void BookRepositoryImpl::Save(const domain::Book& book, const std::string& author_id) {
    pqxx::work work{connection_};
    work.exec_params(
        R"(
INSERT INTO books (id, author_id, title, publication_year) 
VALUES ($1, $2, $3, $4)
ON CONFLICT (id) DO UPDATE SET 
    author_id=$2, 
    title=$3, 
    publication_year=$4;
)"_zv,
        book.GetId().ToString(), 
        author_id, 
        book.GetTitle(), 
        book.GetPublicationYear()
    );
    work.commit();
}

std::vector<ui::detail::BookInfo> BookRepositoryImpl::GetAllBooks() {
    pqxx::work work{connection_};
    std::vector<ui::detail::BookInfo> books;
    
    auto result = work.exec(
        "SELECT title, publication_year FROM books ORDER BY title"_zv
    );
    
    for (const auto& row : result) {
        books.push_back({
            row[0].as<std::string>(),
            row[1].as<int>()
        });
    }
    
    return books;
}

std::vector<ui::detail::BookInfo> BookRepositoryImpl::GetBooksByAuthor(const std::string& author_id) {
    pqxx::work work{connection_};
    std::vector<ui::detail::BookInfo> books;
    
    auto result = work.exec_params(
        "SELECT title, publication_year FROM books WHERE author_id=$1 ORDER BY publication_year, title"_zv,
        author_id
    );
    
    for (const auto& row : result) {
        books.push_back({
            row[0].as<std::string>(),
            row[1].as<int>()
        });
    }
    
    return books;
}

// Database
Database::Database(pqxx::connection connection)
    : connection_{std::move(connection)} {
    pqxx::work work{connection_};
    
    // Create authors table
    work.exec(R"(
CREATE TABLE IF NOT EXISTS authors (
    id UUID CONSTRAINT author_id_constraint PRIMARY KEY,
    name varchar(100) UNIQUE NOT NULL
);
)"_zv);
    
    // Create books table
    work.exec(R"(
CREATE TABLE IF NOT EXISTS books (
    id UUID CONSTRAINT book_id_constraint PRIMARY KEY,
    author_id UUID NOT NULL,
    title varchar(100) NOT NULL,
    publication_year integer NOT NULL,
    CONSTRAINT fk_author FOREIGN KEY (author_id) REFERENCES authors(id) ON DELETE CASCADE
);
)"_zv);
    
    work.commit();
}

}  // namespace postgres