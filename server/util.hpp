#ifndef SRAVZ_BACKENDCPP_UTIL_H
#define SRAVZ_BACKENDCPP_UTIL_H

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
#include <boost/beast/websocket.hpp>
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

using tcp = boost::asio::ip::tcp;     

namespace beast = boost::beast;        
namespace http = beast::http;           
namespace websocket = beast::websocket; 
namespace net = boost::asio;            
namespace json = boost::json;

typedef boost::asio::io_context::executor_type executor_type;
typedef boost::asio::strand<executor_type> strand;

class ThreadInfo
{
public:
    beast::flat_buffer buffer;
    
    ThreadInfo() {}
    
};
typedef std::map<boost::thread::id, ThreadInfo> ThreadsInfo;

bool getenv(const char* name, std::string& env);
std::string decToHexa(size_t n);
std::string SHA256HashString(std::string aString);

#endif 
