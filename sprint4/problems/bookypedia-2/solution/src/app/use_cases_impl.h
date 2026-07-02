#pragma once
#include "../domain/author_fwd.h"
#include "../domain/book_fwd.h"
#include "use_cases.h"

namespace app {

class UseCasesImpl : public UseCases {
public:
  explicit UseCasesImpl(domain::AuthorRepository &authors,
                        domain::BookRepository &books)
      : authors_{authors}, books_{books} {}

  std::string AddAuthor(const std::string &name) override;
  std::vector<ui::detail::AuthorInfo> ShowAuthors() const override;
  std::string AddBook(ui::detail::AddBookParams params) override;
  std::vector<ui::detail::BookInfo>
  ShowAuthorBooks(const std::string &author_id) const override;
  std::vector<ui::detail::BookInfo> ShowBooks() const override;
  std::vector<ui::detail::BookInfoEx> ShowBooksEx() const override;
  std::optional<ui::detail::AuthorInfo>
  FindAuthorByName(const std::string &author_name) const override;

  std::string DeleteAuthorByName(const std::string &author_name) const override;
  std::string DeleteAuthorById(const std::string &author_id) const override;

  std::string
  EditAuthorByName(const std::string &author_name,
                   const std::string &new_author_name) const override;
  std::string EditAuthorById(const std::string &author_id,
                             const std::string &new_author_name) const override;

  std::vector<ui::detail::BookInfoEx>
  GetBookByTitle(const std::string &book_title) const override;

  std::string DeleteBookById(const std::string &id) const override;

  std::string
  EditBookById(const ui::detail::EditBookParams &edit_book) const override;

private:
  domain::AuthorRepository &authors_;
  domain::BookRepository &books_;
};

} // namespace app