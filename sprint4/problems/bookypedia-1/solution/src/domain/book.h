#pragma once
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
  Book(BookId id, std::string title, int16_t publication_year)
      : id_(std::move(id)), title_(std::move(title)),
        publication_year_{publication_year} {}

  const BookId &GetId() const noexcept { return id_; }

  const std::string &GetTitle() const noexcept { return title_; }

  int16_t GetPublicationYear() const noexcept { return publication_year_; }

private:
  BookId id_;
  std::string title_;
  int16_t publication_year_;
};

class BookRepository {
public:
  virtual void Save(const domain::Book &book, const std::string &author_id) = 0;
  virtual std::vector<ui::detail::BookInfo> ShowBooks() = 0;

  virtual std::vector<ui::detail::BookInfo>
  ShowAuthorBooks(const std::string &author_id) = 0;

protected:
  ~BookRepository() = default;
};

} // namespace domain
