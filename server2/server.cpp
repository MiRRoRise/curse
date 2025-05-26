#include "server.hpp"
#include <iostream>

RTPServer::RTPServer(unsigned short port)
    : socket_(io_context_, udp::endpoint(udp::v4(), port)),
      cleanup_timer_(io_context_), 
      strand_(boost::asio::make_strand(io_context_))
{
    socket_.non_blocking(true);
    startReceive();
    startCleanupTimer(); 
}

void RTPServer::run()
{
    io_context_.run();
}

void RTPServer::startReceive() {
    socket_.async_receive_from(
        boost::asio::buffer(data_), remote_endpoint_,
        boost::asio::bind_executor(strand_,
            [this](const boost::system::error_code &error, std::size_t bytes_recvd) {
                if (!error && bytes_recvd > 0) {
                    handleReceive(bytes_recvd);
                } else if (error) {
                    logError("Receive error: " + error.message());
                }
                startReceive();
            }));
}

void RTPServer::handleReceive(std::size_t bytes_recvd) {
    if (bytes_recvd >= 4 && std::string(data_.data(), 4) == "PING") {
        log("Received PING from " + remote_endpoint_.address().to_string());
        sendToClient(remote_endpoint_, "PONG", 4);
        return;
    }

    if (bytes_recvd >= 8 && std::string(data_.data(), 8) == "REGISTER") {
        if (bytes_recvd > 9) {
            std::string channel(data_.data() + 9, bytes_recvd - 9);
            handleClientRegistration(validateChannelName(channel));
        } else {
            logError("Invalid REGISTER message - missing channel name");
            sendToClient(remote_endpoint_, "ERROR:INVALID_CHANNEL", 20);
        }
        return;
    }

    if (bytes_recvd >= 6 && std::string(data_.data(), 6) == "AUDIO ") {
        size_t space_pos = std::find(data_.data() + 6, data_.data() + bytes_recvd, ' ') - data_.data();
        if (space_pos < bytes_recvd) {
            std::string channel(data_.data() + 6, space_pos - 6);
            channel = validateChannelName(channel);
            
            if (!channel.empty()) {
                std::lock_guard<std::mutex> lock(clients_mutex_);
                auto now = steady_clock::now();
                auto &cl_time = clients_[remote_endpoint_].first;
                cl_time = now;
                clients_[remote_endpoint_].second = channel;

                broadcast(data_.data(), bytes_recvd, remote_endpoint_, channel);
            }
        }
        return;
    }

    logError("Unknown message type from " + remote_endpoint_.address().to_string());
}

std::string RTPServer::validateChannelName(const std::string& channel) {
    if (channel.empty() || channel.length() > MAX_CHANNEL_LENGTH) {
        logError("Invalid channel length from " + remote_endpoint_.address().to_string());
        return "";
    }

    if (!std::all_of(channel.begin(), channel.end(), [](char c) {
        return std::isalnum(c) || c == '-' || c == '_';
    })) {
        logError("Invalid channel characters from " + remote_endpoint_.address().to_string());
        return "";
    }

    return channel;
}

void RTPServer::handleClientRegistration(const std::string& channel) {
    if (channel.empty()) {
        sendToClient(remote_endpoint_, "ERROR:INVALID_CHANNEL", 20);
        return;
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto client_it = clients_.find(remote_endpoint_);
    bool is_new_client = (client_it == clients_.end());

    if (is_new_client) {
        if (clients_.size() >= MAX_CLIENTS) {
            logError("Max clients reached (" + std::to_string(MAX_CLIENTS) + ")");
            sendToClient(remote_endpoint_, "ERROR:SERVER_FULL", 16);
            return;
        }

        clients_[remote_endpoint_] = {steady_clock::now(), channel};
        channels_[channel].push_back(remote_endpoint_);
        log("New client registered: " + remote_endpoint_.address().to_string() + 
            " to channel: " + channel);
        sendToClient(remote_endpoint_, "REGISTERED", 10);
    } else {
        auto& old_channel = client_it->second.second;

        if (old_channel != channel) {
            auto& old_channel_clients = channels_[old_channel];
            old_channel_clients.erase(
                std::remove(old_channel_clients.begin(), old_channel_clients.end(), remote_endpoint_),
                old_channel_clients.end());

            client_it->second = {steady_clock::now(), channel};
            channels_[channel].push_back(remote_endpoint_);
            log("Client changed channel: " + remote_endpoint_.address().to_string() + 
                " from " + old_channel + " to " + channel);
            sendToClient(remote_endpoint_, "RE-REGISTERED", 13);
        } else {
            client_it->second.first = steady_clock::now();
            sendToClient(remote_endpoint_, "RE-REGISTERED", 13);
        }
    }

    log("Active clients: " + std::to_string(clients_.size()) + 
        ", Channel " + channel + " clients: " + 
        std::to_string(channels_[channel].size()));
}

void RTPServer::broadcast(const char *data, std::size_t length, const udp::endpoint &sender, const std::string& channel) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto channel_it = channels_.find(channel);
    if (channel_it == channels_.end()) {
        logError("XYITA " + channel);
        return;
    }
    int counter = 0;
    for (const auto &client : channel_it->second) {
        std::cout << ++counter << "\n";
        if (client != sender) {
            sendToClient(client, data, length);
        }
    }
}

void RTPServer::sendToClient(const udp::endpoint &client, const char *data, std::size_t length)
{
    socket_.async_send_to(
        boost::asio::buffer(data, length), client,
        boost::asio::bind_executor(strand_,
            [this, client](const boost::system::error_code &error, std::size_t bytes_sent) {
                (void)bytes_sent;
                if (error) {
                    logError("Error sending to " + client.address().to_string() +
                            ":" + std::to_string(client.port()) +
                            " - " + error.message());
                }
            }));
}

void RTPServer::startCleanupTimer() {
    cleanup_timer_.expires_after(seconds(CLIENT_TIMEOUT_SEC));
    cleanup_timer_.async_wait(
        boost::asio::bind_executor(strand_,
            [this](const boost::system::error_code &error) {
                if (!error) {
                    cleanupInactiveClients();
                    startCleanupTimer();
                }
            }));
}

void RTPServer::cleanupInactiveClients() {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto now = steady_clock::now();
    size_t removed = 0;

    for (auto it = clients_.begin(); it != clients_.end();) {
        if (duration_cast<seconds>(now - it->second.first).count() > CLIENT_TIMEOUT_SEC) {
            log("Removing inactive client: " +
                it->first.address().to_string() + ":" +
                std::to_string(it->first.port()));

            auto& channel_clients = channels_[it->second.second];
            channel_clients.erase(
                std::remove(channel_clients.begin(), channel_clients.end(), it->first),
                channel_clients.end());

            if (channel_clients.empty()) {
                channels_.erase(it->second.second);
            }

            it = clients_.erase(it);
            removed++;
        } else {
            ++it;
        }
    }

    if (removed > 0) {
        log("Removed " + std::to_string(removed) + " inactive clients");
        log("Active clients: " + std::to_string(clients_.size()));
    }
}

void RTPServer::log(const std::string &message) {
    auto now = system_clock::to_time_t(system_clock::now());
    std::cout << "[SERVER][" << std::put_time(std::localtime(&now), "%T") << "] "
              << message << std::endl;
}

void RTPServer::logError(const std::string &message) {
    auto now = system_clock::to_time_t(system_clock::now());
    std::cerr << "[SERVER][" << std::put_time(std::localtime(&now), "%T") << "] ERROR: "
              << message << std::endl;
}