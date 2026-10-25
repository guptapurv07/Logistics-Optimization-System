#include "../include/api.h"
#include <cmath>
#include <cstdlib>

std::string trim(const std::string& text) {
    std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string checkPhone(const std::string& phone, const char* label) {
    if (phone.empty()) return phone;
    int digits = 0;
    for (std::size_t i = 0; i < phone.size(); ++i) {
        char c = phone[i];
        if (c >= '0' && c <= '9') ++digits;
        else if (c != ' ' && c != '-' && !(c == '+' && i == 0)) throw ApiError(400, std::string(label) + " can only contain digits, spaces and dashes");
    }
    if (digits < 10 || digits > 13) throw ApiError(400, std::string(label) + " must have 10 to 13 digits");
    return phone;
}

Fields::Fields(const nlohmann::json& body) : body_(body) {
    if (!body_.is_object()) throw ApiError(400, "The request must be a JSON object");
}

std::string Fields::text(const char* key, const char* label, std::size_t minLength, std::size_t maxLength) const {
    std::string value = optionalText(key, label, maxLength);
    if (value.size() < minLength) {
        throw ApiError(400, std::string(label) + (minLength <= 1 ? " is required"
                                                                  : " must be at least " + std::to_string(minLength) + " characters"));
    }
    return value;
}

std::string Fields::optionalText(const char* key, const char* label, std::size_t maxLength) const {
    if (!body_.contains(key) || body_.at(key).is_null()) return "";
    if (!body_.at(key).is_string()) throw ApiError(400, std::string(label) + " must be text");
    std::string value = trim(body_.at(key).get<std::string>());
    if (value.size() > maxLength) {
        throw ApiError(400, std::string(label) + " can be at most " + std::to_string(maxLength) + " characters");
    }
    return value;
}

double Fields::number(const char* key, const char* label) const {
    const std::string error = std::string(label) + " must be a number";
    if (!body_.contains(key)) throw ApiError(400, std::string(label) + " is required");
    const nlohmann::json& value = body_.at(key);
    double number;
    if (value.is_number()) {
        number = value.get<double>();
    } else if (value.is_string()) {
        // Form inputs often arrive as text; accept "12.5" but not "12abc"
        std::string text = trim(value.get<std::string>());
        char* end = nullptr;
        number = std::strtod(text.c_str(), &end);
        if (text.empty() || *end != '\0') throw ApiError(400, error);
    } else {
        throw ApiError(400, error);
    }
    if (!std::isfinite(number)) throw ApiError(400, error);
    return number;
}

std::int64_t Fields::id(const char* key, const char* label) const {
    if (!body_.contains(key)) throw ApiError(400, std::string(label) + " is required");
    const nlohmann::json& value = body_.at(key);
    if (value.is_number_integer() && value.get<std::int64_t>() > 0) return value.get<std::int64_t>();
    // <select> values arrive as text, so accept "12" as well
    if (value.is_string()) {
        std::string text = trim(value.get<std::string>());
        if (!text.empty() && text.size() <= 18 && text.find_first_not_of("0123456789") == std::string::npos) {
            std::int64_t number = std::stoll(text);
            if (number > 0) return number;
        }
    }
    throw ApiError(400, std::string(label) + " is not valid");
}

bool Fields::flag(const char* key, const char* label) const {
    if (!body_.contains(key) || !body_.at(key).is_boolean()) throw ApiError(400, std::string(label) + " must be true or false");
    return body_.at(key).get<bool>();
}
