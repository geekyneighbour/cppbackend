#pragma once

#include "model.h"
#include "model_serialization.h"

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace state_saver {

namespace fs = std::filesystem;

template <typename Game, typename Tokens>
bool SaveState(const Game& game, const Tokens& tokens, const fs::path& filepath) {
    try {
        fs::create_directories(filepath.parent_path());

        fs::path temp_path = filepath;
        temp_path += ".tmp";

        serialization::GameStateRepr state(game, tokens);

        std::ofstream ofs(temp_path, std::ios::binary);
        if (!ofs.is_open()) {
            throw std::runtime_error("Cannot open file: " + temp_path.string());
        }

        boost::archive::text_oarchive oa(ofs);
        oa << state;

        ofs.close();

        fs::rename(temp_path, filepath);
        return true;

    } catch (const std::exception& e) {
        std::cerr << "SaveState failed: " << e.what() << std::endl;
        return false;
    }
}

template <typename Game, typename Tokens>
bool LoadState(Game& game, Tokens& tokens, const fs::path& filepath) {
    try {
        if (!fs::exists(filepath)) {
            return true;
        }

        if (fs::file_size(filepath) == 0) {
            return false; 
        }

        std::ifstream ifs(filepath, std::ios::binary);
        if (!ifs.is_open()) {
            return false;
        }

        boost::archive::text_iarchive ia(ifs);

        serialization::GameStateRepr state;
        ia >> state;

        std::unordered_map<std::string, model::Player*> tmp_tokens;
        
        for (const auto& [token, player] : tokens) {
            tmp_tokens[token] = player;
        }

        state.Restore(game, tmp_tokens);

        tokens.clear();
        for (const auto& [token, player] : tmp_tokens) {
            tokens[token] = player;
        }

        return true;

    } catch (const std::exception& e) {
        std::cerr << "LoadState failed: " << e.what() << std::endl;
        
        
        return false;
    }
}

} // namespace state_saver