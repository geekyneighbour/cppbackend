#include "use_cases_impl.h"

#include <stdexcept>
#include "../domain/author.h"
#include "../domain/book.h"

namespace app {

// Author methods
void UseCasesImpl::AddAuthor(const std::string& name) {
    if (name.empty()) {
        throw std::runtime_error("Author name cannot be empty");
    }
    authors_.Save({domain::AuthorId::New(), name});
}

std::vector<ui::detail::AuthorInfo> UseCasesImpl::GetAllAuthors() {
    return authors_.GetAllAuthors();
}

// Book methods
void UseCasesImpl::AddBook(const std::string& title, int publication_year, 
                           const std::string& author_id) {
    if (title.empty()) {
        throw std::runtime_error("Book title cannot be empty");
    }
    books_.Save({domain::BookId::New(), title, publication_year}, author_id);
}

std::vector<ui::detail::BookInfo> UseCasesImpl::GetAllBooks() {
    return books_.GetAllBooks();
}

std::vector<ui::detail::BookInfo> UseCasesImpl::GetBooksByAuthor(const std::string& author_id) {
    return books_.GetBooksByAuthor(author_id);
}

}  // namespace app