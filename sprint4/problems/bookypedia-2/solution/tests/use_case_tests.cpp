#include <catch2/catch_test_macros.hpp>

#include "../src/app/use_cases_impl.h"
#include "../src/domain/author.h"
#include "../src/domain/book.h"

namespace {

struct MockAuthorRepository : domain::AuthorRepository {
    std::vector<domain::Author> saved_authors;

    std::string Save(const domain::Author& author) override {
        saved_authors.emplace_back(author);
        return "";
    }
    
    std::vector<ui::detail::AuthorInfo> ShowAuthors() const override {
        return {};
    }
    
    std::optional<ui::detail::AuthorInfo> ShowAuthorByName(const std::string&) const override {
        return std::nullopt;
    }
    
    std::string DeleteAuthorByName(const std::string&) const override {
        return "";
    }
    
    std::string DeleteAuthorById(const std::string&) const override {
        return "";
    }
    
    std::string EditAuthorByName(const std::string&, const std::string&) const override {
        return "";
    }
    
    std::string EditAuthorById(const std::string&, const std::string&) const override {
        return "";
    }
};

struct MockBookRepository : domain::BookRepository {
    std::vector<domain::Book> saved_books;
    std::vector<std::string> saved_author_ids;

    std::string Save(domain::Book book, std::string author_id, std::set<std::string> tags) override {
        saved_books.emplace_back(book);
        saved_author_ids.push_back(author_id);
        return "";
    }
    
    std::vector<ui::detail::BookInfo> ShowBooks() const override {
        return {};
    }
    
    std::vector<ui::detail::BookInfoEx> ShowBooksEx() const override {
        return {};
    }
    
    std::vector<ui::detail::BookInfo> ShowAuthorBooks(const std::string&) const override {
        return {};
    }
    
    std::vector<ui::detail::BookInfoEx> GetBookByTitle(const std::string&) const override {
        return {};
    }
    
    std::string DeleteBookById(const std::string&) const override {
        return "";
    }
    
    std::string EditBookById(const ui::detail::EditBookParams&) const override {
        return "";
    }
};

struct Fixture {
    MockAuthorRepository authors;
    MockBookRepository books;
};

}  // namespace

SCENARIO_METHOD(Fixture, "Book Adding") {
    GIVEN("Use cases") {
        app::UseCasesImpl use_cases{authors, books};

        WHEN("Adding an author") {
            const auto author_name = "Joanne Rowling";
            use_cases.AddAuthor(author_name);

            THEN("author with the specified name is saved to repository") {
                REQUIRE(authors.saved_authors.size() == 1);
                CHECK(authors.saved_authors.at(0).GetName() == author_name);
                CHECK(authors.saved_authors.at(0).GetId() != domain::AuthorId{});
            }
        }
    }
}