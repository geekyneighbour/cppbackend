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
        if (game.GetSessions().empty() && tokens.empty()) {
            if (fs::exists(filepath)) {
                fs::remove(filepath);
            }
            return true;
        }
        
        fs::path temp_path = filepath;
        temp_path += ".tmp";
        
        serialization::GameStateRepr state(game, tokens);
        std::ofstream ofs(temp_path);
        if (!ofs.is_open()) {
            throw std::runtime_error("Cannot open file for writing");
        }
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
        
        if (fs::file_size(filepath) == 0) {
            fs::remove(filepath);
            return true;
        }
        
        std::ifstream ifs(filepath);
        if (!ifs.is_open()) {
            return false;
        }
        
        boost::archive::text_iarchive ia(ifs);
        serialization::GameStateRepr state;
        ia >> state;
        

        std::unordered_map<uint64_t, model::Dog*> dog_id_map;
        for (const auto& session_repr : state.GetSessions()) {
            session_repr.Restore(game, dog_id_map);
        }
        

        for (const auto& token_repr : state.GetTokens()) {
            const auto* map = game.FindMap(model::Map::Id{token_repr.map_id});
            if (!map) {
                throw std::runtime_error("Map not found for token: " + token_repr.map_id);
            }
            
            auto& session = game.FindOrCreateSession(map);
            
            model::Player* found_player = nullptr;
            for (auto* player : session.GetPlayers()) {
                if (*player->GetDog()->GetId() == token_repr.dog_id) {
                    found_player = player;
                    break;
                }
            }
            
            if (!found_player) {
                const auto& dogs = session.GetDogs();
                for (const auto& dog_ptr : dogs) {
                    if (*dog_ptr->GetId() == token_repr.dog_id) {
                        auto& player = session.AddPlayer(*dog_ptr);
                        found_player = &player;
                        break;
                    }
                }
            }
            
            if (found_player) {
                tokens[token_repr.token] = found_player;
            } else {
                throw std::runtime_error("Failed to restore player for token: " + token_repr.token);
            }
        }
        
        uint64_t next_player_id = state.GetNextPlayerId();
        for (auto& [map, session] : game.GetSessions()) {
            session->SetNextPlayerId(next_player_id);
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load state: " << e.what() << std::endl;
        if (fs::exists(filepath)) {
            fs::remove(filepath);
        }
        return false;
    }
}

} // namespace state_saver