#ifndef BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_SHARED_STATE_HPP
#define BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_SHARED_STATE_HPP

#include <chrono>
#include <utility>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <vector>    
#include <optional>    
#include <boost/enable_shared_from_this.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include "util.hpp"
#include "parser.hpp"
#include "sqlite/sqlite3.h"

class websocket_session;
class symbol;

class shared_state : public boost::enable_shared_from_this<shared_state>
{
    std::string const doc_root_;
    std::string const db_root_;
    parser parser_;

public:
    explicit shared_state(std::string doc_root, std::string db_root);

    std::string const& doc_root() const noexcept
    {
        return doc_root_;
    }

    std::string const& db_root() const noexcept
    {
        return db_root_;
    }

    void join(websocket_session* session);
    void leave(websocket_session* session);
    void parse(std::string msg, websocket_session* session);
    void websocket_subscribe_to_symbols(websocket_session* session, std::string chatId);
    void websocket_unsubscribe_to_symbols(websocket_session* session);
    void searchUsersByName(websocket_session* session, std::string searchTerm);
    void deleteUserFromChat(websocket_session* session, int chatId);
    void updateAccount(websocket_session* session, int userId, const std::string& newName, const std::string& newPassword);
    void deleteUserAccount(websocket_session* session);
    void getUserList(websocket_session* session);
    void newUser(std::string name, int id);
    void inviteToChat(websocket_session* session, int chatId, std::vector<int> userId, int parentUser);
    void getUserInChatList(websocket_session* session);
    void getChatList(websocket_session* session);
    void createChat(websocket_session* session, std::string chatName, std::vector<std::string> invited, bool isVoiceChat = false); 
    void getMessageList(websocket_session* session);
    void sendMsg(websocket_session* session, std::string message);
    void addFriend(websocket_session* session, int userId, int friendId);
    void getFriendsList(websocket_session* session, int userId);
    void acceptFriendRequest(websocket_session* session, int userId, int friendId);
    void deleteFriend(websocket_session* session, int friendId); 
    void logout(websocket_session* session);                    
    void deleteVoiceChat(websocket_session* session, int chatId); 

    std::mutex mutex_;
    std::map<std::string, boost::shared_ptr<symbol>> symbols_;
    boost::lockfree::spsc_queue<std::pair<std::string, std::string>, boost::lockfree::capacity<1024>> spsc_queue_;
    boost::lockfree::spsc_queue<std::tuple<parser::MsgType, uint32_t, uint32_t, std::string, int64_t, std::optional<std::vector<int>>>, boost::lockfree::capacity<1024>> spsc_queue_subscriber_;
    std::unordered_map<std::string, websocket_session*> sess___;
    std::unordered_set<websocket_session*> sessions_;
};

#endif // BOOST_BEAST_EXAMPLE_WEBSOCKET_CHAT_MULTI_SHARED_STATE_HPP