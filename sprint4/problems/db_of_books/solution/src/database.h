#pragma once

#include <string>
#include <vector>
#include <memory>
#include <pqxx/pqxx>

struct Book {
    int id;
    std::string title;
    std::string author;
    int year;
    std::string isbn;  // empty string represents NULL
};

class Database {
public:
    explicit Database(const std::string& connection_string);
    ~Database() = default;
    
    void create_table_if_not_exists();
    bool add_book(const std::string& title, const std::string& author, 
                  int year, const std::string& isbn);
    std::vector<Book> get_all_books();
    
private:
    std::unique_ptr<pqxx::connection> conn_;
    
    void bind_parameters(pqxx::work& txn, pqxx::prepare::invocation& inv,
                         const std::string& title, const std::string& author,
                         int year, const std::string& isbn);
};