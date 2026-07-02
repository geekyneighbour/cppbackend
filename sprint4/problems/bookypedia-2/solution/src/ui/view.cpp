bool View::ShowBook(std::istream& cmd_input) const {
    try {
        std::string title;
        std::getline(cmd_input, title);
        boost::algorithm::trim(title);
        
        std::vector<ui::detail::BookInfo> books;
        
        if (title.empty()) {
            // Показываем все книги
            books = GetBooks();
            if (books.empty()) {
                return true;
            }
            PrintVector(output_, books);
            output_ << "Enter the book # or empty line to cancel:" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int book_idx;
            try {
                book_idx = std::stoi(str);
            } catch (const std::exception&) {
                return true;
            }
            
            --book_idx;
            if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                return true;
            }
            
            auto detail = use_cases_.GetBookDetail(books[book_idx].id);
            output_ << "Title: " << detail.title << std::endl;
            output_ << "Author: " << detail.author_name << std::endl;
            output_ << "Publication year: " << detail.publication_year << std::endl;
            if (!detail.tags.empty()) {
                output_ << "Tags: ";
                for (size_t i = 0; i < detail.tags.size(); ++i) {
                    if (i > 0) output_ << ", ";
                    output_ << detail.tags[i];
                }
                output_ << std::endl;
            }
        } else {
            books = use_cases_.GetBooksByTitle(title);
            if (books.empty()) {
                return true;
            }
            
            if (books.size() == 1) {
                auto detail = use_cases_.GetBookDetail(books[0].id);
                output_ << "Title: " << detail.title << std::endl;
                output_ << "Author: " << detail.author_name << std::endl;
                output_ << "Publication year: " << detail.publication_year << std::endl;
                if (!detail.tags.empty()) {
                    output_ << "Tags: ";
                    for (size_t i = 0; i < detail.tags.size(); ++i) {
                        if (i > 0) output_ << ", ";
                        output_ << detail.tags[i];
                    }
                    output_ << std::endl;
                }
            } else {
                PrintVector(output_, books);
                output_ << "Enter the book # or empty line to cancel:" << std::endl;
                
                std::string str;
                if (!std::getline(input_, str) || str.empty()) {
                    return true;
                }
                
                int book_idx;
                try {
                    book_idx = std::stoi(str);
                } catch (const std::exception&) {
                    return true;
                }
                
                --book_idx;
                if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                    return true;
                }
                
                auto detail = use_cases_.GetBookDetail(books[book_idx].id);
                output_ << "Title: " << detail.title << std::endl;
                output_ << "Author: " << detail.author_name << std::endl;
                output_ << "Publication year: " << detail.publication_year << std::endl;
                if (!detail.tags.empty()) {
                    output_ << "Tags: ";
                    for (size_t i = 0; i < detail.tags.size(); ++i) {
                        if (i > 0) output_ << ", ";
                        output_ << detail.tags[i];
                    }
                    output_ << std::endl;
                }
            }
        }
    } catch (const std::exception&) {
        // Ничего не выводим при ошибке
    }
    return true;
}

bool View::DeleteBook(std::istream& cmd_input) const {
    try {
        std::string title;
        std::getline(cmd_input, title);
        boost::algorithm::trim(title);
        
        std::vector<ui::detail::BookInfo> books;
        
        if (title.empty()) {
            // Показываем все книги
            books = GetBooks();
            if (books.empty()) {
                return true;
            }
            PrintVector(output_, books);
            output_ << "Enter the book # or empty line to cancel:" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int book_idx;
            try {
                book_idx = std::stoi(str);
            } catch (const std::exception&) {
                output_ << "Failed to delete book"sv << std::endl;
                return true;
            }
            
            --book_idx;
            if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                output_ << "Failed to delete book"sv << std::endl;
                return true;
            }
            
            if (!use_cases_.DeleteBook(books[book_idx].title)) {
                output_ << "Failed to delete book"sv << std::endl;
            }
        } else {
            books = use_cases_.GetBooksByTitle(title);
            if (books.empty()) {
                output_ << "Failed to delete book"sv << std::endl;
                return true;
            }
            
            if (books.size() == 1) {
                if (!use_cases_.DeleteBook(title)) {
                    output_ << "Failed to delete book"sv << std::endl;
                }
            } else {
                PrintVector(output_, books);
                output_ << "Enter the book # or empty line to cancel:" << std::endl;
                
                std::string str;
                if (!std::getline(input_, str) || str.empty()) {
                    return true;
                }
                
                int book_idx;
                try {
                    book_idx = std::stoi(str);
                } catch (const std::exception&) {
                    output_ << "Failed to delete book"sv << std::endl;
                    return true;
                }
                
                --book_idx;
                if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                    output_ << "Failed to delete book"sv << std::endl;
                    return true;
                }
                
                if (!use_cases_.DeleteBook(books[book_idx].title)) {
                    output_ << "Failed to delete book"sv << std::endl;
                }
            }
        }
    } catch (const std::exception&) {
        output_ << "Failed to delete book"sv << std::endl;
    }
    return true;
}

