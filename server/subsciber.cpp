#include "subscriber.hpp"
#include "symbol.hpp"
#include <chrono>

subscriber::
subscriber(net::io_context& ioc_subscriber, boost::shared_ptr<shared_state> const& state)
    : ioc_subscriber_(ioc_subscriber)
    , state_(state)
{
    int res = sqlite3_open(state_->db_root().c_str(), &db);
    if (res != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }
    sqlite3_busy_timeout(db, 5000);
}

void
subscriber::
subscribe()
{
    boost::asio::post(net::make_strand(ioc_subscriber_),
        [=]()
        {
            std::tuple<parser::MsgType, uint32_t, uint32_t, std::string, int64_t, std::optional<std::vector<int>>> msg_tuple;
            while (true) {
                if (state_->spsc_queue_subscriber_.pop(msg_tuple))
                {
                    auto type = std::get<0>(msg_tuple);
                    auto chatId = std::get<1>(msg_tuple);
                    auto userId = std::get<2>(msg_tuple);
                    auto msg = std::get<3>(msg_tuple);
                    switch (type)
                    {
                    case parser::MsgType::MESSAGE:
                    {
                        std::cout << "Sending msg to db :" << msg << std::endl;
                        std::string sql = "INSERT INTO Message(text,date,chatid,userid) VALUES(?,?,?,?)";
                        sqlite3_stmt* stmt = NULL;
                        const auto p1 = std::chrono::system_clock::now();
                        int64_t date = std::get<4>(msg_tuple);
                        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        sqlite3_bind_text(stmt, 1, msg.c_str(), std::strlen(msg.c_str()), NULL);
                        sqlite3_bind_int64(stmt, 2, date);
                        sqlite3_bind_int(stmt, 3, chatId);
                        sqlite3_bind_int(stmt, 4, userId);
                        auto ans = sqlite3_step(stmt);
                        if (ans != SQLITE_DONE) {
                            std::cout << "error in inserting message: " << ans << "\n";
                        }
                        sqlite3_finalize(stmt);
                        break;
                    }
                    case parser::MsgType::DeleteUserFromChat:
                    {
                        std::string sql = "DELETE FROM UserInChat WHERE chatid=? AND userid=?";
                        sqlite3_stmt* stmt = NULL;
                        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        sqlite3_bind_int(stmt, 1, chatId);
                        sqlite3_bind_int(stmt, 2, userId);
                        if (sqlite3_step(stmt) != SQLITE_DONE) {
                            std::cout << "error in delete user from chat\n";
                        }
                        sql = "DELETE FROM Message WHERE Message.chatid=? AND Message.chatid NOT IN (SELECT uc.chatid FROM UserInChat uc)";
                        sqlite3_clear_bindings(stmt);
                        sqlite3_reset(stmt);
                        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        sqlite3_bind_int(stmt, 1, chatId);
                        if (sqlite3_step(stmt) == SQLITE_DONE) {
                            sql = "DELETE FROM Chat WHERE Chat.id=? AND Chat.id NOT IN (SELECT uc.chatid FROM UserInChat uc)";
                            sqlite3_reset(stmt);
                            rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                            sqlite3_bind_int(stmt, 1, chatId);
                            if (sqlite3_step(stmt) != SQLITE_DONE) {
                                std::cout << "error in deleting chat without users\n";
                            }
                            else {
                                sql = "SELECT userid,chatid FROM Chat,UserInChat where Chat.id=UserInChat.chatid AND Chat.adminid=? GROUP By (chatid)";
                                sqlite3_clear_bindings(stmt);
                                sqlite3_reset(stmt);
                                rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                                sqlite3_bind_int(stmt, 1, userId);
                                while (sqlite3_step(stmt) != SQLITE_DONE) {
                                    std::string sql2 = "UPDATE Chat SET adminid=? WHERE id=?";
                                    sqlite3_stmt* stmt2 = NULL;
                                    int rc = sqlite3_prepare_v2(db, sql2.c_str(), -1, &stmt2, NULL);
                                    sqlite3_bind_int(stmt2, 1, sqlite3_column_int(stmt, 0));
                                    sqlite3_bind_int(stmt2, 2, sqlite3_column_int(stmt, 1));
                                    if (sqlite3_step(stmt2) != SQLITE_DONE) {
                                        std::cout << "error in updating chat adminid\n";
                                    }
                                    sqlite3_finalize(stmt2);
                                }
                            }
                        }
                        sqlite3_finalize(stmt);
                        break;
                    }
                    case parser::MsgType::DeleteUserAccount:
                    {
                        std::string sql = "DELETE FROM UserInChat WHERE userid=?";
                        sqlite3_stmt* stmt = NULL;
                        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        sqlite3_bind_int(stmt, 1, userId);
                        if (sqlite3_step(stmt) != SQLITE_DONE) {
                            std::cout << "error in delete user from chat\n";
                            break;
                        }
                        sql = "DELETE FROM Message WHERE userid=?";
                        sqlite3_reset(stmt);
                        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        sqlite3_bind_int(stmt, 1, userId);
                        if (sqlite3_step(stmt) != SQLITE_DONE) {
                            std::cout << "error in delete message\n";
                            break;
                        }
                        sql = "DELETE FROM Chat WHERE Chat.id NOT IN (SELECT uc.chatid FROM UserInChat uc)";
                        sqlite3_reset(stmt);
                        sqlite3_clear_bindings(stmt);
                        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        if (sqlite3_step(stmt) != SQLITE_DONE) {
                            std::cout << "error in deleting chat without users\n";
                        }
                        sql = "SELECT userid,chatid FROM Chat,UserInChat where Chat.id=UserInChat.chatid AND Chat.adminid=? GROUP By (chatid)";
                        sqlite3_clear_bindings(stmt);
                        sqlite3_reset(stmt);
                        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        sqlite3_bind_int(stmt, 1, userId);
                        while (sqlite3_step(stmt) != SQLITE_DONE) {
                            std::string sql2 = "UPDATE Chat SET adminid=? WHERE id=?";
                            sqlite3_stmt* stmt2 = NULL;
                            rc = sqlite3_prepare_v2(db, sql2.c_str(), -1, &stmt2, NULL);
                            sqlite3_bind_int(stmt2, 1, sqlite3_column_int(stmt, 0));
                            sqlite3_bind_int(stmt2, 2, sqlite3_column_int(stmt, 1));
                            if (sqlite3_step(stmt2) != SQLITE_DONE) {
                                std::cout << "error in updating chat adminid\n";
                            }
                            sqlite3_finalize(stmt2);
                        }
                        sql = "UPDATE UserInChat SET parentUser=(SELECT adminid FROM Chat WHERE Chat.id=UserInChat.chatid)";
                        sqlite3_reset(stmt);
                        sqlite3_clear_bindings(stmt);
                        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        if (sqlite3_step(stmt) != SQLITE_DONE) {
                            std::cout << "error in updating userinchat parentuser\n";
                        }
                        sql = "DELETE FROM Users WHERE id=?";
                        sqlite3_reset(stmt);
                        rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                        sqlite3_bind_int(stmt, 1, userId);
                        if (sqlite3_step(stmt) != SQLITE_DONE) {
                            std::cout << "error in deleting user\n";
                        }
                        sqlite3_finalize(stmt);
                        break;
                    }
                    case parser::MsgType::InviteToChat:
                    {
                        std::string sql = "INSERT INTO UserInChat(chatid, userid, parentUser) VALUES(?,?,?)";
                        sqlite3_stmt* stmt = NULL;
                        std::vector<int> invited = std::get<5>(msg_tuple).value();
                        for (int user : invited) {
                            sql = "SELECT id FROM Users WHERE id=?";
                            int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                            sqlite3_bind_int(stmt, 1, user);
                            if (sqlite3_step(stmt) != SQLITE_ROW) {
                                std::cout << "User with id " << user << " not found, skipping\n";
                                sqlite3_finalize(stmt);
                                continue;
                            }
                            sqlite3_finalize(stmt);

                            sql = "INSERT INTO UserInChat(chatid, userid, parentUser) VALUES(?,?,?)";
                            rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
                            sqlite3_bind_int(stmt, 1, chatId);
                            sqlite3_bind_int(stmt, 2, user);
                            sqlite3_bind_int(stmt, 3, userId);
                            if (sqlite3_step(stmt) != SQLITE_DONE) {
                                std::cout << "error in inviting user " << user << " to chat " << chatId << "\n";
                            }
                            sqlite3_reset(stmt);
                            sqlite3_clear_bindings(stmt);
                        }
                        sqlite3_finalize(stmt);
                        break;
                    }
                    default:
                        std::cout << "Unsupported message type\n";
                        break;
                    }
                }
            }
        });
}