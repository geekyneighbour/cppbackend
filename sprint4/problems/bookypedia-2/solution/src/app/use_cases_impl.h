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
    bool DeleteAuthor(const std::string& author_name) override;
    bool EditAuthor(const std::string& current_name, const std::string& new_name) override;
    
    // Books
    void AddBook(const std::string& title, int publication_year, 
                 const std::string& author_id, const std::vector<std::string>& tags) override;
    std::vector<ui::detail::BookInfo> GetAllBooks() override;
    std::vector<ui::detail::BookInfo> GetBooksByAuthor(const std::string& author_id) override;
    std::vector<ui::detail::BookInfo> GetBooksByTitle(const std::string& title) override;
    ui::detail::BookDetailInfo GetBookDetail(const std::string& book_id) override;
    bool DeleteBook(const std::string& title) override;
    bool EditBook(const std::string& current_title, const std::string& new_title,
                 int new_year, const std::vector<std::string>& new_tags) override;

private:
    domain::AuthorRepository& authors_;
    domain::BookRepository& books_;
};

}  // namespace app