#pragma once

#include <pqxx/connection>
#include <pqxx/transaction>

#include "../domain/author.h"
#include "../domain/book.h"
#include "../app/use_cases.h"  // Добавляем для использования ui::detail::AuthorInfo и BookInfo

namespace postgres {

class AuthorRepositoryImpl : public domain::AuthorRepository {
public:
    explicit AuthorRepositoryImpl(pqxx::connection& connection)
        : connection_{connection} {
    }

    void Save(const domain::Author& author) override;
    std::vector<ui::detail::AuthorInfo> GetAllAuthors() override;

private:
    pqxx::connection& connection_;
};

class BookRepositoryImpl : public domain::BookRepository {
public:
    explicit BookRepositoryImpl(pqxx::connection& connection)
        : connection_{connection} {
    }

    void Save(const domain::Book& book, const std::string& author_id) override;
    std::vector<ui::detail::BookInfo> GetAllBooks() override;
    std::vector<ui::detail::BookInfo> GetBooksByAuthor(const std::string& author_id) override;

private:
    pqxx::connection& connection_;
};

class Database {
public:
    explicit Database(pqxx::connection connection);

    AuthorRepositoryImpl& GetAuthors() & { return authors_; }
    BookRepositoryImpl& GetBooks() & { return books_; }

private:
    pqxx::connection connection_;
    AuthorRepositoryImpl authors_{connection_};
    BookRepositoryImpl books_{connection_};
};

}  // namespace postgres