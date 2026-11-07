#include "../include/codes.h"
#include "../include/api.h"
#include "../include/email.h"
#include "../include/password.h"
#include <iostream>
#include <mutex>

using json = nlohmann::json;

namespace {

const char kStartAgain[] = "This code has run out or was already used. Please start again.";

// One whole line in the server window, even when several requests print at the same moment
void say(const std::string& line) {
    static std::mutex lock;
    std::lock_guard<std::mutex> guard(lock);
    std::cout << line << std::endl;
}

// The code is hashed together with its ticket, so the stored hash can't be matched against the million possible codes
std::string codeHash(const std::string& ticket, const std::string& code) {
    return sha256Hex(ticket + ":" + code);
}

bool isTicket(const std::string& ticket) {
    return ticket.size() == 64 && ticket.find_first_not_of("0123456789abcdef") == std::string::npos;
}

std::string htmlEscape(const std::string& text) {
    std::string out;
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

struct Message {
    std::string subject, text, html;
};

// The email: the code in the subject (so it shows in the notification), and a plain and an HTML body
Message codeMessage(const std::string& purpose, const std::string& fullName, const std::string& code) {
    const bool login = purpose == "login";
    const std::string firstName = fullName.substr(0, fullName.find(' '));
    const std::string action = login ? "Use this code to finish logging in to TruckSetu:"
                                     : "Use this code to confirm your email address and create your TruckSetu account:";
    const std::string after = "It works for " + std::to_string(kCodeMinutes) + " minutes. " +
                              (login ? "If this wasn't you, don't share the code with anyone: someone may know your password."
                                     : "If you didn't sign up for TruckSetu, you can ignore this email.");
    Message message;
    message.subject = code + (login ? " is your TruckSetu login code" : " is your TruckSetu confirmation code");
    message.text = "Hi " + firstName + ",\n\n" + action + "\n\n    " + code + "\n\n" + after + "\n\nTruckSetu\n";
    message.html =
        "<!DOCTYPE html>\n<html><body style=\"margin:0;padding:0;background:#F5F8FC;color:#222222;"
        "font-family:-apple-system,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;\">\n"
        "<div style=\"max-width:480px;margin:0 auto;padding:32px 20px;\">\n"
        "<div style=\"font-size:20px;font-weight:700;letter-spacing:-0.5px;color:#2F6DB0;\">TruckSetu</div>\n"
        "<div style=\"background:#FFFFFF;border:1px solid #E3E8EF;padding:28px 24px;margin-top:16px;\">\n"
        "<p style=\"margin:0 0 12px;font-size:16px;\">Hi " + htmlEscape(firstName) + ",</p>\n"
        "<p style=\"margin:0 0 20px;font-size:15px;line-height:1.5;\">" + htmlEscape(action) + "</p>\n"
        "<div style=\"font-size:32px;font-weight:700;letter-spacing:8px;text-align:center;padding:14px 0;background:#F1F4F8;"
        "font-family:Menlo,Consolas,'Courier New',monospace;\">" + code + "</div>\n"
        "<p style=\"margin:20px 0 0;font-size:13px;line-height:1.5;color:#666666;\">" + htmlEscape(after) + "</p>\n"
        "</div>\n"
        "<p style=\"margin:16px 0 0;font-size:12px;color:#888888;\">TruckSetu &middot; Every truck, the shortest road.</p>\n"
        "</div>\n</body></html>\n";
    return message;
}

// Sends the code, or prints it where no email can go. Returns how it went: "email", "console" or "demo".
std::string deliverCode(const Settings& settings, const std::string& purpose, const std::string& email,
                        const std::string& fullName, const std::string& code) {
    const std::string what = purpose == "login" ? "Login" : "Sign-up";
    if (isExampleAddress(email)) {
        say("[CODE] " + what + " code for the demo address " + email + ": " + code);
        return "demo";
    }
    if (!settings.emailOn()) {
        say("[CODE] " + what + " code for " + email + ": " + code + "   (email isn't set up, so it is shown here; see .env.example)");
        return "console";
    }
    Message message = codeMessage(purpose, fullName, code);
    sendEmail(settings, email, message.subject, message.text, message.html);
    say("[MAIL] " + what + " code sent to " + maskEmail(email));
    return "email";
}

// What the page shows. A login shows the address half hidden, since so far the person has only proved the password;
// a sign-up shows the address that was just typed.
json describe(const std::string& ticket, const std::string& purpose, const std::string& email,
              const std::string& delivery, const std::string& code) {
    json result = {{"ticket", ticket}, {"purpose", purpose}, {"delivery", delivery},
                   {"email", purpose == "login" && delivery != "demo" ? maskEmail(email) : email},
                   {"resend_after", kResendSeconds}, {"minutes", kCodeMinutes}};
    if (delivery == "demo") result["code"] = code;
    return result;
}

void removeTicket(Database& db, const std::string& ticketHash) {
    Statement remove = db.prepare("DELETE FROM email_codes WHERE ticket_hash = ?1");
    remove.bind(1, ticketHash).run();
}

json startCode(Database& db, const Settings& settings, const std::string& purpose, std::int64_t userId,
               const std::string& email, const std::string& fullName, const json& signup) {
    db.exec("DELETE FROM email_codes WHERE expires_at <= datetime('now')");  // tidy up codes that ran out
    {
        // Only a few codes at a time per account (per address for sign-ups). This stops someone who knows a password
        // from guessing codes ticket after ticket, and anyone from flooding an address with emails.
        Statement open = db.prepare(purpose == "login" ? "SELECT COUNT(*) FROM email_codes WHERE user_id = ?1"
                                                       : "SELECT COUNT(*) FROM email_codes WHERE purpose = 'signup' AND email = ?1");
        if (purpose == "login") open.bind(1, userId);
        else open.bind(1, email);
        open.step();
        if (open.columnInt(0) >= kOpenTickets) {
            throw ApiError(429, "Several codes were sent in the last few minutes. Use the newest one, or wait " +
                                    std::to_string(kCodeMinutes) + " minutes and try again.");
        }
    }

    const std::string ticket = randomToken(), code = randomDigits(6), ticketHash = sha256Hex(ticket);
    {
        Statement add = db.prepare(R"SQL(
            INSERT INTO email_codes (ticket_hash, purpose, user_id, email, signup_json, code_hash, expires_at)
            VALUES (?1, ?2, ?3, ?4, ?5, ?6, datetime('now', ?7)))SQL");
        add.bind(1, ticketHash).bind(2, purpose);
        if (userId) add.bind(3, userId);
        else add.bindNull(3);
        add.bind(4, email);
        if (signup.is_null()) add.bindNull(5);
        else add.bind(5, signup.dump());
        add.bind(6, codeHash(ticket, code)).bind(7, "+" + std::to_string(kCodeMinutes) + " minutes").run();
    }

    // The email is sent outside any transaction: talking to the mail server takes a second or two
    std::string delivery;
    try {
        delivery = deliverCode(settings, purpose, email, fullName, code);
    } catch (const EmailError& e) {
        removeTicket(db, ticketHash);
        say("[MAIL] Couldn't send a code to " + maskEmail(email) + ": " + e.what());
        throw ApiError(502, "We couldn't send the email with your code just now. Please try again in a minute.");
    }
    return describe(ticket, purpose, email, delivery, code);
}

}  // namespace

json sendLoginCode(Database& db, const Settings& settings, std::int64_t userId, const std::string& email, const std::string& name) {
    return startCode(db, settings, "login", userId, email, name, nullptr);
}

json sendSignupCode(Database& db, const Settings& settings, const json& signup) {
    return startCode(db, settings, "signup", 0, signup.at("email").get<std::string>(),
                     signup.at("full_name").get<std::string>(), signup);
}

json resendCode(Database& db, const Settings& settings, const std::string& ticket) {
    if (!isTicket(ticket)) throw ApiError(410, kStartAgain);
    const std::string ticketHash = sha256Hex(ticket);
    std::string purpose, email, fullName;
    long long sends = 0, waited = 0;
    bool live = false;
    {
        Statement find = db.prepare(R"SQL(
            SELECT c.purpose, c.email, COALESCE(u.full_name, json_extract(c.signup_json, '$.full_name'), ''), c.sends,
                   CAST(strftime('%s', 'now') AS INTEGER) - CAST(strftime('%s', c.sent_at) AS INTEGER), c.expires_at > datetime('now')
            FROM email_codes c LEFT JOIN users u ON u.id = c.user_id
            WHERE c.ticket_hash = ?1)SQL");
        find.bind(1, ticketHash);
        if (!find.step()) throw ApiError(410, kStartAgain);
        purpose = find.columnText(0);
        email = find.columnText(1);
        fullName = find.columnText(2);
        sends = find.columnInt(3);
        waited = find.columnInt(4);
        live = find.columnInt(5) == 1;
    }
    if (!live) throw ApiError(410, kStartAgain);
    if (waited < kResendSeconds) {
        throw ApiError(429, "You can ask for a new code in " + std::to_string(kResendSeconds - waited) + " seconds");
    }
    if (sends >= kCodeSends) throw ApiError(429, "That's as many codes as we send for one try. Please start again.");

    // The new code replaces the old one; wrong tries still count, so asking again doesn't give more guesses
    const std::string code = randomDigits(6);
    {
        Statement update = db.prepare(R"SQL(
            UPDATE email_codes SET code_hash = ?2, sends = sends + 1, sent_at = datetime('now'), expires_at = datetime('now', ?3)
            WHERE ticket_hash = ?1)SQL");
        update.bind(1, ticketHash).bind(2, codeHash(ticket, code)).bind(3, "+" + std::to_string(kCodeMinutes) + " minutes").run();
    }
    std::string delivery;
    try {
        delivery = deliverCode(settings, purpose, email, fullName, code);
    } catch (const EmailError& e) {
        say("[MAIL] Couldn't send a code to " + maskEmail(email) + ": " + e.what());
        throw ApiError(502, "We couldn't send the email with your code just now. Please try again in a minute.");
    }
    return describe(ticket, purpose, email, delivery, code);
}

ConfirmedCode confirmCode(Database& db, const std::string& ticket, const std::string& typed) {
    // "123 456" and "123-456" are read as 123456
    std::string code;
    for (char c : typed) {
        if (c != ' ' && c != '-') code += c;
    }
    if (code.size() != 6 || code.find_first_not_of("0123456789") != std::string::npos) {
        throw ApiError(400, "Enter the 6-digit code from the email");
    }
    if (!isTicket(ticket)) throw ApiError(410, kStartAgain);
    const std::string ticketHash = sha256Hex(ticket);

    // One check at a time per ticket (the transaction takes the write lock), so two guesses can't both count as the last
    Transaction tx(db);
    ConfirmedCode confirmed;
    std::string storedHash, signupJson;
    long long tries = 0;
    bool live = false;
    {
        Statement find = db.prepare(R"SQL(
            SELECT purpose, COALESCE(user_id, 0), COALESCE(signup_json, ''), code_hash, tries, expires_at > datetime('now')
            FROM email_codes WHERE ticket_hash = ?1)SQL");
        find.bind(1, ticketHash);
        if (!find.step()) throw ApiError(410, kStartAgain);
        confirmed.purpose = find.columnText(0);
        confirmed.userId = find.columnInt(1);
        signupJson = find.columnText(2);
        storedHash = find.columnText(3);
        tries = find.columnInt(4);
        live = find.columnInt(5) == 1;
    }
    if (!live) {
        removeTicket(db, ticketHash);
        tx.commit();
        throw ApiError(410, "This code has run out. Please start again to get a new one.");
    }
    if (!sameText(storedHash, codeHash(ticket, code))) {
        long long left = kCodeTries - tries - 1;
        if (left <= 0) {
            removeTicket(db, ticketHash);
            tx.commit();
            throw ApiError(429, "Too many wrong codes. Please start again to get a new one.");
        }
        Statement count = db.prepare("UPDATE email_codes SET tries = tries + 1 WHERE ticket_hash = ?1");
        count.bind(1, ticketHash).run();
        tx.commit();
        throw ApiError(400, "That code isn't right. " + (left == 1 ? std::string("1 try left.") : std::to_string(left) + " tries left."));
    }
    removeTicket(db, ticketHash);
    tx.commit();
    if (!signupJson.empty()) confirmed.signup = json::parse(signupJson);
    return confirmed;
}