bool View::EditBook(std::istream& cmd_input) const {
    try {
        std::string title;
        std::getline(cmd_input, title);
        boost::algorithm::trim(title);
        
        std::vector<ui::detail::BookInfo> books;
        std::string current_title;
        int current_year = 0;
        std::string current_book_id;
        
        if (title.empty()) {
            // Показываем все книги
            books = GetBooks();
            if (books.empty()) {
                return true;
            }
            PrintVector(output_, books);
            output_ << "Enter the book # or empty line to cancel:" << std::endl;
            
            std::string str;
            if (!std::getline(input_, str) || str.empty()) {
                return true;
            }
            
            int book_idx;
            try {
                book_idx = std::stoi(str);
            } catch (const std::exception&) {
                output_ << "Book not found"sv << std::endl;
                return true;
            }
            
            --book_idx;
            if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                output_ << "Book not found"sv << std::endl;
                return true;
            }
            
            current_title = books[book_idx].title;
            current_year = books[book_idx].publication_year;
            current_book_id = books[book_idx].id;
        } else {
            books = use_cases_.GetBooksByTitle(title);
            if (books.empty()) {
                output_ << "Book not found"sv << std::endl;
                return true;
            }
            
            if (books.size() == 1) {
                current_title = books[0].title;
                current_year = books[0].publication_year;
                current_book_id = books[0].id;
            } else {
                PrintVector(output_, books);
                output_ << "Enter the book # or empty line to cancel:" << std::endl;
                
                std::string str;
                if (!std::getline(input_, str) || str.empty()) {
                    return true;
                }
                
                int book_idx;
                try {
                    book_idx = std::stoi(str);
                } catch (const std::exception&) {
                    output_ << "Book not found"sv << std::endl;
                    return true;
                }
                
                --book_idx;
                if (book_idx < 0 || book_idx >= static_cast<int>(books.size())) {
                    output_ << "Book not found"sv << std::endl;
                    return true;
                }
                
                current_title = books[book_idx].title;
                current_year = books[book_idx].publication_year;
                current_book_id = books[book_idx].id;
            }
        }
        
        // Получаем детальную информацию о книге
        auto detail = use_cases_.GetBookDetail(current_book_id);
        
        // Новый заголовок
        output_ << "Enter new title or empty line to use the current one (" << current_title << "):" << std::endl;
        std::string new_title;
        std::getline(input_, new_title);
        boost::algorithm::trim(new_title);
        if (new_title.empty()) {
            new_title = current_title;
        }
        
        // Новый год
        output_ << "Enter publication year or empty line to use the current one (" << current_year << "):" << std::endl;
        std::string year_str;
        std::getline(input_, year_str);
        boost::algorithm::trim(year_str);
        int new_year = current_year;
        if (!year_str.empty()) {
            try {
                new_year = std::stoi(year_str);
            } catch (const std::exception&) {
                // Используем текущий год
            }
        }
        
        // Новые теги
        std::string tags_str;
        if (!detail.tags.empty()) {
            output_ << "Enter tags (current tags: ";
            for (size_t i = 0; i < detail.tags.size(); ++i) {
                if (i > 0) output_ << ", ";
                output_ << detail.tags[i];
            }
            output_ << "):" << std::endl;
        } else {
            output_ << "Enter tags (current tags: none):" << std::endl;
        }
        std::getline(input_, tags_str);
        
        std::vector<std::string> new_tags;
        if (!tags_str.empty()) {
            new_tags = ParseTags(tags_str);
        } else {
            new_tags = detail.tags;
        }
        
        if (!use_cases_.EditBook(current_title, new_title, new_year, new_tags)) {
            output_ << "Book not found"sv << std::endl;
        }
    } catch (const std::exception&) {
        output_ << "Book not found"sv << std::endl;
    }
    return true;
}