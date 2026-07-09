#pragma once

#include "../ui/view.h"
#include <string>

namespace app {

class UseCases {
public:
  virtual void AddAuthor(const std::string &name) = 0;
  virtual std::vector<ui::detail::AuthorInfo> ShowAuthors() = 0;

  virtual void AddBook(const std::string &title, int16_t publication_year,
                       const std::string &author_id) = 0;
  virtual std::vector<ui::detail::BookInfo>
  ShowAuthorBooks(const std::string &author_id) = 0;
  virtual std::vector<ui::detail::BookInfo> ShowBooks() = 0;

protected:
  ~UseCases() = default;
};

} // namespace app