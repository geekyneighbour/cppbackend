#include "view.h"

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <iostream>
#include <set>
#include <sstream>
#include <istream>

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
    out << book.title << " by " << book.author_name << ", " << book.publication_year;
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
    menu_.AddAction("DeleteAuthor", "name", "Delete author",
                    std::bind(&View::DeleteAuthor, this, ph::_1));
    menu_.AddAction("EditAuthor", "name", "Edit author",
                    std::bind(&View::EditAuthor, this, ph::_1));
    menu_.AddAction("ShowBook", "title", "Show book details",
                    std::bind(&View::ShowBook, this, ph::_1));
    menu_.AddAction("DeleteBook", "title", "Delete book",
                    std::bind(&View::DeleteBook, this, ph::_1));
    menu_.AddAction("EditBook", "title", "Edit book",
                    std::bind(&View::EditBook, this, ph::_1));
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
            use_cases_.AddBook(params->title, params->publication_year, 
                             params->author_id, params->tags);
        }
    } catch (const std::exception&) {
        output_ << "Failed to add book"sv << std::endl;
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

    // Выбор автора
    output_ << "Enter author name or empty line to select from list:" << std::endl;
    std::string author_input;
    std::getline(input_, author_input);
    boost::algorithm::trim(author_input);
    
    if (author_input.empty()) {
        // Выбор из списка
        auto author_id = SelectAuthorFromList();
        if (!author_id.has_value()) {
            return std::nullopt;
        }
        params.author_id = *author_id;
    } else {
        // Поиск автора по имени
        auto authors = GetAuthors();
        auto it = std::find_if(authors.begin(), authors.end(),
            [&author_input](const ui::detail::AuthorInfo& info) {
                return info.name == author_input;
            });
        
        if (it != authors.end()) {
            params.author_id = it->id;
        } else {
            // Предлагаем добавить автора
            output_ << "No author found. Do you want to add " << author_input << " (y/n)?" << std::endl;
            std::string answer;
            std::getline(input_, answer);
            boost::algorithm::trim(answer);
            
            if (answer != "y" && answer != "Y") {
                output_ << "Failed to add book"sv << std::endl;
                return std::nullopt;
            }
            
            // Добавляем автора
            try {
                use_cases_.AddAuthor(author_input);
                // Получаем ID нового автора
                auto updated_authors = GetAuthors();
                auto new_it = std::find_if(updated_authors.begin(), updated_authors.end(),
                    [&author_input](const ui::detail::AuthorInfo& info) {
                        return info.name == author_input;
                    });
                if (new_it != updated_authors.end()) {
                    params.author_id = new_it->id;
                } else {
                    return std::nullopt;
                }
            } catch (const std::exception&) {
                output_ << "Failed to add author"sv << std::endl;
                return std::nullopt;
            }
        }
    }
    
    // Ввод тегов
    output_ << "Enter tags (comma separated):" << std::endl;
    std::string tags_input;
    std::getline(input_, tags_input);
    params.tags = ParseTags(tags_input);
    
    return params;
}

std::vector<std::string> View::ParseTags(const std::string& tags_input) const {
    std::vector<std::string> tags;
    std::vector<std::string> raw_tags;
    
    // Разбиваем по запятым
    boost::split(raw_tags, tags_input, boost::is_any_of(","));
    
    std::set<std::string> unique_tags;
    for (auto& tag : raw_tags) {
        // Удаляем пробелы в начале и конце
        boost::algorithm::trim(tag);
        
        // Удаляем лишние пробелы между словами
        std::string normalized_tag;
        bool in_space = false;
        for (char c : tag) {
            if (c == ' ') {
                if (!in_space) {
                    normalized_tag += ' ';
                    in_space = true;
                }
            } else {
                normalized_tag += c;
                in_space = false;
            }
        }
        // Удаляем пробел в конце
        if (!normalized_tag.empty() && normalized_tag.back() == ' ') {
            normalized_tag.pop_back();
        }
        
        if (!normalized_tag.empty() && normalized_tag.length() <= 30) {
            unique_tags.insert(normalized_tag);
        }
    }
    
    for (const auto& tag : unique_tags) {
        tags.push_back(tag);
    }
    
    return tags;
}

