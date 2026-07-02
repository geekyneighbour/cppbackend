#pragma once

#include <string>
#include <vector>
#include <optional>
#include <set>
#include <ostream>

namespace ui {
namespace detail {

struct AddBookParams {
  std::string title;
  std::string author_id;
  int publication_year = 0;
  std::set<std::string> tags;
};

struct EditBookParams {
  std::string id;
  std::string title;
  int publication_year = 0;
  std::set<std::string> tags;
};

struct AuthorInfo {
  std::string id;
  std::string name;
};

struct BookInfo {
  std::string title;
  int publication_year;
  std::string author_name;
};

struct BookInfoEx : public BookInfo {
  std::string tags;
  std::string id;
};

std::ostream &operator<<(std::ostream &out, const AuthorInfo &author);
std::ostream &operator<<(std::ostream &out, const BookInfo &book);
std::ostream &operator<<(std::ostream &out, const BookInfoEx &book);

} // namespace detail
} // namespace ui

namespace app {

class UseCases {
public:
  virtual std::string AddAuthor(const std::string &name) = 0;
  virtual std::vector<ui::detail::AuthorInfo> ShowAuthors() const = 0;

  virtual std::string AddBook(ui::detail::AddBookParams params) = 0;
  virtual std::vector<ui::detail::BookInfo>
  ShowAuthorBooks(const std::string &author_id) const = 0;
  virtual std::vector<ui::detail::BookInfo> ShowBooks() const = 0;
  virtual std::vector<ui::detail::BookInfoEx> ShowBooksEx() const = 0;
  virtual std::optional<ui::detail::AuthorInfo>
  FindAuthorByName(const std::string &author_name) const = 0;

  virtual std::string
  DeleteAuthorByName(const std::string &author_name) const = 0;
  virtual std::string DeleteAuthorById(const std::string &author_id) const = 0;

  virtual std::string
  EditAuthorByName(const std::string &author_name,
                   const std::string &new_author_name) const = 0;
  virtual std::string
  EditAuthorById(const std::string &author_id,
                 const std::string &new_author_name) const = 0;

  virtual std::vector<ui::detail::BookInfoEx>
  GetBookByTitle(const std::string &book_title) const = 0;

  virtual std::string DeleteBookById(const std::string &id) const = 0;

  virtual std::string
  EditBookById(const ui::detail::EditBookParams &edit_book) const = 0;

protected:
  ~UseCases() = default;
};

} // namespace app