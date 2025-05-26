#ifndef SRAVZ_SYMBOL_HPP
#define SRAVZ_SYMBOL_HPP

#include "util.hpp"
#include "websocket_session.hpp"
#include <boost/atomic.hpp>

class symbol : public boost::enable_shared_from_this<symbol>
{
public:
    std::string code;
    std::string quote;
    std::chrono::milliseconds::rep time;
    mutable boost::atomic<int> refcount;
    std::unordered_set<websocket_session*> sessions_;
    std::mutex mutex_;
    explicit
        symbol(std::string code, std::string quote, std::chrono::milliseconds::rep time);
    void join(websocket_session* session);
    int leave(websocket_session* session);
};

#endif // SRAVZ_SYMBOL_HPP