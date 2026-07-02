#pragma once

#include "../domain/author_fwd.h"
#include "../domain/book_fwd.h"
#include "use_cases.h"

namespace app {

class UseCasesImpl : public UseCases {
public:
    explicit UseCasesImpl(domain::AuthorRepository& authors, domain::BookRepository& books)
        : authors_{authors}
        , books_{books} {
    }

    // Authors
    void AddAuthor(const std::string& name) override;
    std::vector<ui::detail::AuthorInfo> GetAllAuthors() override;
    
    // Books
    void AddBook(const std::string& title, int publication_year, 
                 const std::string& author_id) override;
    std::vector<ui::detail::BookInfo> GetAllBooks() override;
    std::vector<ui::detail::BookInfo> GetBooksByAuthor(const std::string& author_id) override;

private:
    domain::AuthorRepository& authors_;
    domain::BookRepository& books_;
};

}  // namespace app