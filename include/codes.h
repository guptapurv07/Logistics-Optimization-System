#ifndef CODES_H
#define CODES_H

#include <cstdint>
#include <string>
#include "db.h"
#include "json.hpp"
#include "settings.h"

// --- EMAIL CODES ---
// A 6-digit code is emailed to finish every login with a password, and to confirm the address before a new
// account is made. The page gets a random ticket that goes with the code; the database keeps only SHA-256
// hashes of both, so reading the database file doesn't reveal a code that still works.
constexpr int kCodeMinutes = 10;    // how long a code works
constexpr int kCodeTries = 5;       // wrong codes before the ticket is cancelled
constexpr int kResendSeconds = 30;  // wait before another code can be sent
constexpr int kCodeSends = 3;       // codes per ticket, counting the first
constexpr int kOpenTickets = 3;     // tickets out at once for one account (or one address, for sign-ups)

// What a right code confirmed: a login (userId) or a sign-up (the checked form, see checkSignup)
struct ConfirmedCode {
    std::string purpose;    // "login" or "signup"
    std::int64_t userId = 0;
    nlohmann::json signup;
};

// Each makes a ticket and sends its code. They return what the page needs:
// {"ticket", "purpose", "email", "delivery", "resend_after", "minutes"}, where "delivery" is "email", "console"
// (no mail server set up: the code is printed in the server window) or "demo" (an example.com address, which
// can't receive mail: the answer then also carries the "code")
nlohmann::json sendLoginCode(Database& db, const Settings& settings, std::int64_t userId,
                             const std::string& email, const std::string& name);
nlohmann::json sendSignupCode(Database& db, const Settings& settings, const nlohmann::json& signup);
nlohmann::json resendCode(Database& db, const Settings& settings, const std::string& ticket);

// Checks a typed code. A right code uses the ticket up; so do too many wrong ones. Throws ApiError otherwise.
ConfirmedCode confirmCode(Database& db, const std::string& ticket, const std::string& code);

#endif
