#include "spawn.hpp"
#include "socks5.hpp"

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
//#include <boost/asio/ip/v4_only.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/multiprecision/cpp_int.hpp>

#include <charconv>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <type_traits>

template<typename... Ts>
std::string format(Ts &&... args) {
    std::ostringstream oss;
    (oss <<...<< std::forward<Ts>(args));
    return oss.str();
}

template<typename T>
std::optional<T> from_chars(std::string_view sv) noexcept {
    T out;
    auto end = sv.data() + sv.size();
    auto res = std::from_chars(sv.data(), end, out);
    if (res.ec == std::errc{} && res.ptr == end)
        return out;
    return {};
}

class logger {
public:
    template<typename T>
    logger &operator<<(T &&x) {
        std::cerr << std::forward<T>(x);
        return *this;
    }

    ~logger() {
        std::cerr << std::endl;
    }

private:
    static std::mutex m_;

    std::lock_guard<std::mutex> l_{m_};
};

std::mutex logger::m_;

using bigint = boost::multiprecision::cpp_int;

template<typename F>
auto at_scope_exit(F &&f) {
    using f_t = std::remove_cvref_t<F>;
    static_assert(std::is_nothrow_destructible_v<f_t> &&
                  std::is_nothrow_invocable_v<f_t>);
    struct ase_t {
        F f;

        ase_t(F &&f)
                : f(std::forward<F>(f)) {}

        ase_t(const ase_t &) = default;

        ase_t(ase_t &&) = delete;

        ase_t operator=(const ase_t &) = delete;

        ase_t operator=(ase_t &&) = delete;

        ~ase_t() {
            std::forward<F>(f)();
        }
    };
    return ase_t{std::forward<F>(f)};
}

