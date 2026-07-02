#include <boost/json.hpp>
#include <iostream>
#include <pqxx/pqxx>
#include <string>
#include <unordered_map>
#include <functional>
#include <optional>

using namespace std::literals;
using pqxx::operator"" _zv;

// Структура для хранения данных книги
struct BookData {
    std::string title;
    std::string author;
    int year;
    std::optional<std::string> isbn;
};

// Класс для управления базой данных книг
class BookManager {
public:
    explicit BookManager(const std::string& conn_string) 
        : conn_(conn_string) {
        initialize_database();
        prepare_statements();
    }

    // Добавление книги
    bool add_book(const BookData& book) {
        try {
            pqxx::work txn(conn_);
            
            if (book.isbn.has_value()) {
                // ISBN указан
                txn.exec_prepared(
                    "add_book_with_isbn"_zv,
                    book.title,
                    book.author,
                    book.year,
                    book.isbn.value()
                );
            } else {
                // ISBN NULL
                txn.exec_prepared(
                    "add_book_without_isbn"_zv,
                    book.title,
                    book.author,
                    book.year
                );
            }
            
            txn.commit();
            return true;
            
        } catch (const pqxx::sql_error& e) {
            // Дублирующийся ISBN или другая ошибка
            return false;
        } catch (const std::exception& e) {
            std::cerr << "Error adding book: " << e.what() << std::endl;
            return false;
        }
    }

    // Получение всех книг с сортировкой
    boost::json::array get_all_books() {
        pqxx::work txn(conn_);
        auto result = txn.exec_prepared("get_all_books"_zv);
        txn.commit();
        
        boost::json::array books;
        
        for (const auto& row : result) {
            boost::json::object book;
            book["id"] = row[0].as<int>();
            book["title"] = row[1].as<std::string>();
            book["author"] = row[2].as<std::string>();
            book["year"] = row[3].as<int>();
            
            // Обработка NULL для ISBN
            if (row[4].is_null()) {
                book["ISBN"] = nullptr;
            } else {
                book["ISBN"] = row[4].as<std::string>();
            }
            
            books.push_back(book);
        }
        
        return books;
    }

private:
    pqxx::connection conn_;

    // Инициализация таблицы
    void initialize_database() {
        pqxx::work txn(conn_);
        
        // Создаем таблицу
        txn.exec(
            "CREATE TABLE IF NOT EXISTS books ("
            "id SERIAL PRIMARY KEY, "
            "title VARCHAR(100) NOT NULL, "
            "author VARCHAR(100) NOT NULL, "
            "year INTEGER NOT NULL, "
            "ISBN CHAR(13) UNIQUE"
            ")"_zv
        );
        
        txn.commit();
    }

    // Подготовка параметризованных запросов
    void prepare_statements() {
        // Запрос для добавления книги с ISBN
        conn_.prepare(
            "add_book_with_isbn"_zv,
            "INSERT INTO books (title, author, year, ISBN) "
            "VALUES ($1, $2, $3, $4)"_zv
        );
        
        // Запрос для добавления книги без ISBN (NULL)
        conn_.prepare(
            "add_book_without_isbn"_zv,
            "INSERT INTO books (title, author, year) "
            "VALUES ($1, $2, $3)"_zv
        );
        
        // Запрос для получения всех книг с сортировкой
        conn_.prepare(
            "get_all_books"_zv,
            "SELECT id, title, author, year, ISBN FROM books "
            "ORDER BY year DESC, title ASC, author ASC, ISBN ASC"_zv
        );
    }
};

// Парсер JSON команд
class CommandParser {
public:
    using CommandHandler = std::function<void(const boost::json::object&)>;
    
    CommandParser(BookManager& manager) : book_manager_(manager) {
        register_commands();
    }
    
    void execute(const std::string& line) {
        try {
            auto value = boost::json::parse(line);
            const auto& obj = value.as_object();
            
            std::string action = std::string(obj.at("action").as_string());
            
            auto it = handlers_.find(action);
            if (it != handlers_.end()) {
                it->second(obj.at("payload").as_object());
            } else {
                std::cerr << "Unknown action: " << action << std::endl;
            }
            
        } catch (const boost::json::system_error& e) {
            std::cerr << "JSON parse error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    }

private:
    BookManager& book_manager_;
    std::unordered_map<std::string, CommandHandler> handlers_;
    
    void register_commands() {
        // Команда добавления книги
        handlers_["add_book"] = [this](const boost::json::object& payload) {
            BookData book;
            book.title = std::string(payload.at("title").as_string());
            book.author = std::string(payload.at("author").as_string());
            book.year = static_cast<int>(payload.at("year").as_int64());
            
            // Обработка ISBN (может быть null)
            const auto* isbn_ptr = payload.at("ISBN").if_string();
            if (isbn_ptr) {
                book.isbn = std::string(*isbn_ptr);
            } else {
                book.isbn = std::nullopt;
            }
            
            bool result = book_manager_.add_book(book);
            
            boost::json::object response;
            response["result"] = result;
            std::cout << boost::json::serialize(response) << std::endl;
        };
        
        // Команда получения всех книг
        handlers_["all_books"] = [this](const boost::json::object&) {
            auto books = book_manager_.get_all_books();
            std::cout << boost::json::serialize(books) << std::endl;
        };
        
        // Команда выхода
        handlers_["exit"] = [](const boost::json::object&) {
            exit(EXIT_SUCCESS);
        };
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <connection_string>" << std::endl;
        return EXIT_FAILURE;
    }
    
    try {
        // Создаем менеджер базы данных
        BookManager manager(argv[1]);
        
        // Создаем парсер команд
        CommandParser parser(manager);
        
        // Читаем команды построчно
        std::string line;
        while (std::getline(std::cin, line)) {
            if (!line.empty()) {
                parser.execute(line);
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    
    return EXIT_SUCCESS;
}