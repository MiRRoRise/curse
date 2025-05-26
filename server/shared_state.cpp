#include "shared_state.hpp"
#include "websocket_session.hpp"
#include "symbol.hpp"
#include "sqlite/sqlite3.h"

shared_state::shared_state(std::string doc_root, std::string db_root)
    : doc_root_(std::move(doc_root))
    , db_root_(db_root)
{
    sqlite3* db;
    sqlite3_open(db_root_.c_str(), &db);
    if (db) {
                const char* createFriendsTable = "CREATE TABLE IF NOT EXISTS Friends ("
            "user_id INTEGER NOT NULL, "
            "friend_id INTEGER NOT NULL, "
            "PRIMARY KEY (user_id, friend_id), "
            "FOREIGN KEY (user_id) REFERENCES Users(id) ON DELETE CASCADE, "
            "FOREIGN KEY (friend_id) REFERENCES Users(id) ON DELETE CASCADE)";
        sqlite3_exec(db, createFriendsTable, NULL, NULL, NULL);

                std::string sql = "SELECT id FROM Chat";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
        while (sqlite3_step(stmt) != SQLITE_DONE) {
            std::string topic = std::to_string(sqlite3_column_int(stmt, 0));
            symbols_[topic] = boost::make_shared<symbol>(topic, "", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        }
        sqlite3_finalize(stmt);
        sqlite3_close(db);
    }
}

void shared_state::join(websocket_session* session)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "Inserting into session" << std::endl;
    sess___[std::to_string(session->getId())] = session;
    sessions_.insert(session);
}

void shared_state::websocket_subscribe_to_symbols(websocket_session* session, std::string chatId) {
        std::string checkSql = "SELECT 1 FROM Chat WHERE id = ? AND EXISTS "
                          "(SELECT 1 FROM UserInChat WHERE chatid = ? AND userid = ?)";
    sqlite3_stmt* checkStmt = nullptr;
    int rc = sqlite3_prepare_v2(session->db, checkSql.c_str(), -1, &checkStmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Ошибка подготовки SQL (проверка подписки): " << sqlite3_errmsg(session->db) << "\n";
        boost::json::object error;
        error["topic"] = 1;
        error["error"] = "Ошибка базы данных";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(checkStmt);
        return;
    }
    int chatIdInt = std::stoi(chatId);
    sqlite3_bind_int(checkStmt, 1, chatIdInt);
    sqlite3_bind_int(checkStmt, 2, chatIdInt);
    sqlite3_bind_int(checkStmt, 3, session->getId());
    if (sqlite3_step(checkStmt) != SQLITE_ROW) {
        std::cerr << "Недействительный chatId " << chatId << " или пользователь " << session->getId() << " не в чате\n";
        boost::json::object error;
        error["topic"] = 1;
        error["error"] = "Недействительный чат или пользователь не в чате";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(checkStmt);
        return;
    }
    sqlite3_finalize(checkStmt);

        {
        std::lock_guard<std::mutex> lock(mutex_);
        session->topics.insert(chatId);
    }
    std::lock_guard<std::mutex> lock(symbols_[chatId]->mutex_);
    symbols_[chatId]->join(session);
    std::cout << "Подписан пользователь " << session->getId() << " на чат " << chatId << std::endl;

        boost::json::object response;
    response["topic"] = 1;
    response["status"] = "subscribed";
    response["chat_id"] = chatIdInt;
    session->send(boost::make_shared<std::string>(boost::json::serialize(response)));
}

void shared_state::websocket_unsubscribe_to_symbols(websocket_session* session)
{
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (session->topics.empty()) return;
        id = *session->topics.begin();
        session->topics.erase(session->topics.begin());
    }
    std::lock_guard<std::mutex> lock(symbols_[id]->mutex_);
    symbols_[id]->leave(session);
}

void shared_state::searchUsersByName(websocket_session* session, std::string searchTerm) {
    std::string sql = "SELECT id, name FROM Users WHERE name LIKE ?";
    sqlite3_stmt* stmt = NULL;
    boost::json::object obj;
    boost::json::array arr;
    obj["topic"] = 12;  
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    std::string searchPattern = "%" + searchTerm + "%";
    sqlite3_bind_text(stmt, 1, searchPattern.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) != SQLITE_DONE) {
        boost::json::object user;
        user["user_id"] = sqlite3_column_int(stmt, 0);
        user["user_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        arr.emplace_back(user);
    }

    sqlite3_finalize(stmt);
    obj["users"] = arr;

    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    session->send(ss);
}

void shared_state::deleteUserFromChat(websocket_session* session, int chatId)
{
    boost::json::object obj;
    obj["topic"] = 9;
    obj["user_id"] = session->getId();
    std::string sql = "SELECT Users.name FROM Users WHERE Users.id=?";
    sqlite3_stmt* stmt = NULL;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, session->getId());
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        obj["user_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    } else {
        std::cout << "error in searching username\n";
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    for (auto sess : symbols_[std::to_string(chatId)]->sessions_) {
        sess->send(ss);
    }
    spsc_queue_subscriber_.push(std::make_tuple(parser::MsgType::DeleteUserFromChat, chatId, session->getId(), "", 0, std::nullopt));
}

