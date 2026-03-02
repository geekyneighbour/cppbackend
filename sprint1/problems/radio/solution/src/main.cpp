#include <boost/asio.hpp>
#include "audio.h"
#include <iostream>
#include <string>

using namespace std::literals;

namespace net = boost::asio;
using net::ip::udp;


void StartClient(uint16_t port) {
    static const size_t max_buffer_size = 1024;
	std::string host = "127.0.0.1";

    try {
        net::io_context io_context;

        // Перед отправкой данных нужно открыть сокет. 
        // При открытии указываем протокол (IPv4 или IPv6) вместо endpoint.
        udp::socket socket(io_context, udp::v4());

        boost::system::error_code ec;
        auto endpoint = udp::endpoint(net::ip::make_address(host, ec), port);
        Recorder record(ma_format_u8, 1);
        Recorder::RecordingResult res = record.Record(65000, 1.5s);
        auto data_size = res.frames * record.GetFrameSize();
        socket.send_to(boost::asio::buffer(res.data, data_size), endpoint);

    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

void StartServer(uint16_t port) {
    static const size_t max_buffer_size = 65536;

    try {
        boost::asio::io_context io_context;

        udp::socket socket(io_context, udp::endpoint(udp::v4(), port));

        // Запускаем сервер в цикле, чтобы можно было работать со многими клиентами
        for (;;) {
            // Создаём буфер достаточного размера, чтобы вместить датаграмму.
            std::array<char, max_buffer_size> recv_buf;
            udp::endpoint remote_endpoint;

            // Получаем не только данные, но и endpoint клиента
            auto size = socket.receive_from(boost::asio::buffer(recv_buf), remote_endpoint);

            Player play(ma_format_u8, 1);
            size_t frame_size = size / play.GetFrameSize();
            play.PlayBuffer(recv_buf.data(), frame_size, 1.5s);
            //std::cout << "Client said "sv << std::string_view(recv_buf.data(), size) << std::endl;

            // Отправляем ответ на полученный endpoint, игнорируя ошибку.
            // На этот раз не отправляем перевод строки: размер датаграммы будет получен автоматически.
        }
    }
    catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}

int main(int argc, char** argv) {
     if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " <mode> <port>" << std::endl;
        std::cout << "  mode: 'client' or 'server'" << std::endl;
        std::cout << "  port: UDP port number" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    
    // Преобразуем порт из строки в число
    uint16_t port;
    try {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    } catch (const std::exception&) {
        std::cerr << "Invalid port number: " << argv[2] << std::endl;
        return 1;
    }

    if (mode == "server") {
        // Сервер работает постоянно
        StartServer(port);
    } 
    else if (mode == "client") {
        // Клиент в цикле запрашивает IP и отправляет сообщения
        std::cout << "UDP Audio Client started" << std::endl;
        std::cout << "Press Ctrl+C to exit" << std::endl;
        
        while (true) {
            std::cout << "\nPress Enter to record and send audio message..." << std::endl;
            std::cin.get();  // Ждем Enter
            
            StartClient(port);
        }
    }
    else {
        std::cout << "Unknown mode. Use 'client' or 'server'" << std::endl;
        return 1;
    }

    return 0;
}
