#pragma once
#include <optional>
#include <string>
#include <vector>

#include "../ui/view.h"
#include "../util/tagged_uuid.h"

namespace domain {

namespace detail {
struct AuthorTag {};
} // namespace detail

using AuthorId = util::TaggedUUID<detail::AuthorTag>;

class Author {
public:
  Author(AuthorId id, std::string name)
      : id_(std::move(id)), name_(std::move(name)) {}

  const AuthorId &GetId() const noexcept { return id_; }

  const std::string &GetName() const noexcept { return name_; }

private:
  AuthorId id_;
  std::string name_;
};

class AuthorRepository {
public:
  virtual std::string Save(const Author &author) = 0;

  virtual std::vector<ui::detail::AuthorInfo> ShowAuthors() const = 0;

  virtual std::optional<ui::detail::AuthorInfo>
  ShowAuthorByName(const std::string &author_name) const = 0;

  virtual std::string
  DeleteAuthorByName(const std::string &author_name) const = 0;
  virtual std::string DeleteAuthorById(const std::string &author_id) const = 0;

  virtual std::string
  EditAuthorByName(const std::string &author_name,
                   const std::string &new_author_name) const = 0;
  virtual std::string
  EditAuthorById(const std::string &author_id,
                 const std::string &new_author_name) const = 0;

protected:
  ~AuthorRepository() = default;
};

} // namespace domain