std::optional<std::string> View::SelectAuthorFromList() const {
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
        auto author_id = SelectAuthorFromList();
        if (author_id.has_value()) {
            auto books = GetAuthorBooks(*author_id);
            PrintVector(output_, books);
        }
    } catch (const std::exception&) {
        output_ << "Failed to show author books"sv << std::endl;
    }
    return true;
}

bool View::DeleteAuthor(std::istream& cmd_input) const {
    try {
        std::string author_name;
        std::getline(cmd_input, author_name);
        boost::algorithm::trim(author_name);
        
        if (author_name.empty()) {
            // Выбор из списка
            auto authors = GetAuthors();
            PrintVector(output_, authors);
            output_ << "Enter author # or empty line to cancel" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int author_idx;
            try {
                author_idx = std::stoi(str);
            } catch (const std::exception&) {
                output_ << "Failed to delete author"sv << std::endl;
                return true;
            }
            
            --author_idx;
            if (author_idx < 0 || author_idx >= static_cast<int>(authors.size())) {
                output_ << "Failed to delete author"sv << std::endl;
                return true;
            }
            
            author_name = authors[author_idx].name;
        }
        
        if (!use_cases_.DeleteAuthor(author_name)) {
            output_ << "Failed to delete author"sv << std::endl;
        }
    } catch (const std::exception&) {
        output_ << "Failed to delete author"sv << std::endl;
    }
    return true;
}

bool View::EditAuthor(std::istream& cmd_input) const {
    try {
        std::string author_name;
        std::getline(cmd_input, author_name);
        boost::algorithm::trim(author_name);
        
        if (author_name.empty()) {
            // Выбор из списка
            auto authors = GetAuthors();
            PrintVector(output_, authors);
            output_ << "Enter author # or empty line to cancel" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int author_idx;
            try {
                author_idx = std::stoi(str);
            } catch (const std::exception&) {
                output_ << "Failed to edit author"sv << std::endl;
                return true;
            }
            
            --author_idx;
            if (author_idx < 0 || author_idx >= static_cast<int>(authors.size())) {
                output_ << "Failed to edit author"sv << std::endl;
                return true;
            }
            
            author_name = authors[author_idx].name;
        }
        
        output_ << "Enter new name:" << std::endl;
        std::string new_name;
        std::getline(input_, new_name);
        boost::algorithm::trim(new_name);
        
        if (new_name.empty()) {
            output_ << "Failed to edit author"sv << std::endl;
            return true;
        }
        
        if (!use_cases_.EditAuthor(author_name, new_name)) {
            output_ << "Failed to edit author"sv << std::endl;
        }
    } catch (const std::exception&) {
        output_ << "Failed to edit author"sv << std::endl;
    }
    return true;
}