void shared_state::deleteUserAccount(websocket_session* session)
{
    boost::json::object obj;
    obj["topic"] = 8;
    obj["user_id"] = session->getId();

    sqlite3_stmt* stmt = nullptr;
    int rc;

        rc = sqlite3_exec(session->db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to begin transaction: " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Database transaction error";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        return;
    }

        std::string sql = "DELETE FROM Friends WHERE user_id = ? OR friend_id = ?";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (delete friends): " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete friends";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    sqlite3_bind_int(stmt, 2, session->getId());
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Error deleting friends: " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete friends";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);

        sql = "DELETE FROM FriendRequests WHERE requester_id = ? OR requested_id = ?";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (delete friend requests): " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete friend requests";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    sqlite3_bind_int(stmt, 2, session->getId());
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Error deleting friend requests: " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete friend requests";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);

        sql = "DELETE FROM UserInChat WHERE userid = ?";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (delete user in chat): " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete user from chats";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Error deleting user from chats: " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete user from chats";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);

        sql = "DELETE FROM Message WHERE userid = ?";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (delete messages): " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete messages";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Error deleting messages: " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete messages";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);

        sql = "DELETE FROM Users WHERE id = ?";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (delete user): " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete user account";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Error deleting user: " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Failed to delete user account";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_finalize(stmt);

        rc = sqlite3_exec(session->db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Failed to commit transaction: " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Database transaction error";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_exec(session->db, "ROLLBACK;", nullptr, nullptr, nullptr);
        return;
    }

    obj["status"] = "success";
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));

        std::lock_guard<std::mutex> lock(mutex_);
    for (auto sess : sessions_) {
        sess->send(ss);
    }

        leave(session);

    spsc_queue_subscriber_.push(std::make_tuple(parser::MsgType::DeleteUserAccount, 0, session->getId(), "", 0, std::nullopt));
}

