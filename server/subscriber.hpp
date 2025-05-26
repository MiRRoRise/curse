#ifndef SRAVZ_WEB_SUBSCRIBER_HPP
#define SRAVZ_WEB_SUBSCRIBER_HPP

#include "util.hpp"
#include "sqlite/sqlite3.h"
#include "shared_state.hpp"

class subscriber : public boost::enable_shared_from_this<subscriber>
{
    net::io_context& ioc_subscriber_;
    boost::shared_ptr<shared_state> state_;
    sqlite3* db;
public:
    explicit
        subscriber(net::io_context& ioc_subscriber, boost::shared_ptr<shared_state> const& state);
    void subscribe();
};

#endif