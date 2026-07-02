#include "bookypedia.h"

#include <iostream>

#include "menu/menu.h"
#include "postgres/postgres.h"
#include "ui/view.h"

namespace bookypedia {

using namespace std::literals;

Application::Application(const AppConfig& config)
    : db_{pqxx::connection{config.db_url}}
    , use_cases_{db_.GetAuthors(), db_.GetBooks()} {
}

void Application::Run() {
    menu::Menu menu{std::cin, std::cout};
    menu.AddAction("Help"s, {}, "Show instructions"s, [&menu](std::istream&) {
        menu.ShowInstructions();
        return true;
    });
    menu.AddAction("Exit"s, {}, "Exit program"s, [](std::istream&) {
        return false;
    });
    ui::View view{menu, use_cases_, std::cin, std::cout};
    menu.Run();
}

}  // namespace bookypedia