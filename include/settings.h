#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>

// --- SETTINGS ---
// Read once at startup from the file .env in the folder the server runs in: one KEY=value per line,
// lines starting with # are comments. .env holds passwords, so git ignores it; .env.example lists every key
// and where to get its value. Without a .env file the server still works: port 8080, no Google button,
// and the email codes are printed in the server window instead of being emailed.
struct Settings {
    int port = 8080;                 // PORT
    std::string googleClientId;      // GOOGLE_CLIENT_ID: turns on "Continue with Google"
    std::string smtpUrl;             // SMTP_URL, e.g. smtps://smtp.gmail.com:465: turns on emailed codes
    std::string smtpUser;            // SMTP_USER
    std::string smtpPassword;        // SMTP_PASSWORD (for Gmail, an app password)
    std::string mailFrom;            // MAIL_FROM: who the codes come from, "TruckSetu <you@gmail.com>" (SMTP_USER if not set)
    // GOOGLE_CHECK_URL: where Google sign-ins are checked. Only the tests change it, to a fake Google on this computer.
    std::string googleCheckUrl = "https://oauth2.googleapis.com/tokeninfo";

    bool googleOn() const { return !googleClientId.empty(); }
    bool emailOn() const { return !smtpUrl.empty(); }
};

// Reads the file if it exists. Throws std::runtime_error naming the line for a value that can't work.
Settings loadSettings(const std::string& path);

// True for a web or mail address on this computer (localhost, 127.0.0.1 or [::1])
bool isLocalUrl(const std::string& url);

#endif
