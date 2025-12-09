#pragma once

#include <string>
#include <map>
#include <initializer_list>
#include <sstream>
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "core/logging.hpp"

using namespace std;

namespace HttpClient {
    struct Response {
        int status_code;
        string text;
        map<string, string> headers;
        string final_url;  // Added: track final URL after redirects
    };
    
    inline string build_kv_string(
        initializer_list<pair<const string, string>> items,
        char sep = '&'
    ) {
        ostringstream ss;
        bool first = true;
        for (auto &kv: items) {
            if (!first) ss << sep;
            first = false;
            ss << kv.first << '=' << kv.second;
        }
        return ss.str();
    }
    
    inline Response get(
        const std::string &url,
        std::initializer_list<std::pair<const std::string, std::string>> headers = {},
        cpr::Parameters params = {},
        bool follow_redirects = true,  // Added: option to follow redirects
        int max_redirects = 10  // Added: max redirect limit
    ) {
        auto r = cpr::Get(
            cpr::Url{url},
            cpr::Header{headers},
            params,
            cpr::Redirect(follow_redirects ? max_redirects : 0L)  // CPR redirect option
        );
        
        std::map<std::string, std::string> hdrs(r.header.begin(), r.header.end());
        
        // r.url contains the final URL after redirects
        return {
            static_cast<int>(r.status_code), 
            r.text, 
            hdrs,
            r.url.str()  // Final URL after following redirects
        };
    }
    
    inline Response post(
        const string &url,
        initializer_list<pair<const string, string>> headers = {},
        const string &jsonBody = string(),
        initializer_list<pair<const string, string>> form = {},
        bool follow_redirects = true,  // Added: option to follow redirects
        int max_redirects = 10  // Added: max redirect limit
    ) {
        cpr::Header h{headers};
        cpr::Response r;
        
        if (!jsonBody.empty()) {
            h["Content-Type"] = "application/json";
            r = cpr::Post(
                cpr::Url{url},
                h,
                cpr::Body{jsonBody},
                cpr::Redirect(follow_redirects ? max_redirects : 0L)
            );
        } else if (form.size() > 0) {
            string body = build_kv_string(form);
            h["Content-Type"] = "application/x-www-form-urlencoded";
            r = cpr::Post(
                cpr::Url{url},
                h,
                cpr::Body{body},
                cpr::Redirect(follow_redirects ? max_redirects : 0L)
            );
        } else {
            r = cpr::Post(
                cpr::Url{url},
                h,
                cpr::Redirect(follow_redirects ? max_redirects : 0L)
            );
        }
        
        map<string, string> hdrs(r.header.begin(), r.header.end());
        return {
            static_cast<int>(r.status_code), 
            r.text, 
            hdrs,
            r.url.str()
        };
    }
    
    inline nlohmann::json decode(const Response &response) {
        try {
            return nlohmann::json::parse(response.text);
        } catch (const nlohmann::json::exception &e) {
            LOG_ERROR(std::string("Failed to parse JSON response: ") + e.what());
            return nlohmann::json::object();
        }
    }
}
