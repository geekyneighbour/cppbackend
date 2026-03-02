#ifdef WIN32
#include <sdkddkver.h>
#endif

#include "seabattle.h"

#include <atomic>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <string_view>

namespace net = boost::asio;
using net::ip::tcp;
using namespace std::literals;

void PrintFieldPair(const SeabattleField& left, const SeabattleField& right) {
    auto left_pad = "  "s;
    auto delimeter = "    "s;
    std::cout << left_pad;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << delimeter;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << std::endl;
    for (size_t i = 0; i < SeabattleField::field_size; ++i) {
        std::cout << left_pad;
        left.PrintLine(std::cout, i);
        std::cout << delimeter;
        right.PrintLine(std::cout, i);
        std::cout << std::endl;
    }
    std::cout << left_pad;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << delimeter;
    SeabattleField::PrintDigitLine(std::cout);
    std::cout << std::endl;
}

template <size_t sz>
static std::optional<std::string> ReadExact(tcp::socket& socket) {
    boost::array<char, sz> buf;
    boost::system::error_code ec;

    net::read(socket, net::buffer(buf), net::transfer_exactly(sz), ec);

    if (ec) {
        return std::nullopt;
    }

    return {{buf.data(), sz}};
}

static bool WriteExact(tcp::socket& socket, std::string_view data) {
    boost::system::error_code ec;

    net::write(socket, net::buffer(data), net::transfer_exactly(data.size()), ec);

    return !ec;
}

class SeabattleAgent {
public:
    SeabattleAgent(const SeabattleField& field)
        : my_field_(field) {
    }

    void StartGame(tcp::socket& socket, bool my_initiative) {
        // TODO: реализуйте самостоятельно
        PrintFields();
        while (!IsGameEnded()) {
            if (my_initiative) {
				std::cout << "Your turn: ";
                std::string shoot;
                std::cin >> shoot;
                auto move = ParseMove(shoot);
                if (move) {
					if(WriteExact(socket, shoot)){
						auto data = ReadExact<2>(socket);
						if(data){
                    if (data.value() == "MS") {
                        other_field_.MarkMiss(move->second, move->first);
						std::cout << "Miss!" << std::endl;
						my_initiative = false;
                    }
                    else if(data.value() == "HT") {
                        other_field_.MarkHit(move->second, move->first);
						std::cout << "Hit!" << std::endl;
                    }
                    else if (data.value() == "KL") {
                        other_field_.MarkKill(move->second, move->first);
						std::cout << "Kill!" << std::endl;
                    }
                }
						}
					}  
            }
            else {
				std::cout << "Waiting for turn..." << std::endl;
                auto shoot = ReadExact<2>(socket);
				if(shoot){
					std::cout << "Shot to " << shoot.value() << std::endl;
					auto move = ParseMove(shoot.value());
					SeabattleField::ShotResult res = my_field_.Shoot(move->second, move->first);
                    std::string answer;
                    if (res == SeabattleField::ShotResult::MISS) {
                        answer = "MS";
                        my_initiative = true;
                    }
                    else if (res == SeabattleField::ShotResult::HIT) {
                        answer = "HT";
                    }
                    else if (res == SeabattleField::ShotResult::KILL) {
                        answer = "KL";
                    }
                    WriteExact(socket, answer);
				}
                
                    
                
            }
            PrintFields();
        }

    }

private:
    static std::optional<std::pair<int, int>> ParseMove(const std::string_view& sv) {
        if (sv.size() != 2) return std::nullopt;

        int p1 = sv[0] - 'A', p2 = sv[1] - '1';

        if (p1 < 0 || p1 > 8) return std::nullopt;
        if (p2 < 0 || p2 > 8) return std::nullopt;

        return {{p1, p2}};
    }

    static std::string MoveToString(std::pair<int, int> move) {
        char buff[] = {static_cast<char>(move.first) + 'A', static_cast<char>(move.second) + '1'};
        return {buff, 2};
    }

    void PrintFields() const {
        PrintFieldPair(my_field_, other_field_);
    }

    bool IsGameEnded() const {
        return my_field_.IsLoser() || other_field_.IsLoser();
    }

    // TODO: добавьте методы по вашему желанию

private:
    SeabattleField my_field_;
    SeabattleField other_field_;
};

void StartServer(const SeabattleField& field, unsigned short port) {
    SeabattleAgent agent(field);

    net::io_context io_context;

    tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
    std::cout << "Waiting for connection..."sv << std::endl;

    boost::system::error_code ec;
    tcp::socket socket{ io_context };
    acceptor.accept(socket, ec);

    if (ec) {
        std::cout << "Can't accept connection"sv << std::endl;
        return;
    }
    agent.StartGame(socket, false);
};

void StartClient(const SeabattleField& field, const std::string& ip_str, unsigned short port) {
    SeabattleAgent agent(field);

    // TODO: реализуйте самостоятельно
	boost::system::error_code ec;
    auto endpoint = tcp::endpoint(net::ip::make_address(ip_str, ec), port);

    if (ec) {
        std::cout << "Wrong IP format"sv << std::endl;
        return;
    }

    net::io_context io_context;
tcp::socket socket{io_context};
socket.connect(endpoint, ec);

if (ec) {
    std::cout << "Can't connect to server"sv << std::endl;
    return;
}

    agent.StartGame(socket, true);
};

int main(int argc, const char** argv) {
    if (argc != 3 && argc != 4) {
        std::cout << "Usage: program <seed> [<ip>] <port>" << std::endl;
        return 1;
    }

    std::mt19937 engine(std::stoi(argv[1]));
    SeabattleField fieldL = SeabattleField::GetRandomField(engine);

    if (argc == 3) {
        StartServer(fieldL, std::stoi(argv[2]));
    } else if (argc == 4) {
        StartClient(fieldL, argv[2], std::stoi(argv[3]));
    }
}
