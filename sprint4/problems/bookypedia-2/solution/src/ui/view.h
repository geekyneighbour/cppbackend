#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

#include "../app/use_cases.h"

namespace menu {
class Menu;
}

namespace app {
class UseCases;
}

namespace ui {
namespace detail {

struct AddBookParams {
    std::string title;
    std::string author_id;
    int publication_year = 0;
    std::vector<std::string> tags;
};

}  // namespace detail

class View {
public:
    View(menu::Menu& menu, app::UseCases& use_cases, std::istream& input, std::ostream& output);

private:
    bool AddAuthor(std::istream& cmd_input) const;
    bool AddBook(std::istream& cmd_input) const;
    bool ShowAuthors() const;
    bool ShowBooks() const;
    bool ShowAuthorBooks() const;
    bool DeleteAuthor(std::istream& cmd_input) const;
    bool EditAuthor(std::istream& cmd_input) const;
    bool ShowBook(std::istream& cmd_input) const;
    bool DeleteBook(std::istream& cmd_input) const;
    bool EditBook(std::istream& cmd_input) const;

    std::optional<detail::AddBookParams> GetBookParams(std::istream& cmd_input) const;
    std::optional<std::string> SelectAuthorFromList() const;
    std::optional<std::string> SelectBookFromList(const std::vector<ui::detail::BookInfo>& books) const;
    std::optional<std::string> SelectAuthorFromListOrInput(const std::string& input_name) const;
    std::vector<std::string> ParseTags(const std::string& tags_input) const;
    std::vector<ui::detail::AuthorInfo> GetAuthors() const;
    std::vector<ui::detail::BookInfo> GetBooks() const;
    std::vector<ui::detail::BookInfo> GetAuthorBooks(const std::string& author_id) const;

    menu::Menu& menu_;
    app::UseCases& use_cases_;
    std::istream& input_;
    std::ostream& output_;
};

}  // namespace ui