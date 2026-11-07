#include "../include/google.h"
#include "../include/api.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iostream>
#include <memory>

namespace {

std::size_t appendBody(char* data, std::size_t size, std::size_t count, void* userData) {
    std::string& body = *static_cast<std::string*>(userData);
    std::size_t length = size * count;
    if (body.size() + length > 64 * 1024) return 0;  // Google's answer is about 1 KB; anything this big is not it
    body.append(data, length);
    return length;
}

// Fetches an https:// address; returns the HTTP status and fills `body`. Throws std::runtime_error when the
// server can't be reached at all.
long fetch(const std::string& url, std::string& body) {
    CURL* curl = curl_easy_init();
    if (!curl) throw std::runtime_error("libcurl couldn't start");
    std::unique_ptr<CURL, void (*)(CURL*)> cleanup(curl, curl_easy_cleanup);
    char error[CURL_ERROR_SIZE] = "";
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);   // timeouts without signals, which aren't safe with several threads
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) throw std::runtime_error(error[0] ? error : curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    return status;
}

// tokeninfo sends every value as text ("email_verified": "true", "exp": "1762500000")
std::string field(const nlohmann::json& info, const char* key) {
    auto value = info.find(key);
    if (value == info.end()) return "";
    if (value->is_string()) return value->get<std::string>();
    if (value->is_boolean()) return value->get<bool>() ? "true" : "false";
    if (value->is_number_integer()) return std::to_string(value->get<long long>());
    return "";
}

}  // namespace

GoogleAccount checkGoogleToken(const Settings& settings, const std::string& idToken) {
    if (!settings.googleOn()) throw ApiError(503, "Google sign-in isn't switched on for this server");
    // An ID token is three base64url parts joined by dots, about 1 KB in all
    static const std::string kTokenCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.";
    if (idToken.size() < 20 || idToken.size() > 4096 || idToken.find_first_not_of(kTokenCharacters) != std::string::npos ||
        std::count(idToken.begin(), idToken.end(), '.') != 2) {
        throw ApiError(400, "That Google sign-in isn't valid. Please try again.");
    }

    std::string body;
    long status = 0;
    try {
        status = fetch(settings.googleCheckUrl + "?id_token=" + idToken, body);
    } catch (const std::exception& e) {
        std::cerr << "[GOOGLE] Couldn't reach " << settings.googleCheckUrl << ": " << e.what() << std::endl;
        throw ApiError(502, "Couldn't reach Google to check the sign-in. Check the internet connection and try again.");
    }
    // Google answers 400 for a token that is forged, damaged or out of date
    if (status != 200) throw ApiError(401, "Google didn't accept this sign-in (it may have run out). Please try again.");

    nlohmann::json info = nlohmann::json::parse(body, nullptr, false);
    if (!info.is_object()) throw ApiError(502, "Google sent an answer this server doesn't understand. Please try again.");
    // Made for this site? A token that some other website got from Google must not log in here.
    if (field(info, "aud") != settings.googleClientId) throw ApiError(401, "This Google sign-in was made for a different website");
    const std::string issuer = field(info, "iss");
    if (issuer != "accounts.google.com" && issuer != "https://accounts.google.com") throw ApiError(401, "This sign-in didn't come from Google");
    const std::string expires = field(info, "exp");
    if (expires.empty() || expires.size() > 12 || expires.find_first_not_of("0123456789") != std::string::npos ||
        std::stoll(expires) <= static_cast<long long>(std::time(nullptr))) {
        throw ApiError(401, "This Google sign-in has run out. Please try again.");
    }
    if (field(info, "email_verified") != "true") throw ApiError(403, "Google hasn't confirmed this account's email address yet");

    GoogleAccount account;
    account.id = field(info, "sub");
    account.email = field(info, "email");
    std::transform(account.email.begin(), account.email.end(), account.email.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    account.name = trim(field(info, "name"));
    if (account.id.empty() || account.email.find('@') == std::string::npos) {
        throw ApiError(502, "Google didn't say which account signed in. Please try again.");
    }
    if (account.name.empty()) account.name = account.email.substr(0, account.email.find('@'));
    return account;
}