void shared_state::getUserList(websocket_session* session)
{
    std::string sql = "SELECT Users.id, Users.name FROM Users";
    sqlite3_stmt* stmt = NULL;
    boost::json::object obj;
    boost::json::array arr;
    obj["topic"] = 1;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    while (sqlite3_step(stmt) != SQLITE_DONE) {
        boost::json::object ob;
        ob["user_id"] = sqlite3_column_int(stmt, 0);
        ob["user_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        arr.emplace_back(ob);
    }
    obj["users"] = arr;
    sqlite3_finalize(stmt);
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    session->send(ss);
}

void shared_state::newUser(std::string name, int id)
{
    boost::json::object obj;
    obj["topic"] = 0;
    obj["user_id"] = id;
    obj["user_name"] = name;
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    for (auto a : sessions_) {
        a->send(ss);
    }
}

void shared_state::deleteFriend(websocket_session* session, int friendId)
{
    boost::json::object obj;
    obj["topic"] = 18;     sqlite3_stmt* stmt = nullptr;

        std::string sql = "DELETE FROM Friends WHERE (user_id=? AND friend_id=?) OR (user_id=? AND friend_id=?)";
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (Friends): " << sqlite3_errmsg(session->db) << "\n";
        obj["status"] = "error";
        obj["error"] = "Ошибка подготовки запроса: " + std::string(sqlite3_errmsg(session->db));
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    sqlite3_bind_int(stmt, 2, friendId);
    sqlite3_bind_int(stmt, 3, friendId);
    sqlite3_bind_int(stmt, 4, session->getId());
    rc = sqlite3_step(stmt);
    int changes = sqlite3_changes(session->db);
    sqlite3_finalize(stmt);

        sql = "DELETE FROM FriendRequests WHERE (requester_id=? AND requested_id=?) OR (requester_id=? AND requested_id=?)";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (FriendRequests): " << sqlite3_errmsg(session->db) << "\n";
        obj["status"] = "error";
        obj["error"] = "Ошибка подготовки запроса: " + std::string(sqlite3_errmsg(session->db));
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    sqlite3_bind_int(stmt, 2, friendId);
    sqlite3_bind_int(stmt, 3, friendId);
    sqlite3_bind_int(stmt, 4, session->getId());
    rc = sqlite3_step(stmt);
    changes += sqlite3_changes(session->db);     sqlite3_finalize(stmt);

    if (changes > 0) {
        std::cout << "Friend deleted successfully: user_id=" << session->getId() << ", friend_id=" << friendId << "\n";
        obj["status"] = "success";
                boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
        session->send(ss);
        auto it = sess___.find(std::to_string(friendId));
        if (it != sess___.end() && it->second) {
            it->second->send(ss);
        }
    } else {
        std::cerr << "Failed to delete friend: user_id=" << session->getId() << ", friend_id=" << friendId
                  << ", error=" << sqlite3_errmsg(session->db) << "\n";
        obj["status"] = "error";
        obj["error"] = "Не удалось удалить друга: записи не найдены";
    }
    session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
}

void shared_state::logout(websocket_session* session)
{
    boost::json::object obj;
    obj["topic"] = 22;     obj["status"] = "success";
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    session->send(ss);
    leave(session); }

void shared_state::inviteToChat(websocket_session* session, int chatId, std::vector<int> userId, int parentUser) {
        std::string sql = "SELECT name, isVoiceChat FROM Chat WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << sqlite3_errmsg(session->db) << std::endl;
        boost::json::object error;
        error["topic"] = 10;
        error["error"] = "Database error";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    sqlite3_bind_int(stmt, 1, chatId);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        std::cout << "Chat with id " << chatId << " not found\n";
        sqlite3_finalize(stmt);
        boost::json::object error;
        error["topic"] = 10;
        error["error"] = "Chat not found";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    std::string chatName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    bool isVoiceChat = sqlite3_column_int(stmt, 1) == 1;
    sqlite3_finalize(stmt);

    std::vector<int> validUsers;
    for (int id : userId) {
                sql = "SELECT 1 FROM Users WHERE id = ?";
        rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, id);
        bool userExists = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);
        if (!userExists) {
            std::cout << "User with id " << id << " not found\n";
            continue;
        }

                sql = "SELECT 1 FROM UserInChat WHERE chatid = ? AND userid = ?";
        rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, chatId);
        sqlite3_bind_int(stmt, 2, id);
        bool alreadyInChat = (sqlite3_step(stmt) == SQLITE_ROW);
        sqlite3_finalize(stmt);

        if (!alreadyInChat && id != session->getId()) {
            validUsers.push_back(id);
        }
    }

    if (validUsers.empty()) {
        boost::json::object response;
        response["topic"] = 10;
        response["status"] = "success";
        response["message"] = "No new users to invite";
        session->send(boost::make_shared<std::string>(boost::json::serialize(response)));
        return;
    }

        sql = "INSERT OR IGNORE INTO UserInChat (chatid, userid, parentuser, isvoicechat) VALUES (?, ?, ?, ?)";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << sqlite3_errmsg(session->db) << std::endl;
        boost::json::object error;
        error["topic"] = 10;
        error["error"] = "Database error";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    for (int id : validUsers) {
        sqlite3_bind_int(stmt, 1, chatId);
        sqlite3_bind_int(stmt, 2, id);
        sqlite3_bind_int(stmt, 3, parentUser);
        sqlite3_bind_int(stmt, 4, isVoiceChat ? 1 : 0);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            std::cerr << "Failed to insert user " << id << " into chat\n";
        }
        sqlite3_reset(stmt);
    }
    sqlite3_finalize(stmt);

        boost::json::object obj;
    obj["topic"] = 10;
    obj["chat_id"] = chatId;
    obj["chat_name"] = chatName;
    obj["user_id"] = parentUser;
    obj["isVoiceChat"] = isVoiceChat;
    boost::json::array invited;
    for (int id : validUsers) {
        invited.emplace_back(id);
    }
    obj["invited"] = invited;

        boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto id : validUsers) {
        auto it = sess___.find(std::to_string(id));
        if (it != sess___.end() && it->second) {
            it->second->send(ss);
        }
    }
    auto it = sess___.find(std::to_string(session->getId()));
    if (it != sess___.end() && it->second) {
        it->second->send(ss);
    }

    spsc_queue_subscriber_.push(std::make_tuple(parser::MsgType::InviteToChat, chatId, parentUser, "", 0,
        std::make_optional<std::vector<int>>(validUsers)));
}

