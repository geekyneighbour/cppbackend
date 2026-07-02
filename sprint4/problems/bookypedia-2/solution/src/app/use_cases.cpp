#include "use_cases.h"

namespace ui {
namespace detail {

std::ostream &operator<<(std::ostream &out, const AuthorInfo &author) {
  out << author.name;
  return out;
}

std::ostream &operator<<(std::ostream &out, const BookInfo &book) {
  out << book.title << " by " << book.author_name << ", "
      << book.publication_year;
  return out;
}

std::ostream &operator<<(std::ostream &out, const BookInfoEx &book) {
  out << "Title: " << book.title << std::endl;
  out << "Author: " << book.author_name << std::endl;
  out << "Publication year: " << book.publication_year << std::endl;
  if (!book.tags.empty()) {
    out << "Tags: " << book.tags << std::endl;
  }
  return out;
}

} // namespace detail
} // namespace ui