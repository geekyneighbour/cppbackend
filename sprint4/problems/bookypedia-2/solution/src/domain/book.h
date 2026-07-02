#pragma once
#include <set>
#include <string>
#include <vector>

#include "../ui/view.h"
#include "../util/tagged_uuid.h"

namespace domain {

namespace detail {
struct BookTag {};
} // namespace detail

using BookId = util::TaggedUUID<detail::BookTag>;

class Book {
public:
  Book(BookId id, std::string title, int publication_year)
      : id_(std::move(id)), title_(std::move(title)),
        publication_year_{publication_year} {}

  const BookId &GetId() const noexcept { return id_; }

  const std::string &GetTitle() const noexcept { return title_; }

  int GetPublicationYear() const noexcept { return publication_year_; }

private:
  BookId id_;
  std::string title_;
  int publication_year_;
};

class BookRepository {
public:
  virtual std::string Save(domain::Book book, std::string author_id,
                           std::set<std::string> tags) = 0;
  virtual std::vector<ui::detail::BookInfo> ShowBooks() const = 0;

  virtual std::vector<ui::detail::BookInfoEx> ShowBooksEx() const = 0;

  virtual std::vector<ui::detail::BookInfo>
  ShowAuthorBooks(const std::string &author_id) const = 0;

  virtual std::vector<ui::detail::BookInfoEx>
  GetBookByTitle(const std::string &book_title) const = 0;

  virtual std::string DeleteBookById(const std::string &id) const = 0;

  virtual std::string
  EditBookById(const ui::detail::EditBookParams &edit_book) const = 0;

protected:
  ~BookRepository() = default;
};

} // namespace domain