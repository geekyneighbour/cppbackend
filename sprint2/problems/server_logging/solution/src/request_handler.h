#pragma once
#include "http_server.h"
#include "model.h"
#include "logging.h"

#include <boost/json.hpp>
#include <boost/beast/http/file_body.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

#include <string_view>
#include <string>
#include <cassert>
#include <map>
#include <algorithm>
#include <filesystem>
#include <chrono>

namespace http_handler {

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
namespace fs = std::filesystem;

// ==================== LOGGING WRAPPER ====================

template <typename Handler>
class LoggingRequestHandler {
public:
    explicit LoggingRequestHandler(Handler& handler)
        : handler_(handler) {}

    template <typename Request, typename Send, typename Endpoint>
    void operator()(Request&& req, Send&& send, Endpoint&& endpoint) {
        LogRequest(req, endpoint);

        auto start = std::chrono::steady_clock::now();

        handler_(std::move(req),
            [this, &endpoint, start, send = std::forward<Send>(send)]
            (auto&& response) mutable {
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

                LogResponse(response, endpoint, duration);
                send(std::forward<decltype(response)>(response));
            });
    }

private:
    Handler& handler_;

    template <typename Request>
    static void LogRequest(const Request& req, const auto& endpoint) {
        json::value data{
            {"ip", endpoint.address().to_string()},
            {"URI", std::string(req.target())},
            {"method", std::string(req.method_string())}
        };

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data)
            << "request received";
    }

    template <typename Response>
    static void LogResponse(const Response& resp, const auto& endpoint, int time_ms) {
        json::value data{
            {"ip", endpoint.address().to_string()},
            {"response_time", time_ms},
            {"code", resp.result_int()}
        };

        auto it = resp.base().find("Content-Type");
        if (it != resp.base().end()) {
            std::string content_type(it->value().data(), it->value().size());
            data.as_object()["content_type"] = content_type;
        } else {
            data.as_object()["content_type"] = nullptr;
        }

        BOOST_LOG_TRIVIAL(info)
            << boost::log::add_value(additional_data, data)
            << "response sent";
    }
};

// ==================== MAIN HANDLER ====================

class RequestHandler {
public:
    explicit RequestHandler(model::Game& game, fs::path static_files)
        : game_{ game }
        , static_files_{ std::move(static_files) } {
    }

    RequestHandler(const RequestHandler&) = delete;
    RequestHandler& operator=(const RequestHandler&) = delete;

    template <typename Body, typename Allocator, typename Send>
    void operator()(http::request<Body, http::basic_fields<Allocator>>&& req, Send&& send) {
        using namespace std::literals;

        auto const bad_request = [&](std::string_view message) {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::content_type, "application/json");

            json::object error;
            error["code"] = "badRequest";
            error["message"] = std::string(message);

            res.body() = json::serialize(error);
            res.prepare_payload();
            send(std::move(res));
        };

        auto const not_found_api = [&]() {
            http::response<http::string_body> res{ http::status::not_found, req.version() };
            res.set(http::field::content_type, "application/json");

            json::object error;
            error["code"] = "mapNotFound";
            error["message"] = "Map not found";

            res.body() = json::serialize(error);
            res.prepare_payload();
            send(std::move(res));
        };

        auto const bad_req_static = [&](std::string_view message) {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::content_type, "text/plain");
            res.body() = std::string(message);
            res.prepare_payload();
            send(std::move(res));
        };

        auto const not_found_static = [&](std::string_view message) {
            http::response<http::string_body> res{ http::status::not_found, req.version() };
            res.set(http::field::content_type, "text/plain");
            res.body() = std::string(message);
            res.prepare_payload();
            send(std::move(res));
        };

        std::string path = std::string(req.target());
        bool is_api = path.starts_with("/api/");

        if (is_api) {
            if (req.method() != http::verb::get) {
                return bad_request("Invalid method");
            }
        } else {
            if (req.method() != http::verb::get && req.method() != http::verb::head) {
                return bad_request("Invalid method");
            }
        }

        // ================= API =================

        if (is_api &&
            (path == "/api/v1/maps" || path.starts_with("/api/v1/maps/"))) {

            if (path == "/api/v1/maps") {
                json::array result;

                for (const auto& map : game_.GetMaps()) {
                    json::object obj;
                    obj["id"] = std::string(*map.GetId());
                    obj["name"] = map.GetName();
                    result.push_back(obj);
                }

                http::response<http::string_body> res{ http::status::ok, req.version() };
                res.set(http::field::content_type, "application/json");
                res.body() = json::serialize(result);
                res.prepare_payload();

                return send(std::move(res));
            }

            std::string id = path.substr(std::string("/api/v1/maps/").size());
            const auto* map = game_.FindMap(model::Map::Id{ id });

            if (!map) {
                return not_found_api();
            }

            json::object obj;
            obj["id"] = std::string(*map->GetId());
            obj["name"] = map->GetName();
            obj["roads"] = SerializeRoads(map->GetRoads());
            obj["buildings"] = SerializeBuildings(map->GetBuildings());
            obj["offices"] = SerializeOffices(map->GetOffices());

            http::response<http::string_body> res{ http::status::ok, req.version() };
            res.set(http::field::content_type, "application/json");
            res.body() = json::serialize(obj);
            res.prepare_payload();

            return send(std::move(res));
        }

        if (is_api) {
            return bad_request("Unknown API endpoint");
        }

        // ================= STATIC =================

        std::string decoded_uri = DecodeURI(path);

        if (decoded_uri == "/") {
            decoded_uri = "/index.html";
        }

        fs::path full_path = static_files_ / decoded_uri.substr(1);

        if (fs::is_directory(full_path)) {
            full_path /= "index.html";
        }

        if (!IsSubPath(full_path, static_files_)) {
            return bad_req_static("Bad request");
        }

        if (!fs::exists(full_path)) {
            return not_found_static("File not found");
        }

        std::string ext = full_path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return std::tolower(c); });

        std::string content_type = GetContentType(ext);

        beast::error_code ec;
        http::file_body::value_type file;
        file.open(full_path.string().c_str(), beast::file_mode::read, ec);

        if (ec) {
            return bad_req_static("Cannot open file");
        }

        http::response<http::file_body> res{ http::status::ok, req.version() };
        res.set(http::field::content_type, content_type);
        res.body() = std::move(file);
        res.prepare_payload();

        if (req.method() == http::verb::head) {
            res.body() = {};
        }

        return send(std::move(res));
    }

