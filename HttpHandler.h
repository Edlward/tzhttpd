/*-
 * Copyright (c) 2018 TAO Zhijiang<taozhijiang@gmail.com>
 *
 * Licensed under the BSD-3-Clause license, see LICENSE for full information.
 *
 */

#ifndef __TZHTTPD_HTTP_HANDLER_H__
#define __TZHTTPD_HTTP_HANDLER_H__

// 所有的http uri 路由

#include <libconfig.h++>

#include <vector>
#include <string>
#include <memory>

#include <boost/algorithm/string.hpp>
#include <boost/regex.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/shared_mutex.hpp>

#include "CgiHelper.h"
#include "CgiWrapper.h"
#include "SlibLoader.h"

namespace tzhttpd {

class HttpParser;

typedef std::function<int (const HttpParser& http_parser, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpGetHandler;
typedef std::function<int (const HttpParser& http_parser, const std::string& post_data, \
                           std::string& response, std::string& status_line, std::vector<std::string>& add_header)> HttpPostHandler;

template<typename T>
struct HttpHandlerObject {

    boost::atomic<bool>    built_in_;      // built_in handler，无需引用计数
    boost::atomic<int64_t> success_cnt_;
    boost::atomic<int64_t> fail_cnt_;

    boost::atomic<bool>    working_;       // 正在

    T handler_;

    explicit HttpHandlerObject(const T& t, bool built_in = false):
    built_in_(built_in), success_cnt_(0), fail_cnt_(0), working_(true),
        handler_(t) {
    }
};

typedef HttpHandlerObject<HttpGetHandler>  HttpGetHandlerObject;
typedef HttpHandlerObject<HttpPostHandler> HttpPostHandlerObject;

typedef std::shared_ptr<HttpGetHandlerObject>  HttpGetHandlerObjectPtr;
typedef std::shared_ptr<HttpPostHandlerObject> HttpPostHandlerObjectPtr;


class UriRegex: public boost::regex {
public:
    explicit UriRegex(const std::string& regexStr) :
        boost::regex(regexStr), str_(regexStr) {
    }

    std::string str() const {
        return str_;
    }

private:
    std::string str_;
};


class HttpHandler {

public:
    // check_exist
    bool check_exist_http_get_handler(const std::string& uri_r) {
        return do_check_exist_http_handler(uri_r, get_handler_);
    }

    bool check_exist_http_post_handler(const std::string& uri_r) {
        return do_check_exist_http_handler(uri_r, post_handler_);
    }

    // switch on/off
    int switch_http_get_handler(const std::string& uri_r, bool on) {
        return do_switch_http_handler(uri_r, on, get_handler_);
    }

    int switch_http_post_handler(const std::string& uri_r, bool on) {
        return do_switch_http_handler(uri_r, on, post_handler_);
    }

    // update_handler
    int update_http_get_handler(const std::string& uri_r, bool on) {
        return do_update_http_handler<HttpGetHandlerObjectPtr>(uri_r, on, get_handler_);
    }

    int update_http_post_handler(const std::string& uri_r, bool on) {
        return do_update_http_handler<HttpPostHandlerObjectPtr>(uri_r, on, post_handler_);
    }


    int register_http_get_handler(const std::string& uri_r, const HttpGetHandler& handler, bool built_in);
    int register_http_post_handler(const std::string& uri_r, const HttpPostHandler& handler, bool built_in);

    // uri match
    int find_http_get_handler(std::string uri, HttpGetHandlerObjectPtr& phandler_obj);
    int find_http_post_handler(std::string uri, HttpPostHandlerObjectPtr& phandler_obj);

    int update_run_cfg(const libconfig::Config& cfg);

    std::string pure_uri_path(std::string uri) {  // copy
        uri = boost::algorithm::trim_copy(boost::to_lower_copy(uri));
        while (uri[uri.size()-1] == '/' && uri.size() > 1)  // 全部的小写字母，去除尾部
            uri = uri.substr(0, uri.size()-1);

        return uri;
    }

private:
    template<typename T>
    bool do_check_exist_http_handler(const std::string& uri_r, const T& handlers);

    template<typename T>
    int do_switch_http_handler(const std::string& uri_r, bool on, T& handlers);

    template<typename Ptr, typename T>
    int do_update_http_handler(const std::string& uri_r, bool on, T& handlers);

private:

    int parse_cfg(const libconfig::Config& cfg, const std::string& key, std::map<std::string, std::string>& path_map);

    boost::shared_mutex rwlock_;

    // 使用vector保存handler，保证是先注册handler具有高优先级
    std::vector<std::pair<UriRegex, HttpPostHandlerObjectPtr>> post_handler_;
    std::vector<std::pair<UriRegex, HttpGetHandlerObjectPtr>>  get_handler_;

};


// template code should be .h

template<typename T>
bool HttpHandler::do_check_exist_http_handler(const std::string& uri_r, const T& handlers) {

    std::string uri = pure_uri_path(uri_r);
    boost::shared_lock<boost::shared_mutex> rlock(rwlock_);

    for (auto it = handlers.begin(); it != handlers.end(); ++ it) {
        if (it->first.str() == uri ) {
            return true;
        }
    }

    return false;
}


template<typename T>
int HttpHandler::do_switch_http_handler(const std::string& uri_r, bool on, T& handlers) {

    std::string uri = pure_uri_path(uri_r);
    boost::lock_guard<boost::shared_mutex> wlock(rwlock_);

    for (auto it = handlers.begin(); it != handlers.end(); ++ it) {
        if (it->first.str() == uri ) {
            if (it->second->working_ == on) {
                tzhttpd_log_err("uri handler for %s already in %s status...",
                                it->first.str().c_str(), on ? "on" : "off");
                return -1;
            } else {
                tzhttpd_log_alert("uri handler for %s update from %s to %s status...",
                                 it->first.str().c_str(),
                                 it->second->working_ ? "on" : "off", on ? "on" : "off");
                it->second->working_ = on;
                return 0;
            }
        }
    }

    tzhttpd_log_err("uri for %s not found, update status failed...!", uri.c_str());
    return -2;
}


template<typename Ptr, typename T>
int HttpHandler::do_update_http_handler(const std::string& uri_r, bool on, T& handlers) {

    Ptr p_handler_object{};
    std::string uri = pure_uri_path(uri_r);

    boost::lock_guard<boost::shared_mutex> wlock(rwlock_); // 持有互斥锁，不会再有新的请求了

    auto it = handlers.begin();
    for (auto it = handlers.begin(); it != handlers.end(); ++ it) {
        if (it->first.str() == uri ) {
            p_handler_object = it->second;
            break;
        }
    }

    if (p_handler_object->built_in_) {
        tzhttpd_log_err("handler for %s is built_in type, we do not consider support replacement.");
        return -1;
    }

    int retry_count = 10;
    if (p_handler_object) {

        while (p_handler_object.use_count() > 2 && -- retry_count > 0) {
            ::usleep(1000);
        }

        if (p_handler_object.use_count() > 2) {
            tzhttpd_log_err("handler for %s use_count: %ld, may disable it first and update...",
                            uri_r.c_str(), p_handler_object.use_count());
            goto ret;
        }


        // safe remove the handler and (may) unload dll

        SAFE_ASSERT(it < handlers.end());
        handlers.erase(it);

        // install new handler


    }
    // else, good, new handler

ret:
    return -2;
}


namespace http_handler {

int default_http_get_handler(const HttpParser& http_parser, std::string& response,
                             std::string& status_line, std::vector<std::string>& add_header);

extern std::shared_ptr<HttpGetHandlerObject> default_http_get_phandler_obj;


} // end namespace http_handler
} // end namespace tzhttpd


#endif //__TZHTTPD_HTTP_HANDLER_H__
