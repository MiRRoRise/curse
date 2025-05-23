#ifndef RTP_SERVER_HPP
#define RTP_SERVER_HPP

#include <boost/asio.hpp>
#include <unordered_map>
#include <mutex>
#include <utility>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <memory>
#include <cctype>

using boost::asio::ip::udp;
using namespace std::chrono;

const int RTP_PORT = 5004;
const int BUFFER_SIZE = 4096;
const int CLIENT_TIMEOUT_SEC = 10;
const int MAX_CLIENTS = 50;
const int MAX_CHANNEL_LENGTH = 64;

class RTPServer {
public:
    RTPServer(unsigned short port);

    void run();
    void startReceive();
    void handleReceive(std::size_t bytes_recvd);
    std::string validateChannelName(const std::string& channel);
    void handleClientRegistration(const std::string& channel);
    void broadcast(const char *data, std::size_t length, const udp::endpoint &sender, const std::string& channel);
    void sendToClient(const udp::endpoint &client, const char *data, std::size_t length);
    void startCleanupTimer();
    void cleanupInactiveClients();

private:
    void log(const std::string &message);
    void logError(const std::string &message);

    boost::asio::io_context io_context_;
    udp::socket socket_;
    udp::endpoint remote_endpoint_;
    std::array<char, BUFFER_SIZE> data_;
    std::unordered_map<udp::endpoint, std::pair<steady_clock::time_point, std::string>> clients_;
    std::unordered_map<std::string, std::vector<udp::endpoint>> channels_;
    std::mutex clients_mutex_;
    boost::asio::steady_timer cleanup_timer_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
};

#endif // RTP_SERVER_HPP