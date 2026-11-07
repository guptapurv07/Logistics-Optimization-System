#include "../include/settings.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {

std::string trimmed(const std::string& text) {
    std::size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

bool startsWith(const std::string& text, const std::string& start) {
    return text.compare(0, start.size(), start) == 0;
}

bool printableAscii(const std::string& text) {
    for (unsigned char c : text) {
        if (c < 0x20 || c > 0x7E) return false;
    }
    return true;
}

// Gmail shows an app password in four groups ("abcd efgh ijkl mnop"); the spaces are not part of it
std::string withoutAppPasswordSpaces(const std::string& password) {
    if (password.size() != 19) return password;
    std::string joined;
    for (std::size_t i = 0; i < password.size(); ++i) {
        bool gap = i % 5 == 4;
        if (gap != (password[i] == ' ')) return password;
        if (!gap) {
            if (!std::islower(static_cast<unsigned char>(password[i]))) return password;
            joined += password[i];
        }
    }
    return joined;
}

}  // namespace

bool isLocalUrl(const std::string& url) {
    std::size_t start = url.find("://");
    if (start == std::string::npos) return false;
    std::string rest = url.substr(start + 3);
    std::string host = startsWith(rest, "[") ? rest.substr(0, rest.find(']') + 1) : rest.substr(0, rest.find_first_of(":/"));
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

Settings loadSettings(const std::string& path) {
    Settings settings;
    std::ifstream file(path);
    if (!file) return settings;  // no .env: the defaults

    std::string line;
    for (int number = 1; std::getline(file, line); ++number) {
        const std::string where = path + " line " + std::to_string(number) + ": ";
        std::string text = trimmed(line);
        if (text.empty() || text[0] == '#') continue;
        if (startsWith(text, "export ")) text = trimmed(text.substr(7));
        std::size_t equals = text.find('=');
        if (equals == std::string::npos) throw std::runtime_error(where + "expected KEY=value");
        std::string key = trimmed(text.substr(0, equals));
        std::string value = trimmed(text.substr(equals + 1));
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }

        if (key == "PORT") {
            bool digits = !value.empty() && value.size() <= 5 && value.find_first_not_of("0123456789") == std::string::npos;
            int port = digits ? std::stoi(value) : 0;
            if (port < 1 || port > 65535) throw std::runtime_error(where + "PORT must be a number from 1 to 65535");
            settings.port = port;
        } else if (key == "GOOGLE_CLIENT_ID") {
            settings.googleClientId = value;
        } else if (key == "SMTP_URL") {
            settings.smtpUrl = value;
        } else if (key == "SMTP_USER") {
            settings.smtpUser = value;
        } else if (key == "SMTP_PASSWORD") {
            settings.smtpPassword = withoutAppPasswordSpaces(value);
        } else if (key == "MAIL_FROM") {
            settings.mailFrom = value;
        } else if (key == "GOOGLE_CHECK_URL") {
            settings.googleCheckUrl = value;
        } else {
            std::cerr << "[SETTINGS] " << where << "unknown setting " << key << " (ignored)" << std::endl;
        }
    }

    // Values that can't work stop the server now, with the reason, instead of failing at the first login
    if (settings.googleOn()) {
        if (!printableAscii(settings.googleClientId) || settings.googleClientId.find_first_of(" \"'<>") != std::string::npos) {
            throw std::runtime_error(path + ": GOOGLE_CLIENT_ID has characters a client ID can't have");
        }
        if (settings.googleClientId.size() < 27 ||
            settings.googleClientId.compare(settings.googleClientId.size() - 27, 27, ".apps.googleusercontent.com") != 0) {
            std::cerr << "[SETTINGS] " << path << ": GOOGLE_CLIENT_ID usually ends with .apps.googleusercontent.com" << std::endl;
        }
    }
    if (settings.emailOn()) {
        if (!startsWith(settings.smtpUrl, "smtps://") && !startsWith(settings.smtpUrl, "smtp://")) {
            throw std::runtime_error(path + ": SMTP_URL must start with smtps:// or smtp://, e.g. smtps://smtp.gmail.com:465");
        }
        if (settings.mailFrom.empty()) settings.mailFrom = settings.smtpUser;
        if (settings.mailFrom.find('@') == std::string::npos || !printableAscii(settings.mailFrom)) {
            throw std::runtime_error(path + ": set MAIL_FROM (or SMTP_USER) to the email address the codes come from");
        }
        if (!settings.smtpUser.empty() && settings.smtpPassword.empty()) {
            throw std::runtime_error(path + ": SMTP_USER is set but SMTP_PASSWORD is empty");
        }
    }
    if (!startsWith(settings.googleCheckUrl, "https://") && !(startsWith(settings.googleCheckUrl, "http://") && isLocalUrl(settings.googleCheckUrl))) {
        throw std::runtime_error(path + ": GOOGLE_CHECK_URL must be an https:// address");
    }
    return settings;
}
