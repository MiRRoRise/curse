//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/vinniefalco/CppCon2018
//

#include "http_session.hpp"
#include "websocket_session.hpp"
#include <boost/config.hpp>
#include <iostream>
#include "util.hpp"


//------------------------------------------------------------------------------

// Return a reasonable mime type based on the extension of a file.
beast::string_view
mime_type(beast::string_view path)
{
    using beast::iequals;
    auto const ext = [&path]
        {
            auto const pos = path.rfind(".");
            if (pos == beast::string_view::npos)
                return beast::string_view{};
            return path.substr(pos);
        }();
        if (iequals(ext, ".htm"))  return "text/html";
        if (iequals(ext, ".html")) return "text/html";
        if (iequals(ext, ".php"))  return "text/html";
        if (iequals(ext, ".css"))  return "text/css";
        if (iequals(ext, ".txt"))  return "text/plain";
        if (iequals(ext, ".js"))   return "application/javascript";
        if (iequals(ext, ".json")) return "application/json";
        if (iequals(ext, ".xml"))  return "application/xml";
        if (iequals(ext, ".swf"))  return "application/x-shockwave-flash";
        if (iequals(ext, ".flv"))  return "video/x-flv";
        if (iequals(ext, ".png"))  return "image/png";
        if (iequals(ext, ".jpe"))  return "image/jpeg";
        if (iequals(ext, ".jpeg")) return "image/jpeg";
        if (iequals(ext, ".jpg"))  return "image/jpeg";
        if (iequals(ext, ".gif"))  return "image/gif";
        if (iequals(ext, ".bmp"))  return "image/bmp";
        if (iequals(ext, ".ico"))  return "image/vnd.microsoft.icon";
        if (iequals(ext, ".tiff")) return "image/tiff";
        if (iequals(ext, ".tif"))  return "image/tiff";
        if (iequals(ext, ".svg"))  return "image/svg+xml";
        if (iequals(ext, ".svgz")) return "image/svg+xml";
        return "application/text";
}

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
    beast::string_view base,
    beast::string_view path)
{
    if (base.empty())
        return std::string(path);
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if (result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for (auto& c : result)
        if (c == '/')
            c = path_separator;
#else
    char constexpr path_separator = '/';
    if (result.back() == path_separator)
        result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif
    return result;
}



//template <class Body, class Allocator>
//boost::optional<http::message_generator>
//validate_jwt_token(http::request<Body, http::basic_fields<Allocator>>& req)
//{
//    // Returns a bad request response
//    auto const unauthorized =
//        [&req](beast::string_view why)
//        {
//            http::response<http::string_body> res{ http::status::bad_request, req.version() };
//            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//            res.set(http::field::content_type, "text/html");
//            res.keep_alive(req.keep_alive());
//            res.body() = std::string(why);
//            res.prepare_payload();
//            return res;
//        };
//
//    boost::urls::url_view uv(req.base().target());
//
//    for (auto v : uv.params()) {
//        if (v.key == "api_token") {
//            try {
//                auto dec_obj = jwt::decode(v.value, jwt::params::algorithms({ "HS256" }), jwt::params::secret("secret"));
//                // std::cout << "User Authorized " << dec_obj.payload() << std::endl;
//                return boost::none;
//            }
//            catch (const jwt::TokenExpiredError& e) {
//                return boost::make_optional<http::message_generator>(unauthorized("Unauthorized - api_token expired"));
//            }
//            catch (const jwt::SignatureFormatError& e) {
//                return boost::make_optional<http::message_generator>(unauthorized("Unauthorized - api_token signature format error"));
//            }
//            catch (const jwt::DecodeError& e) {
//                return boost::make_optional<http::message_generator>(unauthorized("Unauthorized - api_token decode error"));
//            }
//            catch (const jwt::VerificationError& e) {
//                return boost::make_optional<http::message_generator>(unauthorized("Unauthorized - api_token verification error"));
//            }
//            catch (...) {
//                return boost::make_optional<http::message_generator>(unauthorized("Unauthorized - api_token unknown error"));
//            }
//        }
//    }
//
//    return boost::make_optional<http::message_generator>(unauthorized("Unauthorized - api_token missing"));
//
//    //TODO: Implement HTTP header authorization
//
//    // std::string authorizationHeader;
//
//    // std::cout << "\nDebug: ALL HTTP Request Header Values:" << "\n\n";
//
//    // for (auto& h : parser_->get().base())
//    // {
//    //     auto headerValue = std::string(h.value());
//    //     std::cout << "Header: " << h.name() << ", Header Name: "
//    //               << h.name_string() << ", Value: " << headerValue << "\n\n";
//
//    //     if (h.name() == http::field::authorization)
//    //     {
//    //         authorizationHeader = headerValue;
//    //         std::cout << "\nDebug: Detected HTTP Request Authorization Header." << "\n\n";
//    //     }
//    // }
//}

template <class Body, class Allocator>
boost::optional<int>
validate_auth(http::request<Body, http::basic_fields<Allocator>>& req,sqlite3* db)
{
    // Returns a bad request response
    auto const unauthorized =
        [&req](beast::string_view why)
        {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
        };

    boost::urls::url_view uv(req.base().target());
   std::optional< std::string> login=std::nullopt, password=std::nullopt;
    for (auto v : uv.params()) {
        if (v.key == "login") {
            login.emplace(v.value);
        }
        else if (v.key == "password") {
            password.emplace(v.value);
        }
    }
    if (login.has_value() && password.has_value()) {
        //AUTH
        std::string sql = "SELECT id FROM Users WHERE login=? AND pass=?";
        sqlite3_stmt* stmt = NULL;
        std::hash<std::string> str_hash;
        std::string hashed_pass = SHA256HashString(password.value());
        int rc=sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
        std::cout << login.value().c_str() << " log , pass " << password.value().c_str()<<std::endl;
        sqlite3_bind_text(stmt, 1, login.value().c_str(), std::strlen(login.value().c_str()), NULL);
        sqlite3_bind_text(stmt, 2, hashed_pass.c_str(), std::strlen(hashed_pass.c_str()), NULL);
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            int ans = sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
            boost::optional<int> r = boost::make_optional<int&>(ans);
            return r;

        }
        else {
            sqlite3_finalize(stmt);
            return boost::none;
        }
    }
    else {
        return boost::none;
    }
}
template <class Body, class Allocator>
boost::optional<std::pair<int,std::string>>
register_user(http::request<Body, http::basic_fields<Allocator>>& req,sqlite3* db)
{
    // Returns a bad request response
    auto const unauthorized =
        [&req](beast::string_view why)
        {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
        };

    boost::urls::url_view uv(req.base().target());
    std::optional< std::string> login = std::nullopt, password = std::nullopt,name=std::nullopt;
    for (auto v : uv.params()) {
        if (v.key == "login_reg") {
            login.emplace(v.value);
        }
        else if (v.key == "password") {
            password.emplace(v.value);
        }
        else if (v.key == "name") {
            name.emplace(v.value);
        }
    }
    if (login.has_value() && password.has_value()&&name.has_value()) {
        //AUTH
        std::string sql = "INSERT INTO Users(login,pass,name) VALUES(?,?,?)";
        sqlite3_stmt* stmt = NULL;
        std::hash<std::string> str_hash;
        std::string hashed_pass = SHA256HashString(password.value());
        
        int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, login.value().c_str(), std::strlen(login.value().c_str()), NULL);
        sqlite3_bind_text(stmt, 2, (hashed_pass).c_str(), std::strlen(hashed_pass.c_str()), NULL);
        sqlite3_bind_text(stmt, 3, name.value().c_str(), std::strlen(name.value().c_str()), NULL);
        if (sqlite3_step(stmt)== SQLITE_DONE) {
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            sql = "SELECT id FROM Users WHERE login=? AND pass=? AND name=?";
            int rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
            sqlite3_bind_text(stmt, 1, login.value().c_str(), std::strlen(login.value().c_str()), NULL);
            sqlite3_bind_text(stmt, 2, (hashed_pass).c_str(), std::strlen(hashed_pass.c_str()), NULL);
            sqlite3_bind_text(stmt, 3, name.value().c_str(), std::strlen(name.value().c_str()), NULL);
            auto a = sqlite3_step(stmt);
            if (a != SQLITE_DONE) {
                std::pair<int, std::string> ans = { sqlite3_column_int(stmt, 0), 
                name.value()};
                sqlite3_finalize(stmt);
                return boost::make_optional<std::pair<int, std::string>&>(ans);
            }
            else {
                sqlite3_finalize(stmt);
                return boost::none;
            }
            
            
            
        }
        else {
            sqlite3_finalize(stmt);
            return boost::none;
        }
    }
    else {
        return boost::none;
    }
}


