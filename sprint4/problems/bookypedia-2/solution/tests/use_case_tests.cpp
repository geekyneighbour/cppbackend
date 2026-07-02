#include <catch2/catch_test_macros.hpp>

#include "../src/app/use_cases_impl.h"
#include "../src/domain/author.h"
#include "../src/domain/book.h"

namespace {

struct MockAuthorRepository : domain::AuthorRepository {
    std::vector<domain::Author> saved_authors;

    void Save(const domain::Author& author) override {
        saved_authors.emplace_back(author);
    }
    
    std::vector<ui::detail::AuthorInfo> GetAllAuthors() override {
        return {};
    }
    
    void Delete(const std::string& author_id) override {
        // Mock implementation
    }
    
    void Edit(const std::string& author_id, const std::string& new_name) override {
        // Mock implementation
    }
};

struct MockBookRepository : domain::BookRepository {
    std::vector<domain::Book> saved_books;
    std::vector<std::string> saved_author_ids;

    void Save(const domain::Book& book, const std::string& author_id) override {
        saved_books.emplace_back(book);
        saved_author_ids.push_back(author_id);
    }
    
    void SaveTags(const std::string& book_id, const std::vector<std::string>& tags) override {
        // Mock implementation
    }
    
    std::vector<ui::detail::BookInfo> GetAllBooks() override {
        return {};
    }
    
    std::vector<ui::detail::BookInfo> GetBooksByAuthor(const std::string& author_id) override {
        return {};
    }
    
    std::vector<ui::detail::BookInfo> GetBooksByTitle(const std::string& title) override {
        return {};
    }
    
    ui::detail::BookDetailInfo GetBookDetail(const std::string& book_id) override {
        return {};
    }
    
    void DeleteBook(const std::string& book_id) override {
        // Mock implementation
    }
    
    void EditBook(const std::string& book_id, const std::string& title, 
                  int publication_year, const std::vector<std::string>& tags) override {
        // Mock implementation
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