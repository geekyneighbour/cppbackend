#include "http_server.h"

namespace http_server {

    SessionBase::SessionBase(tcp::socket&& socket)
        : stream_(std::move(socket)) {
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
            ReportError(ec, "read");
            return;
        }

        HandleRequest(std::move(request_));
    }

    void SessionBase::OnWrite(bool close, beast::error_code ec, std::size_t) {
        if (ec) {
            ReportError(ec, "write");
            return;
        }

        if (close) {
            return Close();
        }

        Read();
    }

    void SessionBase::Close() {
        stream_.socket().shutdown(tcp::socket::shutdown_send);
        std::cout << "Session is closed" << std::endl;
    }

}  // namespace http_server