void shared_state::getUserInChatList(websocket_session* session)
{
    std::string sql = "SELECT Users.id, Users.name FROM Users, UserInChat WHERE Users.id=UserInChat.userid AND UserInChat.chatid=?";
    sqlite3_stmt* stmt = NULL;
    boost::json::object obj;
    boost::json::array arr;
    obj["topic"] = 11;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    if (session->topics.empty()) { return; }
    sqlite3_bind_int(stmt, 1, stoi(*session->topics.begin()));
    while (sqlite3_step(stmt) != SQLITE_DONE) {
        boost::json::object ob;
        ob["user_id"] = sqlite3_column_int(stmt, 0);
        ob["user_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        arr.emplace_back(ob);
    }
    obj["users"] = arr;
    sqlite3_finalize(stmt);
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    session->send(ss);
}

void shared_state::getChatList(websocket_session* session)
{
    std::string sql = "SELECT Chat.id as chatId, Chat.name as chatName, Chat.isVoiceChat "
                     "FROM Chat JOIN UserInChat ON Chat.id = UserInChat.chatid "
                     "WHERE UserInChat.userid = ?";
    sqlite3_stmt* stmt = nullptr;
    boost::json::object obj;
    boost::json::array arr;
    obj["topic"] = 2;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (getChatList): " << sqlite3_errmsg(session->db) << std::endl;
        boost::json::object error;
        error["topic"] = 2;
        error["error"] = "Failed to fetch chat list";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    sqlite3_bind_int(stmt, 1, session->getId());
    while (sqlite3_step(stmt) != SQLITE_DONE) {
        boost::json::object ob;
        ob["chat_id"] = sqlite3_column_int(stmt, 0);
        ob["chat_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        ob["isVoiceChat"] = sqlite3_column_int(stmt, 2) == 1;
        arr.emplace_back(ob);
    }
    obj["chats"] = arr;
    sqlite3_finalize(stmt);
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    session->send(ss);
    std::cout << "Sent chat list to user ID: " << session->getId() << ", chats count: " << arr.size() << std::endl;
}

void shared_state::createChat(websocket_session* session, std::string chatName, std::vector<std::string> invited, bool isVoiceChat)
{
    std::string sql = "INSERT INTO Chat(name, adminid, isVoiceChat) VALUES(?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (create chat): " << sqlite3_errmsg(session->db) << std::endl;
        boost::json::object error;
        error["topic"] = 4;
        error["error"] = "Failed to prepare create chat query";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    sqlite3_bind_text(stmt, 1, chatName.c_str(), -1, nullptr);
    sqlite3_bind_int(stmt, 2, session->getId());
    sqlite3_bind_int(stmt, 3, isVoiceChat ? 1 : 0);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Error creating chat: " << sqlite3_errmsg(session->db) << std::endl;
        sqlite3_finalize(stmt);
        boost::json::object error;
        error["topic"] = 4;
        error["error"] = "Failed to create chat";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    int chatId = sqlite3_last_insert_rowid(session->db);
    sqlite3_finalize(stmt);
    std::cout << "Created chat ID: " << chatId << ", name: " << chatName << ", isVoiceChat: " << isVoiceChat << std::endl;

        sql = "INSERT INTO UserInChat(chatid, userid, parentuser, isvoicechat) VALUES(?,?,?,?)";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (add creator): " << sqlite3_errmsg(session->db) << std::endl;
        boost::json::object error;
        error["topic"] = 4;
        error["error"] = "Failed to add creator to chat";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    sqlite3_bind_int(stmt, 1, chatId);
    sqlite3_bind_int(stmt, 2, session->getId());
    sqlite3_bind_int(stmt, 3, session->getId());
    sqlite3_bind_int(stmt, 4, isVoiceChat ? 1 : 0);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Error adding creator to chat: " << sqlite3_errmsg(session->db) << std::endl;
        sqlite3_finalize(stmt);
        boost::json::object error;
        error["topic"] = 4;
        error["error"] = "Failed to add creator to chat";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    sqlite3_finalize(stmt);
    std::cout << "Added creator (user_id: " << session->getId() << ") to chat ID: " << chatId << std::endl;

        if (!invited.empty()) {
        std::vector<int> validUsers;
        for (const auto& user : invited) {
            try {
                int userId = std::stoi(user);
                sql = "SELECT 1 FROM Users WHERE id = ?";
                rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
                sqlite3_bind_int(stmt, 1, userId);
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    validUsers.push_back(userId);
                } else {
                    std::cout << "User ID " << userId << " not found" << std::endl;
                }
                sqlite3_finalize(stmt);
            } catch (const std::exception& e) {
                std::cerr << "Invalid user ID: " << user << std::endl;
            }
        }

        if (!validUsers.empty()) {
            sql = "INSERT OR IGNORE INTO UserInChat(chatid, userid, parentuser, isvoicechat) VALUES(?,?,?,?)";
            rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                std::cerr << "SQL prepare error (add invited): " << sqlite3_errmsg(session->db) << std::endl;
                return;
            }
            for (int id : validUsers) {
                sqlite3_bind_int(stmt, 1, chatId);
                sqlite3_bind_int(stmt, 2, id);
                sqlite3_bind_int(stmt, 3, session->getId());
                sqlite3_bind_int(stmt, 4, isVoiceChat ? 1 : 0);
                if (sqlite3_step(stmt) != SQLITE_DONE) {
                    std::cerr << "Failed to add user " << id << " to chat: " << sqlite3_errmsg(session->db) << std::endl;
                }
                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
            std::cout << "Added " << validUsers.size() << " invited users to chat ID: " << chatId << std::endl;
        }
    }

        boost::json::object ms;
    ms["topic"] = 4;
    ms["chat_name"] = chatName;
    ms["chat_id"] = chatId;
    ms["isVoiceChat"] = isVoiceChat;
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(ms));

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sess___.find(std::to_string(session->getId()));
    if (it != sess___.end() && it->second) {
        it->second->send(ss);
        std::cout << "Sent CreateChat notification to user ID: " << session->getId() << std::endl;
    }

    symbols_[std::to_string(chatId)] = boost::make_shared<symbol>(
        std::to_string(chatId), "",
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

void shared_state::getMessageList(websocket_session* session) {
    std::string sql = "SELECT u.name,m.text,m.date,m.id,m.userid FROM Message m, UserInChat c ,Users u WHERE m.chatid=? AND u.id=m.userid AND c.userid = u.id AND c.chatid=m.chatid ORDER BY(date)";
    sqlite3_stmt* stmt = NULL;
    boost::json::object obj;
    boost::json::array arr;
    obj["topic"] = 6;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    if (session->topics.empty()) { return; }
    sqlite3_bind_int(stmt, 1, stoi(*session->topics.begin()));
    while (sqlite3_step(stmt) != SQLITE_DONE) {
        boost::json::object ob;
        ob["user_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        ob["text"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        ob["date"] = sqlite3_column_int64(stmt, 2);
        ob["msg_id"] = sqlite3_column_int(stmt, 3);
        ob["user_id"] = sqlite3_column_int(stmt, 4);
        arr.emplace_back(ob);
    }
    obj["messages"] = arr;
    sqlite3_finalize(stmt);
    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    session->send(ss);
}

void shared_state::sendMsg(websocket_session* session, std::string message) {
    auto const ss = boost::make_shared<std::string const>(std::move(message));
    if (session->topics.empty()) {
        boost::json::object error;
        error["topic"] = 3;
        error["error"] = "Нет подписки на чат";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }
    std::string chatId = *session->topics.begin();
    int userId = session->getId();

        std::string checkSql = "SELECT 1 FROM Chat WHERE id = ? AND EXISTS "
                          "(SELECT 1 FROM UserInChat WHERE chatid = ? AND userid = ?)";
    sqlite3_stmt* checkStmt = nullptr;
    int rc = sqlite3_prepare_v2(session->db, checkSql.c_str(), -1, &checkStmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Ошибка подготовки SQL (проверка чата): " << sqlite3_errmsg(session->db) << "\n";
        boost::json::object error;
        error["topic"] = 3;
        error["error"] = "Ошибка базы данных";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(checkStmt);
        return;
    }
    sqlite3_bind_int(checkStmt, 1, std::stoi(chatId));
    sqlite3_bind_int(checkStmt, 2, std::stoi(chatId));
    sqlite3_bind_int(checkStmt, 3, userId);
    if (sqlite3_step(checkStmt) != SQLITE_ROW) {
        std::cerr << "Недействительный chatId " << chatId << " или пользователь " << userId << " не в чате\n";
        boost::json::object error;
        error["topic"] = 3;
        error["error"] = "Недействительный чат или пользователь не в чате";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(checkStmt);
        return;
    }
    sqlite3_finalize(checkStmt);

    const auto p1 = std::chrono::system_clock::now();
    int64_t date = std::chrono::duration_cast<std::chrono::milliseconds>(
        p1.time_since_epoch()).count();


        int msg_id = 0;
    std::string sql = "INSERT INTO Message(text, date, chatid, userid) VALUES(?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "Ошибка подготовки SQL: " << sqlite3_errmsg(session->db) << "\n";
        boost::json::object error;
        error["topic"] = 3;
        error["error"] = "Не удалось подготовить вставку сообщения";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(stmt);
        return;
    }
    sqlite3_bind_text(stmt, 1, ss->c_str(), ss->length(), nullptr);
    sqlite3_bind_int64(stmt, 2, date);
    sqlite3_bind_int(stmt, 3, std::stoi(chatId));
    sqlite3_bind_int(stmt, 4, userId);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        std::cerr << "Ошибка вставки сообщения в БД: " << sqlite3_errmsg(session->db) << "\n";
        boost::json::object error;
        error["topic"] = 3;
        error["error"] = sqlite3_errmsg(session->db);
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(stmt);
        return;
    }
    msg_id = sqlite3_last_insert_rowid(session->db);
    sqlite3_finalize(stmt);

        boost::json::object obj;
    obj["topic"] = 3;
    obj["text"] = *ss;
    obj["msg_id"] = msg_id;
    std::string sqlUser = "SELECT name FROM Users WHERE id=?";
    rc = sqlite3_prepare_v2(session->db, sqlUser.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        obj["user_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    } else {
        obj["user_name"] = "Неизвестный";
    }
    obj["date"] = date;
    sqlite3_finalize(stmt);

        std::vector<boost::weak_ptr<websocket_session>> v;
    {
        std::lock_guard<std::mutex> lock_symbol(symbols_[chatId]->mutex_);
        v.reserve(symbols_[chatId]->sessions_.size());
        for (auto p : symbols_[chatId]->sessions_) {
            v.emplace_back(p->weak_from_this());
        }
    }
    for (auto const& wp : v) {
        if (auto sp = wp.lock()) {
            sp->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        }
    }

    spsc_queue_subscriber_.push(std::make_tuple(parser::MsgType::MESSAGE, std::stoi(chatId), userId, *ss, date, std::nullopt));
}

void shared_state::leave(websocket_session* session)
{
    websocket_unsubscribe_to_symbols(session);
    std::lock_guard<std::mutex> lock(mutex_);
    sess___.erase(std::to_string(session->getId()));
    sessions_.erase(session);
}

void shared_state::parse(std::string msg, websocket_session* session)
{
    try {
        auto value = parser_.parse(msg);
        if (!value.is_object()) {
            boost::json::object error;
            error["topic"] = 0;
            error["error"] = "Сообщение не является JSON-объектом";
            session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
            return;
        }
        auto obj = value.as_object();
        if (!obj.contains("ty")) {
            boost::json::object error;
            error["topic"] = 0;
            error["error"] = "Отсутствует ключ 'ty'";
            session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
            return;
        }
        auto type = static_cast<parser::MsgType>(boost::json::value_to<int>(obj.at("ty")));
        switch (type) {
        case parser::MsgType::SUBSCRIBE:
        {
            std::string chatId = std::to_string(boost::json::value_to<int>(obj.at("to")));
            websocket_subscribe_to_symbols(session, chatId);
            break;
        }
        case parser::MsgType::UNSUBSCRIBE:
        {
            websocket_unsubscribe_to_symbols(session);
            break;
        }
        case parser::MsgType::MESSAGE:
        {
            std::string msg = boost::json::value_to<std::string>(obj.at("msg"));
            sendMsg(session, msg);
            break;
        }
        case parser::MsgType::CreateChat:
        {
            std::string chatName = boost::json::value_to<std::string>(obj.at("chatName"));
            std::vector<std::string> invited;
            boost::json::array arr = obj.at("Invited").as_array();
            for (auto& val : arr) {
                if (val.is_int64()) {
                    invited.push_back(std::to_string(boost::json::value_to<int>(val)));
                } else if (val.is_string()) {
                    invited.push_back(boost::json::value_to<std::string>(val));
                }
            }
            bool isVoiceChat = obj.contains("isVoiceChat") && boost::json::value_to<bool>(obj.at("isVoiceChat"));
            createChat(session, chatName, invited, isVoiceChat);
            break;
        }
        case parser::MsgType::DeleteFriend:
            deleteFriend(session, boost::json::value_to<int>(obj.at("friend_id")));
            break;
        case parser::MsgType::Logout:
            logout(session);
            break;
        case parser::MsgType::GetChatList:
        {
            getChatList(session);
            break;
        }
        case parser::MsgType::GetMessageList:
        {
            getMessageList(session);
            break;
        }
        case parser::MsgType::DeleteUserFromChat:
            deleteUserFromChat(session, boost::json::value_to<int>(obj.at("chatId")));
            break;
        case parser::MsgType::DeleteUserAccount:
        {
            deleteUserAccount(session);
            break;
        }
        case parser::MsgType::UpdateAccount:
        {
            int userId = boost::json::value_to<int>(obj.at("user_id"));
            std::string newName = obj.contains("name") ? boost::json::value_to<std::string>(obj.at("name")) : "";
            std::string newPassword = obj.contains("password") ? boost::json::value_to<std::string>(obj.at("password")) : "";
            updateAccount(session, userId, newName, newPassword);
            break;
        }
        case parser::MsgType::GetUserList:
        {
            getUserList(session);
            break;
        }
        case parser::MsgType::InviteToChat:
        {
            if (session->topics.empty()) {
                boost::json::object error;
                error["topic"] = 10;
                error["error"] = "Чат не выбран";
                session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
                break;
            }
            std::vector<int> invited;
            boost::json::array arr = obj.at("Invited").as_array();
            for (auto& val : arr) {
                invited.push_back(boost::json::value_to<int>(val));
            }
            inviteToChat(session, std::stoi(*session->topics.begin()), invited, session->getId());
            break;
        }
        case parser::MsgType::GetUserInChatList:
        {
            getUserInChatList(session);
            break;
        }
        case parser::MsgType::SearchUsersByName:
        {
            std::string searchTerm = boost::json::value_to<std::string>(obj.at("searchTerm"));
            searchUsersByName(session, searchTerm);
            break;
        }
        case parser::MsgType::AddFriend:
        {
            int userId = boost::json::value_to<int>(obj.at("user_id"));
            int friendId = boost::json::value_to<int>(obj.at("friend_id"));
            addFriend(session, userId, friendId);

                        boost::json::object notify;
            notify["topic"] = 17;             notify["friend_id"] = userId;
            std::string sql = "SELECT name FROM Users WHERE id=?";
            sqlite3_stmt* stmt;
            int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, userId);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                notify["friend_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
            }
            sqlite3_finalize(stmt);

            auto it = sess___.find(std::to_string(friendId));
            if (it != sess___.end() && it->second) {
                it->second->send(boost::make_shared<std::string>(boost::json::serialize(notify)));
            }
            break;
        }
        case parser::MsgType::GetFriendsList:
        {
            int userId = boost::json::value_to<int>(obj.at("user_id"));
            getFriendsList(session, userId);
            break;
        }
        case parser::MsgType::AcceptFriendRequest:
            acceptFriendRequest(session, boost::json::value_to<int>(obj.at("user_id")), boost::json::value_to<int>(obj.at("friend_id")));
            break;
        case parser::MsgType::RejectFriendRequest:
        {
            std::string sql = "DELETE FROM FriendRequests WHERE requester_id = ? AND requested_id = ? AND status = 'pending'";
            sqlite3_stmt* stmt;
            int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
            sqlite3_bind_int(stmt, 1, boost::json::value_to<int>(obj.at("friend_id")));
            sqlite3_bind_int(stmt, 2, boost::json::value_to<int>(obj.at("user_id")));
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            break;
        }
        case parser::MsgType::DeleteVoiceChat:
            deleteVoiceChat(session, boost::json::value_to<int>(obj.at("chat_id")));
            break;
        default:
            std::cout << "(Неизвестный тип сообщения: " << static_cast<int>(type) << ")\n";
            break;
        }
    } catch (const boost::system::system_error& e) {
        std::cerr << "Ошибка парсинга JSON: " << e.what() << std::endl;
        boost::json::object error;
        error["topic"] = 0;
        error["error"] = "Некорректное JSON-сообщение";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
    }
}

void shared_state::addFriend(websocket_session* session, int userId, int friendId) {
    if (userId == friendId) {
        boost::json::object error;
        error["topic"] = 13;
        error["error"] = "Нельзя добавить себя в друзья";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }

        std::string checkUserSql = "SELECT 1 FROM Users WHERE id = ?";
    sqlite3_stmt* checkStmt;
    int rc = sqlite3_prepare_v2(session->db, checkUserSql.c_str(), -1, &checkStmt, nullptr);
    if (rc != SQLITE_OK) {
        boost::json::object error;
        error["topic"] = 13;
        error["error"] = "Ошибка базы данных";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(checkStmt);
        return;
    }
    sqlite3_bind_int(checkStmt, 1, userId);
    if (sqlite3_step(checkStmt) != SQLITE_ROW) {
        boost::json::object error;
        error["topic"] = 13;
        error["error"] = "Пользователь не существует";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(checkStmt);
        return;
    }
    sqlite3_reset(checkStmt);
    sqlite3_bind_int(checkStmt, 1, friendId);
    if (sqlite3_step(checkStmt) != SQLITE_ROW) {
        boost::json::object error;
        error["topic"] = 13;
        error["error"] = "Пользователь для добавления не существует";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        sqlite3_finalize(checkStmt);
        return;
    }
    sqlite3_finalize(checkStmt);

        std::string checkSql = "SELECT 1 FROM FriendRequests WHERE requester_id = ? AND requested_id = ? AND status = 'pending'";
    rc = sqlite3_prepare_v2(session->db, checkSql.c_str(), -1, &checkStmt, nullptr);
    sqlite3_bind_int(checkStmt, 1, userId);
    sqlite3_bind_int(checkStmt, 2, friendId);
    bool requestExists = (sqlite3_step(checkStmt) == SQLITE_ROW);
    sqlite3_finalize(checkStmt);

    if (requestExists) {
        boost::json::object response;
        response["topic"] = 13;
        response["status"] = "request_sent";
        response["friend_id"] = friendId;
        session->send(boost::make_shared<std::string>(boost::json::serialize(response)));
        return;
    }

        std::string friendCheckSql = "SELECT 1 FROM Friends WHERE user_id = ? AND friend_id = ?";
    rc = sqlite3_prepare_v2(session->db, friendCheckSql.c_str(), -1, &checkStmt, nullptr);
    sqlite3_bind_int(checkStmt, 1, userId);
    sqlite3_bind_int(checkStmt, 2, friendId);
    bool areFriends = (sqlite3_step(checkStmt) == SQLITE_ROW);
    sqlite3_finalize(checkStmt);

    if (areFriends) {
        boost::json::object error;
        error["topic"] = 13;
        error["error"] = "Пользователь уже является вашим другом";
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
        return;
    }

        std::string sql = "INSERT INTO FriendRequests (requester_id, requested_id, status) VALUES (?, ?, 'pending')";
    sqlite3_stmt* stmt;
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_int(stmt, 2, friendId);
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        boost::json::object response;
        response["topic"] = 13;
        response["status"] = "request_sent";
        response["friend_id"] = friendId;
        session->send(boost::make_shared<std::string>(boost::json::serialize(response)));

                boost::json::object notify;
        notify["topic"] = 17;         notify["friend_id"] = userId;
        std::string nameSql = "SELECT name FROM Users WHERE id=?";
        rc = sqlite3_prepare_v2(session->db, nameSql.c_str(), -1, &checkStmt, nullptr);
        sqlite3_bind_int(checkStmt, 1, userId);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            notify["friend_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(checkStmt, 0)));
        }
        sqlite3_finalize(checkStmt);

        auto it = sess___.find(std::to_string(friendId));
        if (it != sess___.end() && it->second) {
            it->second->send(boost::make_shared<std::string>(boost::json::serialize(notify)));
        }
    } else {
        std::cerr << "Ошибка вставки запроса на дружбу: " << sqlite3_errmsg(session->db) << "\n";
        boost::json::object error;
        error["topic"] = 13;
        error["error"] = "Не удалось отправить запрос на дружбу: " + std::string(sqlite3_errmsg(session->db));
        session->send(boost::make_shared<std::string>(boost::json::serialize(error)));
    }
    sqlite3_finalize(stmt);
}