bool View::ShowBook(std::istream& cmd_input) const {
    try {
        std::string title;
        std::getline(cmd_input, title);
        boost::algorithm::trim(title);
        
        std::vector<ui::detail::BookInfo> books;
        
        if (title.empty()) {
            // Показываем все книги
            books = GetBooks();
            if (books.empty()) {
                return true;
            }
            PrintVector(output_, books);
            output_ << "Enter the book # or empty line to cancel:" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int book_idx;
            try {
                book_idx = std::stoi(str);
            } catch (const std::exception&) {
                return true;
            }
            
            --book_idx;
            if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                return true;
            }
            
            auto detail = use_cases_.GetBookDetail(books[book_idx].id);
            output_ << "Title: " << detail.title << std::endl;
            output_ << "Author: " << detail.author_name << std::endl;
            output_ << "Publication year: " << detail.publication_year << std::endl;
            if (!detail.tags.empty()) {
                output_ << "Tags: ";
                for (size_t i = 0; i < detail.tags.size(); ++i) {
                    if (i > 0) output_ << ", ";
                    output_ << detail.tags[i];
                }
                output_ << std::endl;
            }
        } else {
            books = use_cases_.GetBooksByTitle(title);
            if (books.empty()) {
                return true;
            }
            
            if (books.size() == 1) {
                auto detail = use_cases_.GetBookDetail(books[0].id);
                output_ << "Title: " << detail.title << std::endl;
                output_ << "Author: " << detail.author_name << std::endl;
                output_ << "Publication year: " << detail.publication_year << std::endl;
                if (!detail.tags.empty()) {
                    output_ << "Tags: ";
                    for (size_t i = 0; i < detail.tags.size(); ++i) {
                        if (i > 0) output_ << ", ";
                        output_ << detail.tags[i];
                    }
                    output_ << std::endl;
                }
            } else {
                PrintVector(output_, books);
                output_ << "Enter the book # or empty line to cancel:" << std::endl;
                
                std::string str;
                if (!std::getline(input_, str) || str.empty()) {
                    return true;
                }
                
                int book_idx;
                try {
                    book_idx = std::stoi(str);
                } catch (const std::exception&) {
                    return true;
                }
                
                --book_idx;
                if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                    return true;
                }
                
                auto detail = use_cases_.GetBookDetail(books[book_idx].id);
                output_ << "Title: " << detail.title << std::endl;
                output_ << "Author: " << detail.author_name << std::endl;
                output_ << "Publication year: " << detail.publication_year << std::endl;
                if (!detail.tags.empty()) {
                    output_ << "Tags: ";
                    for (size_t i = 0; i < detail.tags.size(); ++i) {
                        if (i > 0) output_ << ", ";
                        output_ << detail.tags[i];
                    }
                    output_ << std::endl;
                }
            }
        }
    } catch (const std::exception&) {
        // Ничего не выводим при ошибке
    }
    return true;
}

bool View::DeleteBook(std::istream& cmd_input) const {
    try {
        std::string title;
        std::getline(cmd_input, title);
        boost::algorithm::trim(title);
        
        std::vector<ui::detail::BookInfo> books;
        
        if (title.empty()) {
            // Показываем все книги
            books = GetBooks();
            if (books.empty()) {
                return true;
            }
            PrintVector(output_, books);
            output_ << "Enter the book # or empty line to cancel:" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int book_idx;
            try {
                book_idx = std::stoi(str);
            } catch (const std::exception&) {
                output_ << "Failed to delete book"sv << std::endl;
                return true;
            }
            
            --book_idx;
            if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                output_ << "Failed to delete book"sv << std::endl;
                return true;
            }
            
            if (!use_cases_.DeleteBook(books[book_idx].title)) {
                output_ << "Failed to delete book"sv << std::endl;
            }
        } else {
            books = use_cases_.GetBooksByTitle(title);
            if (books.empty()) {
                output_ << "Failed to delete book"sv << std::endl;
                return true;
            }
            
            if (books.size() == 1) {
                if (!use_cases_.DeleteBook(title)) {
                    output_ << "Failed to delete book"sv << std::endl;
                }
            } else {
                PrintVector(output_, books);
                output_ << "Enter the book # or empty line to cancel:" << std::endl;
                
                std::string str;
                if (!std::getline(input_, str) || str.empty()) {
                    return true;
                }
                
                int book_idx;
                try {
                    book_idx = std::stoi(str);
                } catch (const std::exception&) {
                    output_ << "Failed to delete book"sv << std::endl;
                    return true;
                }
                
                --book_idx;
                if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                    output_ << "Failed to delete book"sv << std::endl;
                    return true;
                }
                
                if (!use_cases_.DeleteBook(books[book_idx].title)) {
                    output_ << "Failed to delete book"sv << std::endl;
                }
            }
        }
    } catch (const std::exception&) {
        output_ << "Failed to delete book"sv << std::endl;
    }
    return true;
}

