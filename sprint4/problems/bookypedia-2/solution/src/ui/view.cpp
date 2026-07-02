#include "view.h"

#include <boost/algorithm/string/trim.hpp>
#include <cassert>
#include <exception>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <string>

#include "../app/use_cases.h"
#include "../menu/menu.h"

using namespace std::literals;
namespace ph = std::placeholders;

namespace ui {

template <typename T>
void PrintVector(std::ostream &out, const std::vector<T> &vector) {
  int i = 1;
  for (auto &value : vector) {
    out << i++ << " " << value << std::endl;
  }
}

View::View(menu::Menu &menu, app::UseCases &use_cases, std::istream &input,
           std::ostream &output)
    : menu_{menu}, use_cases_{use_cases}, input_{input}, output_{output} {
  menu_.AddAction( //
      "AddAuthor"s, "name"s, "Adds author"s,
      std::bind(&View::AddAuthor, this, ph::_1)
  );
  menu_.AddAction("AddBook"s, "<pub year> <title>"s, "Adds book"s,
                  std::bind(&View::AddBook, this, ph::_1));
  menu_.AddAction("ShowBook"s, "<title>"s, "Show book"s,
                  std::bind(&View::ShowBook, this, ph::_1));
  menu_.AddAction("DeleteBook"s, "<title>"s, "Delete book"s,
                  std::bind(&View::DeleteBook, this, ph::_1));
  menu_.AddAction("EditBook"s, "<title>"s, "Edit book"s,
                  std::bind(&View::EditBook, this, ph::_1));
  menu_.AddAction("ShowAuthors"s, {}, "Show authors"s,
                  std::bind(&View::ShowAuthors, this));
  menu_.AddAction("ShowBooks"s, {}, "Show books"s,
                  std::bind(&View::ShowBooks, this));
  menu_.AddAction("ShowAuthorBooks"s, {}, "Show author books"s,
                  std::bind(&View::ShowAuthorBooks, this));
  menu_.AddAction("DeleteAuthor"s, "<author>"s, "Delete author"s,
                  std::bind(&View::DeleteAuthor, this, ph::_1));
  menu_.AddAction("EditAuthor"s, "<author>"s, "Edit author"s,
                  std::bind(&View::EditAuthor, this, ph::_1));
}

bool View::AddAuthor(std::istream &cmd_input) const {
  try {
    std::string name;
    std::getline(cmd_input, name);
    boost::algorithm::trim(name);
    if (name.empty()) {
      throw std::exception{};
    }
    use_cases_.AddAuthor(std::move(name));
  } catch (const std::exception &) {
    output_ << "Failed to add author"sv << std::endl;
  }
  return true;
}

bool View::AddBook(std::istream &cmd_input) const {
  try {
    if (auto params = GetBookParams(cmd_input)) {
      use_cases_.AddBook(std::move(*params));
    }
  } catch (const std::exception &) {
    output_ << "Failed to add book"sv << std::endl;
  }
  return true;
}

bool View::DeleteAuthor(std::istream &cmd_input) const {
  try {
    std::string author_name;
    if (std::getline(cmd_input, author_name) && !author_name.empty()) {
      boost::algorithm::trim(author_name);
      use_cases_.DeleteAuthorByName(author_name);
      return true;
    }
    if (auto author_id = SelectAuthor()) {
      use_cases_.DeleteAuthorById(*author_id);
      return true;
    }
  } catch (const std::exception &) {
    output_ << "Failed to delete author"sv << std::endl;
  }
  return true;
}

bool View::EditAuthor(std::istream &cmd_input) const {
  try {
    std::string author_name;
    if (std::getline(cmd_input, author_name) && !author_name.empty()) {
      boost::algorithm::trim(author_name);
      const auto author = FindAuthorByName(author_name);
      if (!author) {
        throw std::exception{};
      }
      const auto new_name = GetNewAuthorName();
      use_cases_.EditAuthorByName(author_name, new_name);
      return true;
    }
    if (auto author_id = SelectAuthor()) {
      const auto new_name = GetNewAuthorName();
      use_cases_.EditAuthorById(*author_id, new_name);
      return true;
    }
  } catch (const std::exception &) {
    output_ << "Failed to edit author"sv << std::endl;
  }
  return true;
}

std::string View::GetNewAuthorName() const {
  output_ << "Enter new name:" << std::endl;
  std::string new_name;
  std::getline(input_, new_name);
  return new_name;
}

detail::EditBookParams
View::GetNewBookData(const detail::BookInfoEx &book_info) const {
  output_ << "Enter new title or empty line to use the current one ("
          << book_info.title << "):" << std::endl;
  std::string new_title;
  std::getline(input_, new_title);
  boost::algorithm::trim(new_title);

  if (new_title.empty()) {
    new_title = book_info.title;
  }

  const int publication_year = EnterPublicationYear(book_info.publication_year);
  const auto tags = EnterTags(book_info.tags);

  detail::EditBookParams edit_book_params{book_info.id, std::move(new_title),
                                          publication_year, std::move(tags)};
  return edit_book_params;
}

int View::EnterPublicationYear(int publication_year) const {
  output_ << "Enter publication year or empty line to use the current one ("
          << publication_year << "):" << std::endl;
  std::string str_year;
  std::getline(input_, str_year);
  boost::algorithm::trim(str_year);

  if (str_year.empty()) {
    return publication_year;
  }

  int new_year;
  try {
    new_year = std::stoi(str_year);
  } catch (std::exception const &) {
    return publication_year;
  }
  return new_year;
}

bool View::ShowAuthors() const {
  PrintVector(output_, GetAuthors());
  return true;
}

bool View::ShowBooks() const {
  PrintVector(output_, GetBooks());
  return true;
}

bool View::ShowBook(std::istream &cmd_input) const {
  try {
    std::string book_title;
    if (std::getline(cmd_input, book_title) && !book_title.empty()) {
      boost::algorithm::trim(book_title);
      const auto books_info_ex = GetBookByTitle(book_title);
      if (books_info_ex.empty()) {
        return true;
      }
      if (books_info_ex.size() > 1) {
        const auto book_info = SelectBook(books_info_ex);
        if (book_info) {
          output_ << *book_info;
        }
        return true;
      }

      output_ << books_info_ex[0];
      return true;
    }

    const auto books = GetBooksEx();
    if (books.empty()) {
      return true;
    }

    const auto book_info = SelectBook(books);
    if (book_info) {
      output_ << *book_info;
    }

  } catch (const std::exception &) {
    // Do nothing
  }
  return true;
}

bool View::DeleteBook(std::istream &cmd_input) const {
  try {
    std::string book_title;
    if (std::getline(cmd_input, book_title) && !book_title.empty()) {
      boost::algorithm::trim(book_title);
      const auto books_info = GetBookByTitle(book_title);
      if (books_info.empty()) {
        return true;
      }
      if (books_info.size() > 1) {
        const auto book_info = SelectBook(books_info);
        if (book_info) {
          use_cases_.DeleteBookById(book_info->id);
        }
        return true;
      }

      use_cases_.DeleteBookById(books_info[0].id);
      return true;
    }

    const auto books = GetBooksEx();
    if (books.empty()) {
      return true;
    }

    const auto book_info = SelectBook(books);
    if (book_info) {
      use_cases_.DeleteBookById(book_info->id);
    }

  } catch (const std::exception &) {
    output_ << "Failed to delete book"sv << std::endl;
  }
  return true;
}

bool View::EditBook(std::istream &cmd_input) const {
  try {
    std::string book_title;
    if (std::getline(cmd_input, book_title) && !book_title.empty()) {
      boost::algorithm::trim(book_title);
      const auto books_info = GetBookByTitle(book_title);
      if (books_info.empty()) {
        output_ << "Book not found" << std::endl;
        return true;
      }
      if (books_info.size() > 1) {
        const auto book_info = SelectBook(books_info);
        if (!book_info) {
          output_ << "Book not found" << std::endl;
          return true;
        }
        const auto new_book_data = GetNewBookData(*book_info);
        use_cases_.EditBookById(new_book_data);
        return true;
      }

      const auto new_book_data = GetNewBookData(books_info[0]);
      use_cases_.EditBookById(new_book_data);
      return true;
    }

    const auto books = GetBooksEx();
    if (books.empty()) {
      output_ << "Book not found" << std::endl;
      return true;
    }

    const auto book_info = SelectBook(books);
    if (!book_info) {
      output_ << "Book not found" << std::endl;
      return true;
    }

    const auto new_book_data = GetNewBookData(*book_info);
    use_cases_.EditBookById(new_book_data);

  } catch (const std::exception &) {
    output_ << "Book not found" << std::endl;
  }
  return true;
}

std::optional<detail::BookInfoEx>
View::SelectBook(const std::vector<detail::BookInfoEx> &books_info) const {
  PrintVector(output_, std::vector<detail::BookInfo>(books_info.begin(),
                                                     books_info.end()));
  output_ << "Enter the book # or empty line to cancel: " << std::endl;

  std::string str;
  std::getline(input_, str);

  if (str.empty()) {
    return std::nullopt;
  }

  int book_idx;
  try {
    book_idx = std::stoi(str) - 1;
  } catch (std::exception const &) {
    throw std::runtime_error("Invalid book num");
  }

  if (book_idx < 0 or book_idx >= static_cast<int>(books_info.size())) {
    throw std::runtime_error("Invalid book num");
  }

  return books_info[book_idx];
}

bool View::ShowAuthorBooks() const {
  try {
    if (auto author_id = SelectAuthor()) {
      PrintVector(output_, GetAuthorBooks(*author_id));
    }
  } catch (const std::exception &) {
    // Do nothing
  }
  return true;
}

std::optional<detail::AddBookParams>
View::GetBookParams(std::istream &cmd_input) const {
  detail::AddBookParams params;

  cmd_input >> params.publication_year;
  std::getline(cmd_input, params.title);
  boost::algorithm::trim(params.title);

  auto author_id = EnterAuthor();
  if (!author_id.has_value()) {
    return std::nullopt;
  } else {
    params.tags = EnterTags(std::nullopt);
    params.author_id = author_id.value();
    return params;
  }
}

std::set<std::string>
View::EnterTags(std::optional<std::string> current_tags) const {
  if (current_tags) {
    output_ << "Enter tags (" << *current_tags << "):" << std::endl;
  } else {
    output_ << "Enter tags (comma separated):" << std::endl;
  }
  std::string tags;
  std::getline(input_, tags);

  std::set<std::string> result;
  std::stringstream ss(tags);
  std::string token;

  while (std::getline(ss, token, ',')) {
    size_t start = token.find_first_not_of(" \t");
    size_t end = token.find_last_not_of(" \t");

    if (start != std::string::npos) {
      std::string cleaned = token.substr(start, end - start + 1);

      std::string normalized;
      bool in_space = false;
      for (char ch : cleaned) {
        if (ch == ' ' || ch == '\t') {
          if (!in_space) {
            normalized += ' ';
            in_space = true;
          }
        } else {
          normalized += ch;
          in_space = false;
        }
      }

      result.insert(normalized);
    }
  }

  return result;
}

std::optional<std::string> View::EnterAuthor() const {
  output_ << "Enter author name or empty line to select from list:"
          << std::endl;
  std::string author_name;
  if (!std::getline(input_, author_name) || author_name.empty()) {
    return SelectAuthor();
  }

  const auto &author = FindAuthorByName(author_name);
  if (author) {
    return author->id;
  }

  output_ << "No author found. Do you want to add " << author_name << " (y/n)?"
          << std::endl;

  std::string accept;

  if (std::getline(input_, accept) && (accept == "y" || accept == "Y")) {
    return use_cases_.AddAuthor(std::move(author_name));
  }
  throw std::exception{};
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
  } catch (std::exception const &) {
    throw std::runtime_error("Invalid author num");
  }

  --author_idx;
  if (author_idx < 0 or author_idx >= static_cast<int>(authors.size())) {
    throw std::runtime_error("Invalid author num");
  }

  return authors[author_idx].id;
}

std::optional<detail::AuthorInfo>
View::FindAuthorByName(const std::string &author_name) const {
  return use_cases_.FindAuthorByName(author_name);
}

std::vector<detail::AuthorInfo> View::GetAuthors() const {
  return use_cases_.ShowAuthors();
}

std::vector<detail::BookInfo> View::GetBooks() const {
  return use_cases_.ShowBooks();
}

std::vector<detail::BookInfoEx> View::GetBooksEx() const {
  return use_cases_.ShowBooksEx();
}

std::vector<detail::BookInfo>
View::GetAuthorBooks(const std::string &author_id) const {
  return use_cases_.ShowAuthorBooks(author_id);
}

std::vector<detail::BookInfoEx>
View::GetBookByTitle(const std::string &book_title) const {
  return use_cases_.GetBookByTitle(book_title);
}

} // namespace ui