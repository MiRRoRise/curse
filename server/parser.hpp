#ifndef SRAVZ_WEB_PARSER_HPP
#define SRAVZ_WEB_PARSER_HPP

#include <boost/json.hpp>
#include "util.hpp"

class parser : public boost::enable_shared_from_this<parser>
{
    boost::json::parse_options opt_{};

public:
    parser();
    boost::json::value parse(const std::string& s);

    enum class MsgType {
        SUBSCRIBE = 1,
        UNSUBSCRIBE = 2,
        MESSAGE = 3,
        CreateChat = 4,
        GetChatList = 5,
        GetMessageList = 6,
        DeleteUserFromChat = 7,
        DeleteUserAccount = 8,
        GetUserList = 9,
        InviteToChat = 10,
        GetUserInChatList = 11,
        SearchUsersByName = 12,
        AddFriend = 13,
        GetFriendsList = 14,
        AcceptFriendRequest = 15,
        RejectFriendRequest = 16,
        NewFriendRequest = 17,
        DeleteFriend = 18, 
        UpdateAccount = 20,
        DeleteVoiceChat = 21,
        Logout = 22
    };
};

#endif
