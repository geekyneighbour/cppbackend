#pragma once

#include <string>
#include <vector>
#include "../util/tagged_uuid.h"
#include "../app/use_cases.h"

namespace domain {

namespace detail {
struct BookTag {};
}  // namespace detail

using BookId = util::TaggedUUID<detail::BookTag>;

class Book {
public:
    Book(BookId id, std::string title, int publication_year)
        : id_(std::move(id))
        , title_(std::move(title))
        , publication_year_(publication_year) {
    }

    const BookId& GetId() const noexcept { return id_; }
    const std::string& GetTitle() const noexcept { return title_; }
    int GetPublicationYear() const noexcept { return publication_year_; }

private:
    BookId id_;
    std::string title_;
    int publication_year_;
};

class BookRepository {
public:
    virtual ~BookRepository() = default;
    
    virtual void Save(const Book& book, const std::string& author_id) = 0;
    virtual void SaveTags(const std::string& book_id, const std::vector<std::string>& tags) = 0;
    virtual std::vector<ui::detail::BookInfo> GetAllBooks() = 0;
    virtual std::vector<ui::detail::BookInfo> GetBooksByAuthor(const std::string& author_id) = 0;
    virtual std::vector<ui::detail::BookInfo> GetBooksByTitle(const std::string& title) = 0;
    virtual ui::detail::BookDetailInfo GetBookDetail(const std::string& book_id) = 0;
    virtual void DeleteBook(const std::string& book_id) = 0;
    virtual void EditBook(const std::string& book_id, const std::string& title, 
                          int publication_year, const std::vector<std::string>& tags) = 0;
};

}  // namespace domain