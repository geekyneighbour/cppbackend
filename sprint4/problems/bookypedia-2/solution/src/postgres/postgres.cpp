#include "postgres.h"

#include <pqxx/zview.hxx>
#include <set>

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
    
    for (pqxx::row_size_type i = 0; i < result.size(); ++i) {
        const auto row = result[i];
        ui::detail::AuthorInfo info;
        info.id = row[0].as<std::string>();
        info.name = row[1].as<std::string>();
        authors.push_back(info);
    }
    
    return authors;
}

void AuthorRepositoryImpl::Delete(const std::string& author_id) {
    pqxx::work work{connection_};
    
    // Сначала удаляем теги книг автора
    work.exec_params(
        "DELETE FROM book_tags WHERE book_id IN (SELECT id FROM books WHERE author_id = $1)"_zv,
        author_id
    );
    
    // Затем удаляем книги автора
    work.exec_params(
        "DELETE FROM books WHERE author_id = $1"_zv,
        author_id
    );
    
    // И наконец удаляем самого автора
    work.exec_params(
        "DELETE FROM authors WHERE id = $1"_zv,
        author_id
    );
    
    work.commit();
}

void AuthorRepositoryImpl::Edit(const std::string& author_id, const std::string& new_name) {
    pqxx::work work{connection_};
    
    // Проверяем, существует ли автор
    auto check = work.exec_params(
        "SELECT id FROM authors WHERE id = $1"_zv,
        author_id
    );
    
    if (check.empty()) {
        throw std::runtime_error("Author not found");
    }
    
    // Обновляем имя автора
    work.exec_params(
        "UPDATE authors SET name = $1 WHERE id = $2"_zv,
        new_name, author_id
    );
    
    work.commit();
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

void BookRepositoryImpl::SaveTags(const std::string& book_id, const std::vector<std::string>& tags) {
    pqxx::work work{connection_};
    
    // Удаляем старые теги
    work.exec_params(
        "DELETE FROM book_tags WHERE book_id = $1"_zv,
        book_id
    );
    
    // Добавляем новые теги
    for (const auto& tag : tags) {
        if (!tag.empty()) {
            work.exec_params(
                "INSERT INTO book_tags (book_id, tag) VALUES ($1, $2) ON CONFLICT DO NOTHING"_zv,
                book_id, tag
            );
        }
    }
    
    work.commit();
}

std::vector<ui::detail::BookInfo> BookRepositoryImpl::GetAllBooks() {
    pqxx::work work{connection_};
    std::vector<ui::detail::BookInfo> books;
    
    auto result = work.exec(
        R"(
SELECT b.id, b.title, b.publication_year, a.name 
FROM books b
JOIN authors a ON b.author_id = a.id
ORDER BY b.title, a.name, b.publication_year
)"_zv
    );
    
    for (pqxx::row_size_type i = 0; i < result.size(); ++i) {
        const auto row = result[i];
        ui::detail::BookInfo info;
        info.id = row[0].as<std::string>();
        info.title = row[1].as<std::string>();
        info.publication_year = row[2].as<int>();
        info.author_name = row[3].as<std::string>();
        books.push_back(info);
    }
    
    return books;
}

std::vector<ui::detail::BookInfo> BookRepositoryImpl::GetBooksByAuthor(const std::string& author_id) {
    pqxx::work work{connection_};
    std::vector<ui::detail::BookInfo> books;
    
    auto result = work.exec_params(
        R"(
SELECT b.id, b.title, b.publication_year, a.name 
FROM books b
JOIN authors a ON b.author_id = a.id
WHERE b.author_id = $1
ORDER BY b.title, b.publication_year
)"_zv,
        author_id
    );
    
    for (pqxx::row_size_type i = 0; i < result.size(); ++i) {
        const auto row = result[i];
        ui::detail::BookInfo info;
        info.id = row[0].as<std::string>();
        info.title = row[1].as<std::string>();
        info.publication_year = row[2].as<int>();
        info.author_name = row[3].as<std::string>();
        books.push_back(info);
    }
    
    return books;
}