void shared_state::updateAccount(websocket_session* session, int userId, const std::string& newName, const std::string& newPassword)
{
    boost::json::object obj;
    obj["topic"] = 20;

    std::string sql;
    sqlite3_stmt* stmt = nullptr;
    int rc;

    if (!newName.empty() && !newPassword.empty()) {
        sql = "UPDATE Users SET name = ?, password = ? WHERE id = ?";
        rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, newName.c_str(), -1, nullptr);
        sqlite3_bind_text(stmt, 2, newPassword.c_str(), -1, nullptr);
        sqlite3_bind_int(stmt, 3, userId);
    } else if (!newName.empty()) {
        sql = "UPDATE Users SET name = ? WHERE id = ?";
        rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, newName.c_str(), -1, nullptr);
        sqlite3_bind_int(stmt, 2, userId);
    } else if (!newPassword.empty()) {
        sql = "UPDATE Users SET password = ? WHERE id = ?";
        rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, newPassword.c_str(), -1, nullptr);
        sqlite3_bind_int(stmt, 2, userId);
    } else {
        obj["status"] = "error";
        obj["error"] = "No fields to update";
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        return;
    }

    if (rc != SQLITE_OK) {
        std::cerr << "SQL prepare error (update account): " << sqlite3_errmsg(session->db) << std::endl;
        obj["status"] = "error";
        obj["error"] = "Database error";
        sqlite3_finalize(stmt);
        session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
        return;
    }

    if (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(session->db) > 0) {
        obj["status"] = "success";
        if (!newName.empty()) {
            obj["name"] = newName;
        }
    } else {
        obj["status"] = "error";
        obj["error"] = "Failed to update account";
    }
    sqlite3_finalize(stmt);

    session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
}