bool View::EditBook(std::istream& cmd_input) const {
    try {
        std::string title;
        std::getline(cmd_input, title);
        boost::algorithm::trim(title);
        
        std::vector<ui::detail::BookInfo> books;
        std::string current_title;
        int current_year = 0;
        std::string current_book_id;
        
        if (title.empty()) {
            // Показываем все книги
            books = GetBooks();
            if (books.empty()) {
                return true;
            }
            PrintVector(output_, books);
            output_ << "Enter the book # or empty line to cancel:" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int book_idx;
            try {
                book_idx = std::stoi(str);
            } catch (const std::exception&) {
                output_ << "Book not found"sv << std::endl;
                return true;
            }
            
            --book_idx;
            if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                output_ << "Book not found"sv << std::endl;
                return true;
            }
            
            current_title = books[book_idx].title;
            current_year = books[book_idx].publication_year;
            current_book_id = books[book_idx].id;
        } else {
            books = use_cases_.GetBooksByTitle(title);
            if (books.empty()) {
                output_ << "Book not found"sv << std::endl;
                return true;
            }
            
            if (books.size() == 1) {
                current_title = books[0].title;
                current_year = books[0].publication_year;
                current_book_id = books[0].id;
            } else {
                PrintVector(output_, books);
                output_ << "Enter the book # or empty line to cancel:" << std::endl;
                
                std::string str;
                if (!std::getline(input_, str) || str.empty()) {
                    return true;
                }
                
                int book_idx;
                try {
                    book_idx = std::stoi(str);
                } catch (const std::exception&) {
                    output_ << "Book not found"sv << std::endl;
                    return true;
                }
                
                --book_idx;
                if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                    output_ << "Book not found"sv << std::endl;
                    return true;
                }
                
                current_title = books[book_idx].title;
                current_year = books[book_idx].publication_year;
                current_book_id = books[book_idx].id;
            }
        }
        
        // Получаем детальную информацию о книге
        auto detail = use_cases_.GetBookDetail(current_book_id);
        
        // Новый заголовок
        output_ << "Enter new title or empty line to use the current one (" << current_title << "):" << std::endl;
        std::string new_title;
        std::getline(input_, new_title);
        boost::algorithm::trim(new_title);
        if (new_title.empty()) {
            new_title = current_title;
        }
        
        // Новый год
        output_ << "Enter publication year or empty line to use the current one (" << current_year << "):" << std::endl;
        std::string year_str;
        std::getline(input_, year_str);
        boost::algorithm::trim(year_str);
        int new_year = current_year;
        if (!year_str.empty()) {
            try {
                new_year = std::stoi(year_str);
            } catch (const std::exception&) {
                // Используем текущий год
            }
        }
        
        // Новые теги
        std::string tags_str;
        if (!detail.tags.empty()) {
            output_ << "Enter tags (current tags: ";
            for (size_t i = 0; i < detail.tags.size(); ++i) {
                if (i > 0) output_ << ", ";
                output_ << detail.tags[i];
            }
            output_ << "):" << std::endl;
        } else {
            output_ << "Enter tags (current tags: none):" << std::endl;
        }
        std::getline(input_, tags_str);
        
        std::vector<std::string> new_tags;
        if (!tags_str.empty()) {
            new_tags = ParseTags(tags_str);
        } else {
            new_tags = detail.tags;
        }
        
        if (!use_cases_.EditBook(current_title, new_title, new_year, new_tags)) {
            output_ << "Book not found"sv << std::endl;
        }
    } catch (const std::exception&) {
        output_ << "Book not found"sv << std::endl;
    }
    return true;
}

std::vector<ui::detail::AuthorInfo> View::GetAuthors() const {
    return use_cases_.GetAllAuthors();
}

std::vector<ui::detail::BookInfo> View::GetBooks() const {
    return use_cases_.GetAllBooks();
}

std::vector<ui::detail::BookInfo> View::GetAuthorBooks(const std::string& author_id) const {
    return use_cases_.GetBooksByAuthor(author_id);
}

}  // namespace ui