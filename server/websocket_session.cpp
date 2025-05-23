//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//

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
    // Remove this session from the list of active sessions
    state_->leave(this);
    sqlite3_close(db);
}

void
websocket_session::
fail(beast::error_code ec, char const* what)
{
    // Don't report these
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
    // Handle the error, if any
    if (ec)
        return fail(ec, "read");

    // Send to all connections
    state_->parse(beast::buffers_to_string(buffer_.data()), this);
    // state_->send(beast::buffers_to_string(buffer_.data()));
    // Implement parser to parse message and subscribe to symbol
    // Clear the buffer
    buffer_.consume(buffer_.size());

    // Read another message
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
    // Post our work to the strand, this ensures
    // that the members of `this` will not be
    // accessed concurrently.

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
    // Always add to queue
    queue_.push_back(ss);

    // Are we already writing?
    if (queue_.size() > 1)
        return;

    // We are not currently writing, so send this immediately
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
    // Handle the error, if any
    if (ec)
        return fail(ec, "write");

    // Remove the string from the queue
    queue_.erase(queue_.begin());

    // Send the next message if any
    if (!queue_.empty())
        ws_.async_write(
            net::buffer(*queue_.front()),
            beast::bind_front_handler(
                &websocket_session::on_write,
                shared_from_this()));
}
