#ifndef BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_WEBSOCKET_SESSION_HPP
#define BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_WEBSOCKET_SESSION_HPP

#include "net.hpp"
#include "beast.hpp"
#include "shared_state.hpp"
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>
#include <set>


class shared_state;

class websocket_session : public boost::enable_shared_from_this<websocket_session>
{
    beast::flat_buffer buffer_;
    websocket::stream<beast::tcp_stream> ws_;
    boost::shared_ptr<shared_state> state_;
    std::vector<boost::shared_ptr<std::string const>> queue_;
    uint32_t id;
    parser parser_;
    void fail(beast::error_code ec, char const* what);
    void on_accept(beast::error_code ec);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void on_write(beast::error_code ec, std::size_t bytes_transferred);

public:
    std::set<std::string> topics;
    sqlite3* db;
    websocket_session(
        tcp::socket&& socket,
        boost::shared_ptr<shared_state> const& state,int id);
    void getMyId();
    ~websocket_session();
    uint32_t getId() const { return this->id; }
    template<class Body, class Allocator>
    void
        run(http::request<Body, http::basic_fields<Allocator>> req);

    
    void
        send(boost::shared_ptr<std::string const> const& ss);

private:
    void
        on_send(boost::shared_ptr<std::string const> const& ss);
};

template<class Body, class Allocator>
void
websocket_session::
run(http::request<Body, http::basic_fields<Allocator>> req)
{
    
    ws_.set_option(
        websocket::stream_base::timeout::suggested(
            beast::role_type::server));

    
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::response_type& res)
        {
            res.set(http::field::server,
            std::string(BOOST_BEAST_VERSION_STRING) +
            " websocket-chat-multi");
        }));

    
    ws_.async_accept(
        req,
        beast::bind_front_handler(
            &websocket_session::on_accept,
            shared_from_this()));
}

#endif