private:
    model::Game& game_;
    fs::path static_files_;

    json::array SerializeRoads(const std::vector<model::Road>& map_roads) {
        json::array roads;
        for (const auto& r : map_roads) {
            json::object road;
            road["x0"] = r.GetStart().x;
            road["y0"] = r.GetStart().y;

            if (r.IsHorizontal()) {
                road["x1"] = r.GetEnd().x;
            } else {
                road["y1"] = r.GetEnd().y;
            }

            roads.push_back(road);
        }
        return roads;
    }

    json::array SerializeBuildings(const std::vector<model::Building>& map_buildings) {
        json::array buildings;
        for (const auto& b : map_buildings) {
            json::object building;
            auto bounds = b.GetBounds();
            building["x"] = bounds.position.x;
            building["y"] = bounds.position.y;
            building["w"] = bounds.size.width;
            building["h"] = bounds.size.height;
            buildings.push_back(building);
        }
        return buildings;
    }

    json::array SerializeOffices(const std::vector<model::Office>& map_offices) {
        json::array offices;
        for (const auto& o : map_offices) {
            json::object office;
            office["id"] = std::string(*o.GetId());
            office["x"] = o.GetPosition().x;
            office["y"] = o.GetPosition().y;
            office["offsetX"] = o.GetOffset().dx;
            office["offsetY"] = o.GetOffset().dy;
            offices.push_back(office);
        }
        return offices;
    }

    bool IsSubPath(fs::path path, fs::path base) {
        path = fs::weakly_canonical(path);
        base = fs::weakly_canonical(base);

        for (auto b = base.begin(), p = path.begin(); b != base.end(); ++b, ++p) {
            if (p == path.end() || *p != *b) {
                return false;
            }
        }
        return true;
    }

    std::string DecodeURI(const std::string& encoded) {
        std::string hex_symbols = "0123456789abcdef";
        std::string decoded;

        for (size_t i = 0; i < encoded.size();) {
            if (encoded[i] == '%') {
                assert(i + 2 < encoded.size());
                char first = std::tolower(static_cast<unsigned char>(encoded[i + 1]));
                char second = std::tolower(static_cast<unsigned char>(encoded[i + 2]));
                auto first_pos = hex_symbols.find(first);
                auto second_pos = hex_symbols.find(second);
                assert(first_pos != std::string::npos && second_pos != std::string::npos);
                int symbol = first_pos * 16 + second_pos;
                decoded += static_cast<char>(symbol);
                i += 3;
            } else if (encoded[i] == '+') {
                decoded += ' ';
                ++i;
            } else {
                decoded += encoded[i];
                ++i;
            }
        }

        return decoded;
    }

    std::string GetContentType(const std::string& ext) {
        static const std::map<std::string, std::string> types = {
            { ".htm", "text/html"},
            { ".html", "text/html"},
            { ".css", "text/css"},
            { ".txt", "text/plain" },
            { ".js", "text/javascript" },
            { ".json", "application/json" },
            { ".xml", "application/xml" },
            { ".png", "image/png" },
            { ".jpg", "image/jpeg" },
            { ".jpe", "image/jpeg" },
            { ".jpeg", "image/jpeg" },
            { ".gif", "image/gif" },
            { ".bmp", "image/bmp" },
            { ".ico", "image/vnd.microsoft.icon" },
            { ".tiff", "image/tiff" },
            { ".tif", "image/tiff" },
            { ".svg", "image/svg+xml" },
            { ".svgz", "image/svg+xml" },
            { ".mp3", "audio/mpeg" }
        };

        auto it = types.find(ext);
        if (it == types.end()) {
            return "application/octet-stream";
        }
        return it->second;
    }
};

}  // namespace http_handler