int main(int argc, char *argv[]) {

    try {
        if (argc != 2)
            throw std::runtime_error(format("Usage: ", argv[0], " <listen-port>"));
        auto port = from_chars<std::uint16_t>(argv[1]);
        if (!port || !*port)
            throw std::runtime_error("Port must be in [1;65535]");
        boost::asio::io_context ctx;
        boost::asio::signal_set stop_signals{ctx, SIGINT, SIGTERM};
        stop_signals.async_wait([&](boost::system::error_code ec, int /*signal*/) {
            if (ec)
                return;
            logger{} << "Terminating in response to signal.";
            ctx.stop();
        });

        bccoro::spawn(bind_executor(ctx, [&ctx, port = *port](bccoro::yield_context yc) {

            boost::asio::ip::tcp::acceptor acceptor{ctx};
            boost::asio::ip::tcp::endpoint ep{boost::asio::ip::tcp::v4(), port};

            //acceptor.open(ep.protocol());

            boost::asio::ip::tcp::socket socket{make_strand(acceptor.get_executor())};
            boost::system::error_code ec;

            socket.async_connect(ep, yc[ec]);

            constexpr static std::chrono::seconds timeout{20};

            boost::asio::io_context io_context;
            boost::asio::ip::tcp::resolver resolver(io_context);

            auto http_endpoint = *resolver.resolve(
                    boost::asio::ip::tcp::v4(), "ya.ru", "https");

            if (ec == boost::asio::error::operation_aborted)
                return;
            if (ec)
                logger{} << "Failed to connect: " << ec.message();
            else {
                for (;;) {


                    socks5::request_first socks_request_first;

                    //stream.expires_after(timeout);

                    async_write(socket, socks_request_first.buffers(), yc[ec]);
                    if (ec) {
                        if (ec != boost::asio::error::operation_aborted)
                            logger{} << "Failed to write first request: "
                                     << ec.message();
                        return;
                    }

                    //stream.expires_after(timeout);
                    socks5::reply_first socks_reply_first;
                    socket.async_read_some(socks_reply_first.buffers(), yc[ec]);

                    if (ec) {
                        if (ec != boost::asio::error::operation_aborted)
                            logger{} << "First reply error: " << ec.message();
                        return;
                    }

                    //stream.expires_after(timeout);
                    socks5::request_second socks_request_second(
                            socks5::request_second::connect, http_endpoint);

                    async_write(socket, socks_request_second.buffers(), yc[ec]);
                    if (ec) {
                        if (ec != boost::asio::error::operation_aborted)
                            logger{} << "Failed to write second request: "
                                     << ec.message();
                        return;
                    }

                    //stream.expires_after(timeout);
                    socks5::reply_second socks_reply_second;
                    socket.async_read_some(socks_reply_second.buffers(), yc[ec]);

                    if (ec) {
                        if (ec != boost::asio::error::operation_aborted)
                            logger{} << "Second reply error: " << ec.message();
                        return;
                    }

                    std::cout << "1\n";

                    /*if (ec == boost::asio::error::operation_aborted)
                        return;
                    if (ec)
                        logger{} << "Failed to connect: " << ec.message();
                    else*/

                    bccoro::spawn(bind_executor(socket.get_executor(),
                                                [stream = boost::beast::tcp_stream{std::move(socket)}]
                                                        (bccoro::yield_context yc) mutable {

                                                    //constexpr static std::chrono::seconds timeout{20};
                                                    //constexpr static std::size_t limit = 1024;
                                                    boost::system::error_code ec;
                                                    std::cout << "2\n";
                                                    //std::string in_buf, out_buf;
                                                    //bigint a;

                                                    /*auto read_buffered_number = [&](std::size_t n) {
                                                        bigint x{in_buf.substr(0, n - 1)};
                                                        in_buf.erase(0, n);
                                                        return x;
                                                    };*/

                                                    for (;;) {

                                                        std::string request =
                                                                "GET / HTTPS/1.0\r\n"
                                                                "Host: www.ya.ru\r\n"
                                                                "Accept: */*\r\n"
                                                                "Connection: close\r\n\r\n";

                                                        // Send the HTTP request.
                                                        async_write(stream, boost::asio::buffer(request), yc[ec]);

                                                        // Read until EOF, writing data to output as we go.
                                                        std::array<char, 512> response{};
                                                        boost::system::error_code error;

                                                        while (std::size_t s = stream.async_read_some(
                                                                boost::asio::buffer(response), yc[ec]))
                                                            std::cout.write(response.data(), s);

                                                        if (ec) {
                                                            if (ec != boost::asio::error::operation_aborted)
                                                                logger{} << "Close Error: " << ec.message();
                                                            return;
                                                        }

                                                        std::cout << "3\n";

                                                        /*
                                                        std::size_t n = async_read_until(
                                                                stream,
                                                                boost::asio::dynamic_string_buffer(in_buf, limit),
                                                                '\n', yc[ec]);
                                                        if (ec) {
                                                            if (ec != boost::asio::error::operation_aborted &&
                                                                (ec != boost::asio::error::eof || n))
                                                                logger{} << "Failed to read a: " << ec.message();
                                                            return;
                                                        }
                                                        try {
                                                            a = read_buffered_number(n);
                                                        }
                                                        catch (...) {
                                                            logger{}
                                                                    << boost::current_exception_diagnostic_information();
                                                            return;
                                                        }
                                                        stream.expires_after(timeout);
                                                        n = async_read_until(
                                                                stream,
                                                                boost::asio::dynamic_string_buffer(in_buf, limit),
                                                                '\n', yc[ec]);
                                                        if (ec) {
                                                            if (ec != boost::asio::error::operation_aborted &&
                                                                (ec != boost::asio::error::eof || n))
                                                                logger{} << "Failed to read b: " << ec.message();
                                                            return;
                                                        }
                                                        try {
                                                            out_buf = bigint{a * read_buffered_number(n)}.str() + '\n';
                                                        }
                                                        catch (...) {
                                                            logger{}
                                                                    << boost::current_exception_diagnostic_information();
                                                            return;
                                                        }

                                                        stream.expires_after(timeout);
                                                        async_write(stream, boost::asio::buffer(out_buf), yc[ec]);
                                                        if (ec) {
                                                            if (ec != boost::asio::error::operation_aborted)
                                                                logger{} << "Failed to write result: " << ec.message();
                                                            return;
                                                        }
                                                        */
                                                    }
                                                }));
                }
            }
        }));
        std::vector<std::thread> workers;
        size_t extra_workers = std::thread::hardware_concurrency() - 1;
        workers.reserve(extra_workers);
        auto ase = at_scope_exit([&]() noexcept {
            for (auto &t:workers)
                t.join();
        });
        for (size_t i = 0; i < extra_workers; ++i)
            workers.emplace_back([&] {
                ctx.run();
            });
        ctx.run();
    }
    catch (...) {
        logger{} << boost::current_exception_diagnostic_information();
        return 1;
    }
}
