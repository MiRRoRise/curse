#include "websocket_session.hpp"
#include <iostream>


websocket_session::
websocket_session(
    tcp::socket&& socket,
    boost::shared_ptr<shared_state> const& state,int id)
    : ws_(std::move(socket))
    , state_(state),id(id)
{
    int res = sqlite3_open(state_->db_root().c_str(), &db);
    if (res != SQLITE_OK) {
        sqlite3_close(db);
        fail(boost::asio::error::fault, "unable to connect with db");

    }
    sqlite3_busy_timeout(db, 5000);
}

void websocket_session::getMyId()
{
    boost::json::object obj;
    obj["topic"] = 7;
    obj["user_id"] = id;
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    send(ss);
}

websocket_session::
~websocket_session()
{
    state_->leave(this);
    sqlite3_close(db);
}

void
websocket_session::
fail(beast::error_code ec, char const* what)
{
    if (ec == net::error::operation_aborted ||
        ec == websocket::error::closed)
        return;

    std::cerr << what << ": " << ec.message() << "\n";
}

void websocket_session::on_accept(beast::error_code ec)
{
    if (ec)
        return fail(ec, "accept");
    
    std::cout << "Adding session to shared state\n";
    state_->join(this);
    std::cout << "Sending user list\n";
    state_->getUserList(this);
    std::cout << "Sending chat list\n";
    state_->getChatList(this);
    std::cout << "Sending user ID\n";
    getMyId();
    std::cout << "Starting async read\n";
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &websocket_session::on_read,
            shared_from_this()));
}

void
websocket_session::
on_read(beast::error_code ec, std::size_t)
{    
    if (ec)
        return fail(ec, "read");

    state_->parse(beast::buffers_to_string(buffer_.data()), this);
    
    buffer_.consume(buffer_.size());

    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &websocket_session::on_read,
            shared_from_this()));
}

void
websocket_session::
send(boost::shared_ptr<std::string const> const& ss)
{
    net::post(
        ws_.get_executor(),
        beast::bind_front_handler(
            &websocket_session::on_send,
            shared_from_this(),
            ss));
}

void
websocket_session::
on_send(boost::shared_ptr<std::string const> const& ss)
{
    queue_.push_back(ss);

    if (queue_.size() > 1)
        return;

    ws_.async_write(
        net::buffer(*queue_.front()),
        beast::bind_front_handler(
            &websocket_session::on_write,
            shared_from_this()));
}

void
websocket_session::
on_write(beast::error_code ec, std::size_t)
{
    if (ec)
        return fail(ec, "write");

    queue_.erase(queue_.begin());

    if (!queue_.empty())
        ws_.async_write(
            net::buffer(*queue_.front()),
            beast::bind_front_handler(
                &websocket_session::on_write,
                shared_from_this()));
}
