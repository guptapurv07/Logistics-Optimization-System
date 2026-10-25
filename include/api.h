#ifndef API_H
#define API_H

#include <cstdint>
#include <stdexcept>
#include <string>
#include "db.h"
#include "json.hpp"

// --- API ERRORS ---
// A problem the user can fix (a missing field, a wrong password, a full truck...). The server sends it back
// as {"error": "..."} with this HTTP status, and the page shows the message as it is.
class ApiError : public std::runtime_error {
public:
    ApiError(int status, const std::string& message) : std::runtime_error(message), status_(status) {}
    int status() const { return status_; }

private:
    int status_;
};

// --- REQUEST FIELDS ---
// Reads one JSON request body. Each getter checks type and size and throws a 400 ApiError naming the field,
// so a bad value never gets further than this.
class Fields {
public:
    explicit Fields(const nlohmann::json& body);

    // Text with surrounding spaces removed; `label` is how the field is named in error messages
    std::string text(const char* key, const char* label, std::size_t minLength, std::size_t maxLength) const;
    std::string optionalText(const char* key, const char* label, std::size_t maxLength) const;
    double number(const char* key, const char* label) const;  // a JSON number or numeric text such as "12.5"
    std::int64_t id(const char* key, const char* label) const;  // a whole number from 1 up
    bool flag(const char* key, const char* label) const;

private:
    const nlohmann::json& body_;
};

std::string trim(const std::string& text);

// An optional phone number: "" stays "", otherwise 10-13 digits with optional spaces, dashes and a leading +
std::string checkPhone(const std::string& phone, const char* label);

// A text column that may be NULL, as JSON null or text
inline nlohmann::json textOrNull(const Statement& row, int col) {
    return row.columnIsNull(col) ? nlohmann::json() : nlohmann::json(row.columnText(col));
}

#endif
