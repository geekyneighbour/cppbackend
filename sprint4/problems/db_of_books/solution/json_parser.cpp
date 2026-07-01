#include "json_parser.hpp"
#include <iostream>

json JsonParser::parse_action(const std::string& line) {
    try {
        return json::parse(line);
    } catch (const json::parse_error& e) {
        throw std::runtime_error("Invalid JSON: " + std::string(e.what()));
    }
}

void JsonParser::execute_action(const json& command, Database& db, std::ostream& out) {
    std::string action = command["action"];
    
    if (action == "add_book") {
        const auto& payload = command["payload"];
        std::string title = payload["title"];
        std::string author = payload["author"];
        int year = payload["year"];
        std::string isbn;
        
        if (!payload["ISBN"].is_null()) {
            isbn = payload["ISBN"].get<std::string>();
        } else {
            isbn = "";
        }
        
        bool result = db.add_book(title, author, year, isbn);
        json response = {{"result", result}};
        out << response.dump() << std::endl;
        
    } else if (action == "all_books") {
        std::vector<Book> books = db.get_all_books();
        json response = json::array();
        
        for (const auto& book : books) {
            json book_json = {
                {"id", book.id},
                {"title", book.title},
                {"author", book.author},
                {"year", book.year}
            };
            
            if (book.isbn.empty()) {
                book_json["ISBN"] = nullptr;
            } else {
                book_json["ISBN"] = book.isbn;
            }
            
            response.push_back(book_json);
        }
        
        out << response.dump() << std::endl;
        
    } else if (action == "exit") {

        exit(0);
        
    } else {
        throw std::runtime_error("Unknown action: " + action);
    }
}