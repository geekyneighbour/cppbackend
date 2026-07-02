#include "view.h"

#include <boost/algorithm/string/trim.hpp>
#include <cassert>
#include <iostream>

#include "../app/use_cases.h"
#include "../menu/menu.h"

using namespace std::literals;
namespace ph = std::placeholders;

namespace ui {
namespace detail {

std::ostream& operator<<(std::ostream& out, const AuthorInfo& author) {
    out << author.name;
    return out;
}

std::ostream& operator<<(std::ostream& out, const BookInfo& book) {
    out << book.title << ", " << book.publication_year;
    return out;
}

}  // namespace detail

template <typename T>
void PrintVector(std::ostream& out, const std::vector<T>& vector) {
    int i = 1;
    for (const auto& value : vector) {
        out << i++ << " " << value << std::endl;
    }
}

View::View(menu::Menu& menu, app::UseCases& use_cases, std::istream& input, std::ostream& output)
    : menu_{menu}
    , use_cases_{use_cases}
    , input_{input}
    , output_{output} {
    
    menu_.AddAction("AddAuthor", "name", "Adds author", 
                    std::bind(&View::AddAuthor, this, ph::_1));
    menu_.AddAction("AddBook", "<pub year> <title>", "Adds book",
                    std::bind(&View::AddBook, this, ph::_1));
    menu_.AddAction("ShowAuthors", "", "Show authors",
                    std::bind(&View::ShowAuthors, this));
    menu_.AddAction("ShowBooks", "", "Show books",
                    std::bind(&View::ShowBooks, this));
    menu_.AddAction("ShowAuthorBooks", "", "Show author books",
                    std::bind(&View::ShowAuthorBooks, this));
}

bool View::AddAuthor(std::istream& cmd_input) const {
    try {
        std::string name;
        std::getline(cmd_input, name);
        boost::algorithm::trim(name);
        
        if (name.empty()) {
            output_ << "Failed to add author"sv << std::endl;
            return true;
        }
        
        use_cases_.AddAuthor(name);
    } catch (const std::exception&) {
        output_ << "Failed to add author"sv << std::endl;
    }
    return true;
}

bool View::AddBook(std::istream& cmd_input) const {
    try {
        auto params = GetBookParams(cmd_input);
        if (params.has_value()) {
            use_cases_.AddBook(params->title, params->publication_year, params->author_id);
        }
    } catch (const std::exception&) {
        output_ << "Failed to add book"sv << std::endl;
    }
    return true;
}

bool View::ShowAuthors() const {
    auto authors = GetAuthors();
    PrintVector(output_, authors);
    return true;
}

bool View::ShowBooks() const {
    auto books = GetBooks();
    PrintVector(output_, books);
    return true;
}

bool View::ShowAuthorBooks() const {
    try {
        auto author_id = SelectAuthor();
        if (author_id.has_value()) {
            auto books = GetAuthorBooks(*author_id);
            PrintVector(output_, books);
        }
    } catch (const std::exception& e) {
        output_ << "Failed to show author books"sv << std::endl;
    }
    return true;
}

std::optional<detail::AddBookParams> View::GetBookParams(std::istream& cmd_input) const {
    detail::AddBookParams params;

    if (!(cmd_input >> params.publication_year)) {
        return std::nullopt;
    }
    
    std::getline(cmd_input, params.title);
    boost::algorithm::trim(params.title);
    
    if (params.title.empty()) {
        return std::nullopt;
    }

    auto author_id = SelectAuthor();
    if (!author_id.has_value()) {
        return std::nullopt;
    }
    
    params.author_id = *author_id;
    return params;
}

std::optional<std::string> View::SelectAuthor() const {
    output_ << "Select author:" << std::endl;
    auto authors = GetAuthors();
    PrintVector(output_, authors);
    output_ << "Enter author # or empty line to cancel" << std::endl;

    std::string str;
    if (!std::getline(input_, str) || str.empty()) {
        return std::nullopt;
    }

    int author_idx;
    try {
        author_idx = std::stoi(str);
    } catch (const std::exception&) {
        throw std::runtime_error("Invalid author number");
    }

    --author_idx;
    if (author_idx < 0 || author_idx >= static_cast<int>(authors.size())) {
        throw std::runtime_error("Invalid author number");
    }

    return authors[author_idx].id;
}

std::vector<detail::AuthorInfo> View::GetAuthors() const {
    return use_cases_.GetAllAuthors();
}

std::vector<detail::BookInfo> View::GetBooks() const {
    return use_cases_.GetAllBooks();
}

std::vector<detail::BookInfo> View::GetAuthorBooks(const std::string& author_id) const {
    return use_cases_.GetBooksByAuthor(author_id);
}

}  // namespace ui