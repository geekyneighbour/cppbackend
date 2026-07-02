#pragma once

#include <string>
#include <vector>
#include <optional>

namespace ui {
namespace detail {
struct AuthorInfo {
    std::string id;
    std::string name;
};

struct BookInfo {
    std::string id;
    std::string title;
    int publication_year;
    std::string author_name;
};

struct BookDetailInfo {
    std::string id;
    std::string title;
    int publication_year;
    std::string author_name;
    std::vector<std::string> tags;
};
}  // namespace detail
}  // namespace ui

namespace app {

class UseCases {
public:
    virtual ~UseCases() = default;
    
    // Authors
    virtual void AddAuthor(const std::string& name) = 0;
    virtual std::vector<ui::detail::AuthorInfo> GetAllAuthors() = 0;
    virtual bool DeleteAuthor(const std::string& author_name) = 0;
    virtual bool EditAuthor(const std::string& current_name, const std::string& new_name) = 0;
    
    // Books
    virtual void AddBook(const std::string& title, int publication_year, 
                         const std::string& author_id, const std::vector<std::string>& tags) = 0;
    virtual std::vector<ui::detail::BookInfo> GetAllBooks() = 0;
    virtual std::vector<ui::detail::BookInfo> GetBooksByAuthor(const std::string& author_id) = 0;
    virtual std::vector<ui::detail::BookInfo> GetBooksByTitle(const std::string& title) = 0;
    virtual ui::detail::BookDetailInfo GetBookDetail(const std::string& book_id) = 0;
    virtual bool DeleteBook(const std::string& title) = 0;
    virtual bool EditBook(const std::string& current_title, const std::string& new_title,
                         int new_year, const std::vector<std::string>& new_tags) = 0;
};

}  // namespace app