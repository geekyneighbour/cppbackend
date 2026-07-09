#include "use_cases_impl.h"

#include "../domain/author.h"
#include "../domain/book.h"

namespace app {
using namespace domain;

void UseCasesImpl::AddAuthor(const std::string &name) {
  authors_.Save({domain::AuthorId::New(), name});
}

void UseCasesImpl::AddBook(const std::string &title, int16_t publication_year,
                           const std::string &author_id) {
  books_.Save({::domain::BookId::New(), title, publication_year}, author_id);
};

std::vector<ui::detail::AuthorInfo> UseCasesImpl::ShowAuthors() {
  return authors_.ShowAuthors();
};
std::vector<ui::detail::BookInfo>
UseCasesImpl::ShowAuthorBooks(const std::string &author_id) {
  return books_.ShowAuthorBooks(author_id);
};
std::vector<ui::detail::BookInfo> UseCasesImpl::ShowBooks() {
  return books_.ShowBooks();
};

} // namespace app
