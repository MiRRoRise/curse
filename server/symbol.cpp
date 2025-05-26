#include "symbol.hpp"


symbol::
symbol(std::string code, std::string quote, std::chrono::milliseconds::rep time)
    : code(std::move(code))
    , quote(quote)
    , time(time),
    refcount(0)
{
}

void
symbol::
join(websocket_session* session)
{
    sessions_.insert(session);
}

int
symbol::
leave(websocket_session* session)
{
    return sessions_.erase(session);
}
