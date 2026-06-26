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
        fs::path temp_path = filepath;
        temp_path += ".tmp";
        
        serialization::GameStateRepr state(game, tokens);
        std::ofstream ofs(temp_path);
        boost::archive::text_oarchive oa(ofs);
        oa << state;
        ofs.close();
        
        fs::rename(temp_path, filepath);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save state: " << e.what() << std::endl;
        return false;
    }
}

template <typename Game, typename Tokens>
bool LoadState(Game& game, Tokens& tokens, const fs::path& filepath) {
    try {
        if (!fs::exists(filepath)) {
            return true;
        }
        
        std::ifstream ifs(filepath);
        boost::archive::text_iarchive ia(ifs);
        serialization::GameStateRepr state;
        ia >> state;
        
        state.Restore(game, tokens);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load state: " << e.what() << std::endl;
        return false;
    }
}

} // namespace state_saver