#include "postgres.h"

#include <optional>
#include <pqxx/pqxx>
#include <pqxx/zview.hxx>

namespace postgres {

using namespace std::literals;
using pqxx::operator"" _zv;

std::string AuthorRepositoryImpl::Save(const domain::Author &author) {
  pqxx::work work{connection_};
  work.exec_params(
      R"(
INSERT INTO authors (id, name) VALUES ($1, $2)
ON CONFLICT (id) DO UPDATE SET name=$2;
)"_zv,
      author.GetId().ToString(), author.GetName());
  work.commit();
  return "";
}

std::string BookRepositoryImpl::Save(domain::Book book, std::string author_id,
                                     std::set<std::string> tags) {
  pqxx::work work{connection_};
  const auto &book_id = book.GetId().ToString();
  work.exec_params(
      R"(
INSERT INTO books (id, author_id, title, publication_year) VALUES ($1, $2, $3, $4)
ON CONFLICT (id) DO UPDATE SET author_id=$2, title=$3, publication_year=$4 ;
)"_zv,
      book_id, author_id, book.GetTitle(), book.GetPublicationYear());

  for (const auto &tag : tags) {
    work.exec_params(
        R"(
INSERT INTO book_tags (book_id, tag) VALUES ($1, $2);
)"_zv,
        book_id, tag);
  }

  work.commit();
  return "";
}

Database::Database(pqxx::connection connection)
    : connection_{std::move(connection)} {
  pqxx::work work{connection_};
  work.exec(R"(
CREATE TABLE IF NOT EXISTS authors (
    id UUID CONSTRAINT author_id_constraint PRIMARY KEY,
    name varchar(100) UNIQUE NOT NULL
);
)"_zv);

  work.exec(R"(
CREATE TABLE IF NOT EXISTS books (
    id UUID CONSTRAINT book_id_constraint PRIMARY KEY,
    author_id UUID NOT NULL,
    title varchar(100) NOT NULL,
    publication_year integer NOT NULL
);
)"_zv);

  work.exec(R"(
CREATE TABLE IF NOT EXISTS book_tags (
    book_id UUID,
    tag varchar(30) NOT NULL
);
)"_zv);

  work.commit();
}

std::vector<ui::detail::AuthorInfo> AuthorRepositoryImpl::ShowAuthors() const {
  pqxx::work work{connection_};
  std::vector<ui::detail::AuthorInfo> authors_info{};
  const auto result =
      work.exec_params(R"(SELECT * FROM authors ORDER BY name;)"_zv);
  for (const auto &row : result) {
    authors_info.emplace_back(row[0].as<std::string>(),
                              row[1].as<std::string>());
  }
  return authors_info;
};

std::vector<ui::detail::BookInfo> BookRepositoryImpl::ShowBooks() const {
  pqxx::work work{connection_};
  std::vector<ui::detail::BookInfo> books_info{};
  const auto result = work.exec_params(
      R"(
SELECT b.title, b.publication_year, a.name 
FROM books b
JOIN authors a ON b.author_id = a.id
ORDER BY b.title, a.name, b.publication_year;
      )"_zv);
  for (const auto &row : result) {
    books_info.emplace_back(row[0].as<std::string>(), row[1].as<int>(),
                            row[2].as<std::string>());
  }
  return books_info;
}

std::vector<ui::detail::BookInfoEx> BookRepositoryImpl::ShowBooksEx() const {
  pqxx::work work{connection_};
  std::vector<ui::detail::BookInfoEx> books_info{};

  const auto result = work.exec_params(
      R"(
SELECT b.id, b.title, b.publication_year, a.name, 
       COALESCE(STRING_AGG(bt.tag, ', ' ORDER BY bt.tag), '') as tags
FROM books b
JOIN authors a ON b.author_id = a.id
LEFT JOIN book_tags bt ON b.id = bt.book_id
GROUP BY b.id, b.title, b.publication_year, a.name
ORDER BY b.title, a.name, b.publication_year;
      )"_zv);

  for (const auto &row : result) {

    ui::detail::BookInfoEx book_info;
    book_info.id = row[0].as<std::string>();
    book_info.title = row[1].as<std::string>();
    book_info.publication_year = row[2].as<int>();
    book_info.author_name = row[3].as<std::string>();
    book_info.tags = row[4].as<std::string>();

    books_info.push_back(std::move(book_info));
  }
  return books_info;
}

std::vector<ui::detail::BookInfo>
BookRepositoryImpl::ShowAuthorBooks(const std::string &author_id) const {
  pqxx::work work{connection_};
  std::vector<ui::detail::BookInfo> books_info{};
  const auto result = work.exec_params(
      R"(SELECT title, publication_year FROM books WHERE author_id=$1 ORDER BY publication_year;)"_zv,
      author_id);
  for (const auto &row : result) {
    books_info.emplace_back(row[0].as<std::string>(), row[1].as<int>());
  }
  return books_info;
};

std::optional<ui::detail::AuthorInfo>
AuthorRepositoryImpl::ShowAuthorByName(const std::string &author_name) const {
  pqxx::work work{connection_};
  std::optional<ui::detail::AuthorInfo> author_info{};
  const auto result = work.exec_params(
      R"(SELECT * FROM authors WHERE name=$1;)"_zv, author_name);
  if (result.empty()) {
    return std::nullopt;
  }

  const auto &row = result[0];
  return ui::detail::AuthorInfo{row[0].as<std::string>(),
                                row[1].as<std::string>()};
};