void shared_state::acceptFriendRequest(websocket_session* session, int userId, int friendId) {
    std::string sql = "UPDATE FriendRequests SET status = 'accepted' WHERE requester_id = ? AND requested_id = ? AND status = 'pending'";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, friendId);
    sqlite3_bind_int(stmt, 2, userId);
    if (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(session->db) > 0) {
        sql = "INSERT OR IGNORE INTO Friends (user_id, friend_id) VALUES (?, ?), (?, ?)";
        sqlite3_finalize(stmt);
        rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
        sqlite3_bind_int(stmt, 1, userId);
        sqlite3_bind_int(stmt, 2, friendId);
        sqlite3_bind_int(stmt, 3, friendId);
        sqlite3_bind_int(stmt, 4, userId);
        sqlite3_step(stmt);
                boost::json::object response;
        response["topic"] = 15;         response["status"] = "accepted";
        response["friend_id"] = friendId;
        session->send(boost::make_shared<std::string>(boost::json::serialize(response)));
                auto it = sess___.find(std::to_string(friendId));
        if (it != sess___.end() && it->second) {
            it->second->send(boost::make_shared<std::string>(boost::json::serialize(response)));
        }
    }
    sqlite3_finalize(stmt);
}

void shared_state::deleteVoiceChat(websocket_session* session, int chatId)
{
    std::string sql = "SELECT isVoiceChat, adminid FROM Chat WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    boost::json::object obj;
    obj["topic"] = 21;     int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, chatId);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        bool isVoiceChat = sqlite3_column_int(stmt, 0) == 1;
        int adminId = sqlite3_column_int(stmt, 1);
        if (!isVoiceChat || adminId != session->getId()) {
            obj["status"] = "error";
            obj["error"] = isVoiceChat ? "Только администратор может удалить чат" : "Это не голосовой чат";
            sqlite3_finalize(stmt);
            boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
            session->send(ss);
            return;
        }
    } else {
        obj["status"] = "error";
        obj["error"] = "Чат не найден";
        sqlite3_finalize(stmt);
        boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
        session->send(ss);
        return;
    }
    sqlite3_finalize(stmt);

    sql = "DELETE FROM Chat WHERE id=?";
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, chatId);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_DONE) {
        obj["status"] = "success";
        obj["chat_id"] = chatId;
        symbols_.erase(std::to_string(chatId));
    } else {
        obj["status"] = "error";
        obj["error"] = "Ошибка удаления чата";
    }
    sqlite3_finalize(stmt);

    boost::shared_ptr<std::string> ss = boost::make_shared<std::string>(boost::json::serialize(obj));
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto sess : sessions_) {
        sess->send(ss);
    }
}

