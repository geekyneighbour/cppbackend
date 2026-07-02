#pragma once
#include <pqxx/connection>
#include <pqxx/transaction>
#include <vector>

#include "../domain/author.h"
#include "../domain/book.h"

namespace postgres {

class AuthorRepositoryImpl : public domain::AuthorRepository {
public:
  explicit AuthorRepositoryImpl(pqxx::connection &connection)
      : connection_{connection} {}

  std::string Save(const domain::Author &author) override;

  std::vector<ui::detail::AuthorInfo> ShowAuthors() const override;

  std::optional<ui::detail::AuthorInfo>
  ShowAuthorByName(const std::string &author_name) const override;

  std::string DeleteAuthorByName(const std::string &author_name) const override;
  std::string DeleteAuthorById(const std::string &author_id) const override;

  std::string
  EditAuthorByName(const std::string &author_name,
                   const std::string &new_author_name) const override;
  std::string EditAuthorById(const std::string &author_id,
                             const std::string &new_author_name) const override;

private:
  pqxx::connection &connection_;
};

class BookRepositoryImpl : public domain::BookRepository {
public:
  explicit BookRepositoryImpl(pqxx::connection &connection)
      : connection_{connection} {}

  std::string Save(domain::Book book, std::string author_id,
                   std::set<std::string> tags) override;
  std::vector<ui::detail::BookInfo> ShowBooks() const override;
  std::vector<ui::detail::BookInfoEx> ShowBooksEx() const override;
  std::vector<ui::detail::BookInfo>
  ShowAuthorBooks(const std::string &author_id) const override;

  std::vector<ui::detail::BookInfoEx>
  GetBookByTitle(const std::string &book_title) const override;

  std::string DeleteBookById(const std::string &id) const override;

  std::string
  EditBookById(const ui::detail::EditBookParams &edit_book) const override;

private:
  pqxx::connection &connection_;
};

class Database {
public:
  explicit Database(pqxx::connection connection);

  AuthorRepositoryImpl &GetAuthors() & { return authors_; }
  BookRepositoryImpl &GetBooks() & { return books_; }

private:
  pqxx::connection connection_;
  AuthorRepositoryImpl authors_{connection_};
  BookRepositoryImpl books_{connection_};
};

} // namespace postgres