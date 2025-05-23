#ifndef SRAVZ_BACKENDCPP_UTIL_H
#define SRAVZ_BACKENDCPP_UTIL_H

// Includes
//#include "root_certificates.hpp"
#include <boost/algorithm/string.hpp>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
//#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
//#include <boost/beast/websocket/ssl.hpp>
#include <boost/coroutine2/all.hpp>
#include <boost/foreach.hpp>
#include <boost/json.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/log/trivial.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/thread.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>

#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <boost/url.hpp>
#include <cryptopp/cryptlib.h>
#include <cryptopp/sha.h>
#include <cryptopp/filters.h>
#include <cryptopp/base64.h>


/* Using namespaces */
// TODO: Move this to the cpp files where the namespace is used.
//using RedisInstance = sw::redis::Redis;
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>
// using namespace std::chrono;

// Namespaces
namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace websocket = beast::websocket; // from <boost/beast/websocket.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
//namespace ssl = boost::asio::ssl;       // from <boost/asio/ssl.hpp>
namespace json = boost::json;

typedef boost::asio::io_context::executor_type executor_type;
typedef boost::asio::strand<executor_type> strand;

// Classes
class ThreadInfo
{
public:
    beast::flat_buffer buffer;
    //std::shared_ptr<RedisInstance> redis;
    ThreadInfo() {}
    //ThreadInfo(beast::flat_buffer buffer_, std::shared_ptr<RedisInstance> redis_) : buffer(buffer_), redis(redis_) {}
};
typedef std::map<boost::thread::id, ThreadInfo> ThreadsInfo;

// Functions
// Gets env variable
bool getenv(const char* name, std::string& env);
std::string decToHexa(size_t n);
// Returns redis connection options to be used in redis connection creation
//sw::redis::ConnectionOptions getRedisConnectionOptions();
std::string SHA256HashString(std::string aString);

#endif //endSRAVZ_BACKENDCPP_UTIL_H
