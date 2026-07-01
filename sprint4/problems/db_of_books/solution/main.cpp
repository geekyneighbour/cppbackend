#include <iostream>
#include <string>
#include "database.hpp"
#include "json_parser.hpp"

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <database_connection_string>" << std::endl;
        return 1;
    }
    
    std::string connection_string = argv[1];
    
    try {
        Database db(connection_string);
        db.create_table_if_not_exists();
        
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) {
                continue;
            }
            
            try {
                json command = JsonParser::parse_action(line);
                JsonParser::execute_action(command, db, std::cout);
            } catch (const std::exception& e) {
                std::cerr << "Error processing command: " << e.what() << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Database error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}