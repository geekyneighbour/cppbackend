#pragma once

#include <string>
#include <nlohmann/json.hpp>
#include "database.h"

using json = nlohmann::json;

class JsonParser {
public:
    static json parse_action(const std::string& line);
    static void execute_action(const json& command, Database& db, std::ostream& out);
};