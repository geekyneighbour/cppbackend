#pragma once

#include "../ui/view.h"
#include <string>

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
