#ifndef EMAIL_H
#define EMAIL_H

#include <stdexcept>
#include <string>
#include "settings.h"

// --- EMAIL ---
// Sends mail through the SMTP server named in the settings, using libcurl. The connection is always encrypted
// (smtps://, or smtp:// upgraded with STARTTLS), except to a test mail server on this computer.
class EmailError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One message with a plain-text and an HTML version; throws EmailError if the mail server doesn't take it
void sendEmail(const Settings& settings, const std::string& to, const std::string& subject,
               const std::string& text, const std::string& html);

// example.com, example.net and example.org (and anything under .example) are reserved for examples (RFC 2606)
// and can never receive mail. The demo accounts use them.
bool isExampleAddress(const std::string& email);

// "ap•••@gmail.com": enough for people to recognise their own address, not enough to learn someone else's
std::string maskEmail(const std::string& email);

#endif
