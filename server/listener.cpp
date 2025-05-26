#include "listener.hpp"
#include "http_session.hpp"
#include <iostream>

listener::
listener(
    net::io_context& ioc,
    tcp::endpoint endpoint,
    boost::shared_ptr<shared_state> const& state)
    : ioc_(ioc)
    , acceptor_(ioc)
    , state_(state)
{
    beast::error_code ec;

    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
    {
        fail(ec, "open");
        return;
    }

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
    {
        fail(ec, "set_option");
        return;
    }

    acceptor_.bind(endpoint, ec);
    if (ec)
    {
        fail(ec, "bind");
        return;
    }

    acceptor_.listen(
        net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        fail(ec, "listen");
        return;
    }
}

void
listener::
run()
{
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(
            &listener::on_accept,
            shared_from_this()));
}

void
listener::
fail(beast::error_code ec, char const* what)
{
    if (ec == net::error::operation_aborted)
        return;
    std::cerr << what << ": " << ec.message() << "\n";
}

void
listener::
on_accept(beast::error_code ec, tcp::socket socket)
{
    if (ec)
        return fail(ec, "accept");
    else
        boost::make_shared<http_session>(
            std::move(socket),
            state_)->run();

    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(
            &listener::on_accept,
            shared_from_this()));

}