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

  void AddAuthor(const std::string &name) override;
  std::vector<ui::detail::AuthorInfo> ShowAuthors() override;
  void AddBook(const std::string &title, int16_t publication_year,
               const std::string &author_id) override;
  std::vector<ui::detail::BookInfo>
  ShowAuthorBooks(const std::string &author_id) override;
  std::vector<ui::detail::BookInfo> ShowBooks() override;

private:
  domain::AuthorRepository &authors_;
  domain::BookRepository &books_;
};

} // namespace app