// Return a response for the given request.
//
// The concrete type of the response message (which depends on the
// request), is type-erased in message_generator.
template <class Body, class Allocator>
http::message_generator
handle_request(
    beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
    // Returns a bad request response
    auto const bad_request =
        [&req](beast::string_view why)
        {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
        };

    // Returns a not found response
    auto const not_found =
        [&req](beast::string_view target)
        {
            http::response<http::string_body> res{ http::status::not_found, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "The resource '" + std::string(target) + "' was not found.";
            res.prepare_payload();
            return res;
        };

    // Returns a server error response
    auto const server_error =
        [&req](beast::string_view what)
        {
            http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "An error occurred: '" + std::string(what) + "'";
            res.prepare_payload();
            return res;
        };

    // Make sure we can handle the method
    if (req.method() != http::verb::get &&
        req.method() != http::verb::head&&req.method()!=http::verb::post)
        return bad_request("Unknown HTTP-method");

    // Request path must be absolute and not contain "..".
    if (req.target().empty() ||
        req.target()[0] != '/' ||
        req.target().find("..") != beast::string_view::npos)
        return bad_request("Illegal request-target");

    // Build the path to the requested file
    std::string path = path_cat(doc_root, req.target());
    if (req.target().back() == '/')
        path.append("index.html");

    // Attempt to open the file
    beast::error_code ec;
    http::file_body::value_type body;
    body.open(path.c_str(), beast::file_mode::scan, ec);

    // Handle the case where the file doesn't exist
    if (ec == boost::system::errc::no_such_file_or_directory)
        return not_found(req.target());

    // Handle an unknown error
    if (ec)
        return server_error(ec.message());

    // Cache the size since we need it after the move
    auto const size = body.size();

    // Respond to HEAD request
    if (req.method() == http::verb::head)
    {
        http::response<http::empty_body> res{ http::status::ok, req.version() };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return res;
    }

    // Respond to GET request
    http::response<http::file_body> res{
        std::piecewise_construct,
        std::make_tuple(std::move(body)),
        std::make_tuple(http::status::ok, req.version()) };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, mime_type(path));
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return res;
}

