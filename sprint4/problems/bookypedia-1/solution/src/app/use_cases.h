#pragma once

#include <string>
#include <vector>

namespace ui {
namespace detail {
struct AuthorInfo;
struct BookInfo;
}
}

namespace app {

class UseCases {
public:
    virtual ~UseCases() = default;
    
    // Authors
    virtual void AddAuthor(const std::string& name) = 0;
    virtual std::vector<ui::detail::AuthorInfo> GetAllAuthors() = 0;
    
    // Books
    virtual void AddBook(const std::string& title, int publication_year, 
                         const std::string& author_id) = 0;
    virtual std::vector<ui::detail::BookInfo> GetAllBooks() = 0;
    virtual std::vector<ui::detail::BookInfo> GetBooksByAuthor(const std::string& author_id) = 0;
};

}  // namespace app