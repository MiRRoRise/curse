#include "listener.hpp"
#include "shared_state.hpp"
#include "subscriber.hpp"
#include <boost/asio/signal_set.hpp>
#include <boost/smart_ptr.hpp>
#include <iostream>
#include <vector>

int
main(int argc, char* argv[])
{
    if (argc != 6)
    {
        std::cerr <<
            "Usage: websocket-chat-multi <address> <port> <doc_root> <threads> <db_root>\n" <<
            "Example:\n" <<
            "    websocket-chat-server 0.0.0.0 8080 . 5 .\\db.db\n";
        return EXIT_FAILURE;
    }
    auto address = net::ip::make_address(argv[1]);
    auto port = static_cast<unsigned short>(std::atoi(argv[2]));
    auto doc_root = argv[3];
    auto const threads = std::max<int>(1, std::atoi(argv[4]));
    auto topics_ = argv[5];

    net::io_context ioc;
    net::io_context ioc_subscriber;
    net::io_context ioc_publisher;

    boost::shared_ptr<shared_state> shared_state_ = boost::make_shared<shared_state>(doc_root, topics_);
    boost::shared_ptr<listener> listener_ = boost::make_shared<listener>(ioc, tcp::endpoint{ address, port }, shared_state_);
    boost::shared_ptr<subscriber> subscriber_ = boost::make_shared<subscriber>(ioc_subscriber, shared_state_);
   
    listener_->run();
    subscriber_->subscribe();

    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&ioc, &ioc_subscriber, &ioc_publisher](boost::system::error_code const&, int)
        {
            ioc.stop();
            ioc_subscriber.stop();
            ioc_publisher.stop();
        });

    std::vector<std::thread> v;
    v.reserve(threads);
    for (auto i = threads - 1; i > 0; --i)
        v.emplace_back(
            [&ioc]
            {
                std::cout << "Starting ioc" << std::endl;
                ioc.run();
            });

    v.emplace_back(
        [&ioc_subscriber]
        {
            std::cout << "Starting ioc_subscriber" << std::endl;
            ioc_subscriber.run();
        });

    for (auto& t : v)
        t.join();

    return EXIT_SUCCESS;
}