//------------------------------------------------------------------------------


http_session::
http_session(
    tcp::socket&& socket,
    boost::shared_ptr<shared_state> const& state)
    : stream_(std::move(socket))
    , state_(state)
{
    int res = sqlite3_open(state_->db_root().c_str(), &db);
    if (res != SQLITE_OK) {
        sqlite3_close(db);
        fail(boost::asio::error::fault, "unable to connect with db");
        
    }
    sqlite3_busy_timeout(db, 5000);
}
http_session::
~http_session()  
{
    
    sqlite3_close(db);
}
void
http_session::
run()
{
    do_read();
}

// Report a failure
void
http_session::
fail(beast::error_code ec, char const* what)
{
    // Don't report on canceled operations
    if (ec == net::error::operation_aborted)
        return;

    std::cerr << what << ": " << ec.message() << "\n";
}

void
http_session::
do_read()
{
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    parser_->body_limit(10000);

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request
    http::async_read(
        stream_,
        buffer_,
        parser_->get(),
        beast::bind_front_handler(
            &http_session::on_read,
            shared_from_this()));
}

void
http_session::
on_read(beast::error_code ec, std::size_t)
{
    // This means they closed the connection
    if (ec == http::error::end_of_stream)
    {
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    // Handle the error, if any
    if (ec)
        return fail(ec, "read");


    bool keep_alive;
    auto self = shared_from_this();

    //boost::optional<http::message_generator> unauthorized_res = validate_jwt_token(parser_->release());
    auto req = parser_->get();
    // See if it is a WebSocket Upgrade
    if (websocket::is_upgrade(req))
    {
        
        auto const unauthorized =
            [&req](beast::string_view why)
            {
                http::response<http::string_body> res{ http::status::bad_request, req.version() };
                res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
                res.set(http::field::content_type, "text/html");
                res.keep_alive(req.keep_alive());
                res.body() = std::string(why);
                res.prepare_payload();
                return res;
            };
        boost::urls::url_view uv(req.base().target());
        boost::optional<int> id = boost::none;
        std::optional< std::string> login = std::nullopt, login_reg = std::nullopt, password = std::nullopt,name=std::nullopt;
        for (auto v : uv.params()) {
            if (v.key == "login_reg") {
                login_reg.emplace(v.value);
            }
            else if (v.key == "login") {
                login.emplace(v.value);
            }
            else if (v.key == "password") {
                password.emplace(v.value);
            }
            else if (v.key == "name") {
                name.emplace(v.value);
            }
        }
        if (login_reg.has_value() && password.has_value()&&name.has_value()) {
             boost::optional<std::pair<int,std::string>> ans = register_user(parser_->get(),db);
            if (!ans.has_value()) {
                std::cout << "Sending unauthorized response\n";
                keep_alive = parser_->get().keep_alive();
                auto rs = boost::make_optional<http::message_generator>(unauthorized("That user already exists"));
                beast::async_write(
                    stream_, std::move(*rs),
                    [self, keep_alive](beast::error_code ec, std::size_t bytes)
                    {
                        self->on_write(ec, bytes, keep_alive);
                    });
                return;
            }
            else {
                id.emplace(std::get<0>(ans.value()));
                state_->newUser(std::get<1>(ans.value()), std::get<0>(ans.value()));
            }
        }
        else if (login.has_value() && password.has_value()) {
             id = validate_auth(parser_->get(),db);
            if (!id.has_value()) {
                std::cout << "Sending unauthorized response\n";
                keep_alive = parser_->get().keep_alive();
                auto rs = boost::make_optional<http::message_generator>(unauthorized("Incorrect login or password"));
                beast::async_write(
                    stream_, std::move(*rs),
                    [self, keep_alive](beast::error_code ec, std::size_t bytes)
                    {
                        self->on_write(ec, bytes, keep_alive);
                    });
                return;
            }
        }
        else {
            
            auto rs = boost::make_optional<http::message_generator>(unauthorized("Incorrect data"));
            std::cout << "Sending unauthorized response\n";
            keep_alive = parser_->get().keep_alive();
            // Send the response
            beast::async_write(
                stream_, std::move(*rs),
                [self, keep_alive](beast::error_code ec, std::size_t bytes)
                {
                    self->on_write(ec, bytes, keep_alive);
                });
            return;
        }
        // Create a websocket session, transferring ownership
        // of both the socket and the HTTP request.
        boost::make_shared<websocket_session>(
            stream_.release_socket(),
            state_,id.value())->run(parser_->release());
        return;
    }

    // Handle request
    http::message_generator msg =
        handle_request(state_->doc_root(), parser_->release());

    // Determine if we should close the connection
    keep_alive = msg.keep_alive();

    // Send the response
    beast::async_write(
        stream_, std::move(msg),
        [self, keep_alive](beast::error_code ec, std::size_t bytes)
        {
            self->on_write(ec, bytes, keep_alive);
        });

}

void
http_session::
on_write(beast::error_code ec, std::size_t, bool keep_alive)
{
    // Handle the error, if any
    if (ec)
        return fail(ec, "write");

    if (!keep_alive)
    {
        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
        return;
    }

    // Read another request
    do_read();
}