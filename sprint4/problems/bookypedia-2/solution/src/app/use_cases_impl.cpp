#include "use_cases_impl.h"

#include "../domain/author.h"
#include "../domain/book.h"

namespace app {
using namespace domain;

std::string UseCasesImpl::AddAuthor(const std::string &name) {
  const auto id = domain::AuthorId::New();
  const auto id_str = id.ToString();
  authors_.Save({std::move(id), name});
  return id_str;
}

std::string UseCasesImpl::AddBook(ui::detail::AddBookParams params) {
  books_.Save(
      {domain::BookId::New(), std::move(params.title), params.publication_year},
      std::move(params.author_id), std::move(params.tags));
  return "";
};

std::vector<ui::detail::AuthorInfo> UseCasesImpl::ShowAuthors() const {
  return authors_.ShowAuthors();
};
std::vector<ui::detail::BookInfo>
UseCasesImpl::ShowAuthorBooks(const std::string &author_id) const {
  return books_.ShowAuthorBooks(author_id);
};
std::vector<ui::detail::BookInfo> UseCasesImpl::ShowBooks() const {
  return books_.ShowBooks();
};

std::vector<ui::detail::BookInfoEx> UseCasesImpl::ShowBooksEx() const {
  return books_.ShowBooksEx();
};

std::optional<ui::detail::AuthorInfo>
UseCasesImpl::FindAuthorByName(const std::string &author_name) const {
  return authors_.ShowAuthorByName(author_name);
};

std::string
UseCasesImpl::DeleteAuthorByName(const std::string &author_name) const {
  return authors_.DeleteAuthorByName(author_name);
};

std::string UseCasesImpl::DeleteAuthorById(const std::string &author_id) const {
  return authors_.DeleteAuthorById(author_id);
};

std::string
UseCasesImpl::EditAuthorByName(const std::string &author_name,
                               const std::string &new_author_name) const {
  return authors_.EditAuthorByName(author_name, new_author_name);
};

std::string
UseCasesImpl::EditAuthorById(const std::string &author_id,
                             const std::string &new_author_name) const {
  return authors_.EditAuthorById(author_id, new_author_name);
};

std::vector<ui::detail::BookInfoEx>
UseCasesImpl::GetBookByTitle(const std::string &book_title) const {
  return books_.GetBookByTitle(book_title);
};

std::string UseCasesImpl::DeleteBookById(const std::string &id) const {
  return books_.DeleteBookById(id);
};

std::string
UseCasesImpl::EditBookById(const ui::detail::EditBookParams &edit_book) const {
  return books_.EditBookById(edit_book);
};
} // namespace app