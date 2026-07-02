#pragma once
#include <iosfwd>
#include <optional>
#include <set>
#include <string>
#include <vector>

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
  std::set<std::string> tags;
};

struct EditBookParams {
  std::string id;
  std::string title;
  int publication_year = 0;
  std::set<std::string> tags;
};

struct AuthorInfo {
  std::string id;
  std::string name;
};

struct BookInfo {
  std::string title;
  int publication_year;
  std::string author_name;
};

struct BookInfoEx : public BookInfo {
  std::string tags;
  std::string id;
};

} // namespace detail

class View {
public:
  View(menu::Menu &menu, app::UseCases &use_cases, std::istream &input,
       std::ostream &output);

private:
  bool AddAuthor(std::istream &cmd_input) const;
  bool AddBook(std::istream &cmd_input) const;
  bool ShowAuthors() const;
  bool ShowBooks() const;
  bool ShowBook(std::istream &cmd_input) const;
  bool DeleteBook(std::istream &cmd_input) const;
  bool EditBook(std::istream &cmd_input) const;
  bool ShowAuthorBooks() const;
  bool DeleteAuthor(std::istream &cmd_input) const;
  bool EditAuthor(std::istream &cmd_input) const;

  std::optional<detail::AddBookParams>
  GetBookParams(std::istream &cmd_input) const;
  std::optional<std::string> SelectAuthor() const;
  std::optional<std::string> EnterAuthor() const;
  std::vector<detail::AuthorInfo> GetAuthors() const;
  std::vector<detail::BookInfo> GetBooks() const;
  std::vector<detail::BookInfoEx> GetBooksEx() const;
  std::vector<detail::BookInfoEx>
  GetBookByTitle(const std::string &book_title) const;
  std::vector<detail::BookInfo>
  GetAuthorBooks(const std::string &author_id) const;

  std::optional<detail::AuthorInfo>
  FindAuthorByName(const std::string &author_name) const;

  std::set<std::string>
  EnterTags(std::optional<std::string> current_tags) const;

  std::string GetNewAuthorName() const;

  detail::EditBookParams
  GetNewBookData(const detail::BookInfoEx &book_info) const;
  int EnterPublicationYear(int publication_year) const;

  std::optional<detail::BookInfoEx>
  SelectBook(const std::vector<detail::BookInfoEx> &books_info) const;

  menu::Menu &menu_;
  app::UseCases &use_cases_;
  std::istream &input_;
  std::ostream &output_;
};

} // namespace ui