std::vector<ui::detail::BookInfo> BookRepositoryImpl::GetBooksByTitle(const std::string& title) {
    pqxx::work work{connection_};
    std::vector<ui::detail::BookInfo> books;
    
    auto result = work.exec_params(
        R"(
SELECT b.id, b.title, b.publication_year, a.name 
FROM books b
JOIN authors a ON b.author_id = a.id
WHERE b.title = $1
ORDER BY a.name, b.publication_year
)"_zv,
        title
    );
    
    for (pqxx::row_size_type i = 0; i < result.size(); ++i) {
        const auto row = result[i];
        ui::detail::BookInfo info;
        info.id = row[0].as<std::string>();
        info.title = row[1].as<std::string>();
        info.publication_year = row[2].as<int>();
        info.author_name = row[3].as<std::string>();
        books.push_back(info);
    }
    
    return books;
}

ui::detail::BookDetailInfo BookRepositoryImpl::GetBookDetail(const std::string& book_id) {
    pqxx::work work{connection_};
    ui::detail::BookDetailInfo info;
    
    auto result = work.exec_params(
        R"(
SELECT b.title, b.publication_year, a.name, b.id
FROM books b
JOIN authors a ON b.author_id = a.id
WHERE b.id = $1
)"_zv,
        book_id
    );
    
    if (result.size() == 0) {
        return info;
    }
    
    const auto row = result[0];
    info.title = row[0].as<std::string>();
    info.publication_year = row[1].as<int>();
    info.author_name = row[2].as<std::string>();
    info.id = row[3].as<std::string>();
    
    // Получаем теги
    auto tags_result = work.exec_params(
        "SELECT tag FROM book_tags WHERE book_id = $1 ORDER BY tag"_zv,
        book_id
    );
    
    for (pqxx::row_size_type i = 0; i < tags_result.size(); ++i) {
        info.tags.push_back(tags_result[i][0].as<std::string>());
    }
    
    return info;
}

void BookRepositoryImpl::DeleteBook(const std::string& book_id) {
    pqxx::work work{connection_};
    
    // Сначала удаляем теги книги
    work.exec_params(
        "DELETE FROM book_tags WHERE book_id = $1"_zv,
        book_id
    );
    
    // Затем удаляем саму книгу
    work.exec_params(
        "DELETE FROM books WHERE id = $1"_zv,
        book_id
    );
    
    work.commit();
}

void BookRepositoryImpl::EditBook(const std::string& book_id, const std::string& title, 
                                  int publication_year, const std::vector<std::string>& tags) {
    pqxx::work work{connection_};
    
    work.exec_params(
        "UPDATE books SET title = $1, publication_year = $2 WHERE id = $3"_zv,
        title, publication_year, book_id
    );
    
    // Обновляем теги
    // Сначала удаляем старые
    work.exec_params(
        "DELETE FROM book_tags WHERE book_id = $1"_zv,
        book_id
    );
    
    // Добавляем новые
    for (const auto& tag : tags) {
        if (!tag.empty()) {
            work.exec_params(
                "INSERT INTO book_tags (book_id, tag) VALUES ($1, $2) ON CONFLICT DO NOTHING"_zv,
                book_id, tag
            );
        }
    }
    
    work.commit();
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
    
    // Create book_tags table
    work.exec(R"(
CREATE TABLE IF NOT EXISTS book_tags (
    book_id UUID NOT NULL,
    tag varchar(30) NOT NULL,
    CONSTRAINT fk_book FOREIGN KEY (book_id) REFERENCES books(id) ON DELETE CASCADE,
    CONSTRAINT unique_book_tag UNIQUE (book_id, tag)
);
)"_zv);
    
    work.commit();
}

}  // namespace postgres