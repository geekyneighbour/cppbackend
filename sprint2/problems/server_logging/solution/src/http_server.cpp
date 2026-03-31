#include "http_server.h"
#include "logging.h"
#include <boost/json.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/utility/manipulators/add_value.hpp>

namespace json = boost::json;
namespace logging = boost::log;

namespace http_server {
    
    SessionBase::SessionBase(tcp::socket&& socket)
        : stream_(std::move(socket)) {
    }
    
    void SessionBase::LogError(beast::error_code ec, std::string_view where) {
        json::value data{
            {"code", ec.value()},
            {"text", ec.message()},
            {"where", std::string(where)}
        };

        BOOST_LOG_TRIVIAL(error)
            << boost::log::add_value(additional_data, data)
            << "error";
    }

    void SessionBase::Run() {
        net::dispatch(
            stream_.get_executor(),
            beast::bind_front_handler(&SessionBase::Read, GetSharedThis()));
    }

    void SessionBase::Read() {
        request_ = {};

        stream_.expires_after(std::chrono::seconds(30));

        http::async_read(
            stream_,
            buffer_,
            request_,
            beast::bind_front_handler(
                &SessionBase::OnRead,
                GetSharedThis()));
    }

    void SessionBase::OnRead(beast::error_code ec, std::size_t) {
        if (ec == http::error::end_of_stream) {
            return Close();
        }

        if (ec) {
            LogError(ec, "read");
            return;
        }

        HandleRequest(std::move(request_));
    }

    void SessionBase::OnWrite(bool close, beast::error_code ec, std::size_t) {
        if (ec) {
            LogError(ec, "write");
            return;
        }

        if (close) {
            return Close();
        }

        Read();
    }

    void SessionBase::Close() {
        stream_.socket().shutdown(tcp::socket::shutdown_send);
    }

}  // namespace http_server