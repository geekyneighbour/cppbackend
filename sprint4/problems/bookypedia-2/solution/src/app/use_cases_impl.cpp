#include "use_cases_impl.h"

#include <stdexcept>
#include <algorithm>
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

bool UseCasesImpl::DeleteAuthor(const std::string& author_name) {
    auto authors = authors_.GetAllAuthors();
    auto it = std::find_if(authors.begin(), authors.end(),
        [&author_name](const ui::detail::AuthorInfo& info) {
            return info.name == author_name;
        });
    
    if (it == authors.end()) {
        return false;
    }
    
    try {
        authors_.Delete(it->id);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool UseCasesImpl::EditAuthor(const std::string& current_name, const std::string& new_name) {
    auto authors = authors_.GetAllAuthors();
    auto it = std::find_if(authors.begin(), authors.end(),
        [&current_name](const ui::detail::AuthorInfo& info) {
            return info.name == current_name;
        });
    
    if (it == authors.end()) {
        return false;
    }
    
    try {
        authors_.Edit(it->id, new_name);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// Book methods
void UseCasesImpl::AddBook(const std::string& title, int publication_year, 
                           const std::string& author_id, const std::vector<std::string>& tags) {
    if (title.empty()) {
        throw std::runtime_error("Book title cannot be empty");
    }
    auto book = domain::Book{domain::BookId::New(), title, publication_year};
    books_.Save(book, author_id);
    books_.SaveTags(book.GetId().ToString(), tags);
}

std::vector<ui::detail::BookInfo> UseCasesImpl::GetAllBooks() {
    return books_.GetAllBooks();
}

std::vector<ui::detail::BookInfo> UseCasesImpl::GetBooksByAuthor(const std::string& author_id) {
    return books_.GetBooksByAuthor(author_id);
}

std::vector<ui::detail::BookInfo> UseCasesImpl::GetBooksByTitle(const std::string& title) {
    return books_.GetBooksByTitle(title);
}

ui::detail::BookDetailInfo UseCasesImpl::GetBookDetail(const std::string& book_id) {
    return books_.GetBookDetail(book_id);
}

bool UseCasesImpl::DeleteBook(const std::string& title) {
    auto books = books_.GetBooksByTitle(title);
    if (books.empty()) {
        return false;
    }
    
    try {
        // Удаляем первую книгу с таким названием (если их несколько, пользователь выбирает в UI)
        books_.DeleteBook(books[0].id);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool UseCasesImpl::EditBook(const std::string& current_title, const std::string& new_title,
                           int new_year, const std::vector<std::string>& new_tags) {
    auto books = books_.GetBooksByTitle(current_title);
    if (books.empty()) {
        return false;
    }
    
    try {
        books_.EditBook(books[0].id, new_title, new_year, new_tags);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace app