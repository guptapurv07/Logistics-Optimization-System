#include "../include/email.h"
#include "../include/api.h"
#include "../include/password.h"
#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <memory>

namespace {

struct Upload {
    std::string data;
    std::size_t sent = 0;
};

// libcurl asks for the message piece by piece while it sends it
std::size_t readChunk(char* buffer, std::size_t size, std::size_t count, void* userData) {
    Upload& upload = *static_cast<Upload*>(userData);
    std::size_t length = std::min(size * count, upload.data.size() - upload.sent);
    std::memcpy(buffer, upload.data.data() + upload.sent, length);
    upload.sent += length;
    return length;
}

// "TruckSetu <codes@gmail.com>" gives the name and the address; a bare "codes@gmail.com" has no name
void splitMailbox(const std::string& mailbox, std::string& name, std::string& address) {
    std::size_t open = mailbox.find('<'), close = mailbox.rfind('>');
    if (open != std::string::npos && close != std::string::npos && close > open) {
        name = trim(mailbox.substr(0, open));
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') name = name.substr(1, name.size() - 2);
        address = trim(mailbox.substr(open + 1, close - open - 1));
    } else {
        name.clear();
        address = trim(mailbox);
    }
}

// Addresses go into the mail server's commands as they are, so anything that could end a command or a header
// (spaces, line breaks, brackets, quotes...) is refused
bool isSafeAddress(const std::string& address) {
    if (address.empty() || address.size() > 254 || std::count(address.begin(), address.end(), '@') != 1) return false;
    return std::none_of(address.begin(), address.end(), [](unsigned char c) {
        return c <= 0x20 || c >= 0x7F || std::strchr("<>()[],;:\"\\", c) != nullptr;
    });
}

// Each line of an email ends with CR LF
std::string withCrLf(const std::string& text) {
    std::string out;
    out.reserve(text.size() + text.size() / 40);
    for (char c : text) {
        if (c == '\n') out += "\r\n";
        else if (c != '\r') out += c;
    }
    return out;
}

// The Date header, e.g. "Sat, 07 Nov 2026 15:04:05 +0530" (the C locale keeps the day and month names in English)
std::string emailDate() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_r(&now, &local);
    char text[64];
    std::strftime(text, sizeof(text), "%a, %d %b %Y %H:%M:%S %z", &local);
    return text;
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

}  // namespace

void sendEmail(const Settings& settings, const std::string& to, const std::string& subject,
               const std::string& text, const std::string& html) {
    std::string fromName, fromAddress;
    splitMailbox(settings.mailFrom, fromName, fromAddress);
    if (fromName.empty()) fromName = "TruckSetu";
    if (!isSafeAddress(to)) throw EmailError("refused to send to an address with unusual characters");
    if (!isSafeAddress(fromAddress)) throw EmailError("MAIL_FROM is not a plain email address");

    // One message with two versions: mail apps show the HTML one and fall back to the text
    const std::string boundary = "trucksetu-" + randomToken().substr(0, 24);
    const std::string domain = fromAddress.substr(fromAddress.find('@') + 1);
    Upload upload;
    upload.data = "Date: " + emailDate() + "\r\n"
                  "From: " + fromName + " <" + fromAddress + ">\r\n"
                  "To: <" + to + ">\r\n"
                  "Subject: " + subject + "\r\n"
                  "Message-ID: <" + randomToken().substr(0, 32) + "@" + domain + ">\r\n"
                  "MIME-Version: 1.0\r\n"
                  "Content-Type: multipart/alternative; boundary=\"" + boundary + "\"\r\n"
                  "\r\n"
                  "--" + boundary + "\r\n"
                  "Content-Type: text/plain; charset=UTF-8\r\n"
                  "Content-Transfer-Encoding: 8bit\r\n"
                  "\r\n" + withCrLf(text) + "\r\n"
                  "--" + boundary + "\r\n"
                  "Content-Type: text/html; charset=UTF-8\r\n"
                  "Content-Transfer-Encoding: 8bit\r\n"
                  "\r\n" + withCrLf(html) + "\r\n"
                  "--" + boundary + "--\r\n";

    CURL* curl = curl_easy_init();
    if (!curl) throw EmailError("libcurl couldn't start");
    std::unique_ptr<CURL, void (*)(CURL*)> cleanup(curl, curl_easy_cleanup);
    const std::string envelopeFrom = "<" + fromAddress + ">", envelopeTo = "<" + to + ">";
    curl_slist* recipients = curl_slist_append(nullptr, envelopeTo.c_str());
    std::unique_ptr<curl_slist, void (*)(curl_slist*)> freeRecipients(recipients, curl_slist_free_all);
    char error[CURL_ERROR_SIZE] = "";

    curl_easy_setopt(curl, CURLOPT_URL, settings.smtpUrl.c_str());
    if (!settings.smtpUser.empty()) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, settings.smtpUser.c_str());
        curl_easy_setopt(curl, CURLOPT_PASSWORD, settings.smtpPassword.c_str());
    }
    // smtps:// is encrypted from the start; smtp:// must switch to TLS (STARTTLS) before the password is sent.
    // Only a test mail server on this computer may be plain.
    curl_easy_setopt(curl, CURLOPT_USE_SSL, static_cast<long>(isLocalUrl(settings.smtpUrl) ? CURLUSESSL_NONE : CURLUSESSL_ALL));
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, envelopeFrom.c_str());
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readChunk);
    curl_easy_setopt(curl, CURLOPT_READDATA, &upload);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);   // timeouts without signals, which aren't safe with several threads
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);

    CURLcode result = curl_easy_perform(curl);
    if (result == CURLE_LOGIN_DENIED) {
        throw EmailError("the mail server refused the login: check SMTP_USER and SMTP_PASSWORD in .env (Gmail needs an app password)");
    }
    if (result != CURLE_OK) throw EmailError(error[0] ? error : curl_easy_strerror(result));
}

bool isExampleAddress(const std::string& email) {
    std::size_t at = email.rfind('@');
    if (at == std::string::npos) return false;
    std::string domain = lowercase(email.substr(at + 1));
    for (const char* reserved : {"example.com", "example.net", "example.org"}) {
        std::string dotted = std::string(".") + reserved;
        if (domain == reserved || (domain.size() > dotted.size() && domain.compare(domain.size() - dotted.size(), dotted.size(), dotted) == 0)) {
            return true;
        }
    }
    return domain == "example" || (domain.size() > 8 && domain.compare(domain.size() - 8, 8, ".example") == 0);
}

std::string maskEmail(const std::string& email) {
    std::size_t at = email.find('@');
    if (at == std::string::npos) return email;
    std::size_t keep = at <= 3 ? 1 : 2;
    return email.substr(0, keep) + "•••" + email.substr(at);
}