std::string
AuthorRepositoryImpl::DeleteAuthorByName(const std::string &author_name) const {
  pqxx::work work{connection_};

  auto check_result = work.exec_params(
      "SELECT id FROM authors WHERE name = $1 FOR UPDATE", author_name);

  if (check_result.empty()) {
    throw std::runtime_error("Author with name " + author_name + " not found");
  }

  auto result = work.exec_params(
      R"(
WITH deleted_authors AS (
    DELETE FROM authors 
    WHERE name = $1
    RETURNING id
),
deleted_books AS (
    DELETE FROM books 
    WHERE author_id IN (SELECT id FROM deleted_authors)
    RETURNING id
)
DELETE FROM book_tags 
WHERE book_id IN (SELECT id FROM deleted_books)
RETURNING (SELECT COUNT(*) FROM deleted_authors) > 0 AS deleted;
      )"_zv,
      author_name);

  work.commit();
  return "";
}

std::string
AuthorRepositoryImpl::DeleteAuthorById(const std::string &author_id) const {
  pqxx::work work{connection_};

  auto check_result = work.exec_params(
      "SELECT id FROM authors WHERE id = $1 FOR UPDATE", author_id);

  if (check_result.empty()) {
    throw std::runtime_error("Author with id " + author_id + " not found");
  }

  auto result = work.exec_params(
      R"(
WITH deleted_authors AS (
    DELETE FROM authors 
    WHERE id = $1
    RETURNING id
),
deleted_books AS (
    DELETE FROM books 
    WHERE author_id IN (SELECT id FROM deleted_authors)
    RETURNING id
)
DELETE FROM book_tags 
WHERE book_id IN (SELECT id FROM deleted_books)
RETURNING (SELECT COUNT(*) FROM deleted_authors) > 0 AS deleted;
      )"_zv,
      author_id);

  work.commit();
  return "";
}

std::string AuthorRepositoryImpl::EditAuthorByName(
    const std::string &author_name, const std::string &new_author_name) const {
  pqxx::work work{connection_};
  auto result = work.exec_params(
      R"(
UPDATE authors 
SET name = $2 
WHERE name = $1
RETURNING id;
      )"_zv,
      author_name, new_author_name);

  if (result.empty()) {
    throw std::runtime_error("Author with name " + author_name + " not found");
  }

  work.commit();

  return "";
}

std::string
AuthorRepositoryImpl::EditAuthorById(const std::string &author_id,
                                     const std::string &new_author_name) const {
  pqxx::work work{connection_};
  auto result = work.exec_params(
      R"(
UPDATE authors 
SET name = $2 
WHERE id = $1
RETURNING id;
      )"_zv,
      author_id, new_author_name);

  if (result.empty()) {
    throw std::runtime_error("Author with id " + author_id + " not found");
  }

  work.commit();

  return "";
}

std::vector<ui::detail::BookInfoEx>
BookRepositoryImpl::GetBookByTitle(const std::string &book_title) const {
  pqxx::work work{connection_};
  std::vector<ui::detail::BookInfoEx> books_info_tags{};

  const auto result = work.exec_params(
      R"(
SELECT b.title, b.publication_year, a.name, 
       COALESCE(STRING_AGG(bt.tag, ', ' ORDER BY bt.tag), '') as tags, b.id
FROM books b
JOIN authors a ON b.author_id = a.id
LEFT JOIN book_tags bt ON b.id = bt.book_id
WHERE b.title = $1
GROUP BY b.title, b.publication_year, a.name, b.id
ORDER BY a.name, b.publication_year;
      )"_zv,
      book_title);

  for (const auto &row : result) {
    books_info_tags.push_back(
        {row[0].as<std::string>(), row[1].as<int>(), row[2].as<std::string>(),
         row[3].as<std::string>(), row[4].as<std::string>()});
  }

  return books_info_tags;
}

std::string BookRepositoryImpl::DeleteBookById(const std::string &id) const {
  pqxx::work work{connection_};

  auto result = work.exec_params(
      R"(
WITH deleted_books AS (
    DELETE FROM books 
    WHERE id = $1
    RETURNING id
)
DELETE FROM book_tags 
WHERE book_id IN (SELECT id FROM deleted_books)
RETURNING (SELECT COUNT(*) FROM deleted_books) > 0 AS deleted;
      )"_zv,
      id);

  work.commit();
  return "";
}

std::string BookRepositoryImpl::EditBookById(
    const ui::detail::EditBookParams &edit_book) const {
  pqxx::work work{connection_};

  work.exec_params(
      R"(
UPDATE books 
SET title = $2, publication_year = $3 
WHERE id = $1;
      )"_zv,
      edit_book.id, edit_book.title, edit_book.publication_year);

  work.exec_params(
      R"(
DELETE FROM book_tags WHERE book_id = $1;
      )"_zv,
      edit_book.id);

  for (const auto &tag : edit_book.tags) {
    if (!tag.empty()) {
      work.exec_params(
          R"(
INSERT INTO book_tags (book_id, tag) VALUES ($1, $2);
          )"_zv,
          edit_book.id, tag);
    }
  }

  work.commit();
  return "";
}
} // namespace postgres