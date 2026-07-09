#include "postgres.h"

#include <pqxx/pqxx>
#include <pqxx/zview.hxx>

namespace postgres {

using namespace std::literals;
using pqxx::operator"" _zv;

void AuthorRepositoryImpl::Save(const domain::Author &author) {
  // Пока каждое обращение к репозиторию выполняется внутри отдельной транзакции
  // В будущих уроках вы узнаете про паттерн Unit of Work, при помощи которого
  // сможете несколько запросов выполнить в рамках одной транзакции. Вы также
  // может самостоятельно почитать информацию про этот паттерн и применить его
  // здесь.
  pqxx::work work{connection_};
  work.exec_params(
      R"(
INSERT INTO authors (id, name) VALUES ($1, $2)
ON CONFLICT (id) DO UPDATE SET name=$2;
)"_zv,
      author.GetId().ToString(), author.GetName());
  work.commit();
}

void BookRepositoryImpl::Save(const domain::Book &book,
                              const std::string &author_id) {
  pqxx::work work{connection_};
  work.exec_params(
      R"(
INSERT INTO books (id, author_id, title, publication_year) VALUES ($1, $2, $3, $4)
ON CONFLICT (id) DO UPDATE SET author_id=$2, title=$3, publication_year=$4 ;
)"_zv,
      book.GetId().ToString(), author_id, book.GetTitle(),
      book.GetPublicationYear());
  // std::cout << "commit" << "\n";
  work.commit();
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

  work.commit();
}

std::vector<ui::detail::AuthorInfo> AuthorRepositoryImpl::ShowAuthors() {
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

std::vector<ui::detail::BookInfo> BookRepositoryImpl::ShowBooks() {
  pqxx::work work{connection_};
  std::vector<ui::detail::BookInfo> books_info{};
  const auto result = work.exec_params(
      R"(SELECT title, publication_year FROM books ORDER BY title;)"_zv);
  for (const auto &row : result) {
    books_info.emplace_back(row[0].as<std::string>(), row[1].as<int16_t>());
  }
  return books_info;
};

std::vector<ui::detail::BookInfo>
BookRepositoryImpl::ShowAuthorBooks(const std::string &author_id) {
  pqxx::work work{connection_};
  std::vector<ui::detail::BookInfo> books_info{};
  const auto result = work.exec_params(
      R"(SELECT title, publication_year FROM books WHERE author_id=$1 ORDER BY publication_year;)"_zv,
      author_id);
  for (const auto &row : result) {
    books_info.emplace_back(row[0].as<std::string>(), row[1].as<int16_t>());
  }
  return books_info;
};
} // namespace postgres