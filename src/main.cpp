#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <pqxx/pqxx>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace logging = boost::log;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;
using tcp = boost::asio::ip::tcp;

namespace {

struct ServerConfig {
    std::string host = "0.0.0.0";
    std::uint16_t port = 3000;
};

std::uint16_t parse_port(std::string_view value, std::uint16_t fallback) {
    std::uint32_t parsed = 0;
    const char* begin = value.data();
    const char* end = begin + value.size();
    const auto [ptr, error_code] = std::from_chars(begin, end, parsed);
    if (error_code != std::errc{} || ptr != end || parsed == 0 || parsed > 65535) {
        return fallback;
    }
    return static_cast<std::uint16_t>(parsed);
}

ServerConfig server_config_from_env() {
    ServerConfig config;
    if (const char* host = std::getenv("HOST"); host != nullptr && *host != '\0') {
        config.host = host;
    }
    if (const char* port = std::getenv("PORT"); port != nullptr && *port != '\0') {
        config.port = parse_port(port, config.port);
    }
    return config;
}

logging::trivial::severity_level severity_from_env() {
    const char* raw = std::getenv("LOG_LEVEL");
    if (raw == nullptr || *raw == '\0') {
        return logging::trivial::info;
    }

    std::string level{raw};
    for (char& c : level) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    if (level == "trace") {
        return logging::trivial::trace;
    }
    if (level == "debug") {
        return logging::trivial::debug;
    }
    if (level == "info") {
        return logging::trivial::info;
    }
    if (level == "warning" || level == "warn") {
        return logging::trivial::warning;
    }
    if (level == "error") {
        return logging::trivial::error;
    }
    if (level == "fatal") {
        return logging::trivial::fatal;
    }
    return logging::trivial::info;
}

void setup_logging() {
    logging::add_common_attributes();

    auto formatter =
        expr::stream << "[" << expr::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S")
                     << "] [" << logging::trivial::severity << "] " << expr::smessage;

    logging::add_console_log(std::clog, keywords::format = formatter);

    std::string log_dir = "logs";
    if (const char* dir = std::getenv("LOG_DIR"); dir != nullptr && *dir != '\0') {
        log_dir = dir;
    }

    std::error_code error_code;
    std::filesystem::create_directories(log_dir, error_code);
    if (error_code) {
        BOOST_LOG_TRIVIAL(warning) << "Could not create log directory " << log_dir << ": "
                                   << error_code.message();
    } else {
        logging::add_file_log(keywords::file_name = log_dir + "/chouchou_%Y%m%d.log",
                              keywords::rotation_size = 10 * 1024 * 1024,
                              keywords::time_based_rotation =
                                  logging::sinks::file::rotation_at_time_point(0, 0, 0),
                              keywords::format = formatter, keywords::auto_flush = true);
    }

    logging::core::get()->set_filter(logging::trivial::severity >= severity_from_env());
}

bool database_healthy(const std::shared_ptr<pqxx::connection>& db) {
    try {
        pqxx::work tx{*db};
        tx.exec("SELECT 1");
        tx.commit();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

class Session : public std::enable_shared_from_this<Session> {
  public:
    Session(tcp::socket socket, std::shared_ptr<pqxx::connection> db)
        : _socket(std::move(socket)), _db(std::move(db)) {}

    void run() {
        do_read();
    }

  private:
    void do_read() {
        auto self = shared_from_this();
        http::async_read(_socket, _buffer, _request, [self](beast::error_code error_code, std::size_t) {
            if (error_code) {
                return;
            }
            self->handle_request();
        });
    }

    void handle_request() {
        if (_request.method() == http::verb::get && _request.target() == "/health") {
            const bool ok = database_healthy(_db);
            _response.result(ok ? http::status::ok : http::status::service_unavailable);
            _response.body() = ok ? "OK" : "database unavailable";
        } else {
            _response.result(http::status::not_found);
            _response.body() = "not found";
        }
        _response.set(http::field::content_type, "text/plain");
        _response.keep_alive(false);
        _response.prepare_payload();
        do_write();
    }

    void do_write() {
        auto self = shared_from_this();
        http::async_write(_socket, _response,
                          [self](beast::error_code, std::size_t) { self->do_close(); });
    }

    void do_close() {
        beast::error_code error_code;
        _socket.shutdown(tcp::socket::shutdown_send, error_code);
    }

    tcp::socket _socket;
    beast::flat_buffer _buffer;
    http::request<http::string_body> _request;
    http::response<http::string_body> _response;
    std::shared_ptr<pqxx::connection> _db;
};

class Listener : public std::enable_shared_from_this<Listener> {
  public:
    Listener(tcp::acceptor acceptor, std::shared_ptr<pqxx::connection> db)
        : _acceptor(std::move(acceptor)), _db(std::move(db)) {}

    void run() {
        do_accept();
    }

  private:
    void do_accept() {
        auto self = shared_from_this();
        _acceptor.async_accept([self](beast::error_code error_code, tcp::socket socket) {
            if (!error_code) {
                std::make_shared<Session>(std::move(socket), self->_db)->run();
            }
            self->do_accept();
        });
    }

    tcp::acceptor _acceptor;
    std::shared_ptr<pqxx::connection> _db;
};

} // namespace

int main() {
    try {
        setup_logging();

        auto db = std::make_shared<pqxx::connection>();

        if (!db->is_open()) {
            BOOST_LOG_TRIVIAL(error) << "Could not open database connection.";
            return EXIT_FAILURE;
        }

        pqxx::work tx{*db};
        const std::string version = tx.exec("SELECT version()")[0][0].as<std::string>();
        tx.commit();

        BOOST_LOG_TRIVIAL(info) << "Connected to " << db->dbname();
        BOOST_LOG_TRIVIAL(info) << version;

        const ServerConfig config = server_config_from_env();

        asio::io_context io;
        tcp::acceptor acceptor{io, tcp::endpoint{asio::ip::make_address(config.host), config.port}};
        std::make_shared<Listener>(std::move(acceptor), db)->run();

        BOOST_LOG_TRIVIAL(info) << "Listening on " << config.host << ':' << config.port;
        io.run();
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(fatal) << e.what();
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