void shared_state::getFriendsList(websocket_session* session, int userId) {
    std::string sql = "SELECT u.id, u.name FROM Friends f JOIN Users u ON f.friend_id = u.id WHERE f.user_id = ? "
                     "UNION SELECT requester_id, (SELECT name FROM Users WHERE id = requester_id) FROM FriendRequests "
                     "WHERE requested_id = ? AND status = 'accepted'";
    sqlite3_stmt* stmt = NULL;
    boost::json::object obj;
    boost::json::array arr;
    obj["topic"] = 14;
    int rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_int(stmt, 2, userId);
    while (sqlite3_step(stmt) != SQLITE_DONE) {
        boost::json::object friendObj;
        friendObj["friend_id"] = sqlite3_column_int(stmt, 0);
        friendObj["friend_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        arr.emplace_back(friendObj);
    }
    obj["friends"] = arr;
    sqlite3_finalize(stmt);

        sql = "SELECT requester_id, (SELECT name FROM Users WHERE id = requester_id) FROM FriendRequests "
          "WHERE requested_id = ? AND status = 'pending'";
    boost::json::array requests;
    rc = sqlite3_prepare_v2(session->db, sql.c_str(), -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, userId);
    while (sqlite3_step(stmt) != SQLITE_DONE) {
        boost::json::object requestObj;
        requestObj["friend_id"] = sqlite3_column_int(stmt, 0);
        requestObj["friend_name"] = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
        requests.emplace_back(requestObj);
    }
    obj["friend_requests"] = requests;
    sqlite3_finalize(stmt);

    session->send(boost::make_shared<std::string>(boost::json::serialize(obj)));
}