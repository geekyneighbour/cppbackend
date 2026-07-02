#pragma once

#include <string>
#include <vector>
#include "../util/tagged_uuid.h"
#include "../app/use_cases.h"  // Добавляем для доступа к определениям структур

namespace domain {

namespace detail {
struct AuthorTag {};
}  // namespace detail

using AuthorId = util::TaggedUUID<detail::AuthorTag>;

class Author {
public:
    Author(AuthorId id, std::string name)
        : id_(std::move(id))
        , name_(std::move(name)) {
    }

    const AuthorId& GetId() const noexcept {
        return id_;
    }

    const std::string& GetName() const noexcept {
        return name_;
    }

private:
    AuthorId id_;
    std::string name_;
};

class AuthorRepository {
public:
    virtual ~AuthorRepository() = default;
    
    virtual void Save(const Author& author) = 0;
    virtual std::vector<ui::detail::AuthorInfo> GetAllAuthors() = 0;
};

}  // namespace domain