#ifndef GOOGLE_H
#define GOOGLE_H

#include <string>
#include "settings.h"

// --- GOOGLE SIGN-IN ---
// Google's "Continue with Google" button gives the login page an ID token: a short-lived message signed by
// Google that says which Google account signed in, and for which website. The server asks Google (its tokeninfo
// address) whether the token is genuine and still valid, and checks that it was made for this site's client ID,
// so a token made for some other website can't be used to log in here.
struct GoogleAccount {
    std::string id;      // Google's number for the account ("sub"); it never changes
    std::string email;   // confirmed by Google, in lower case
    std::string name;
};

// Throws ApiError with a message for the page when Google doesn't vouch for the token
GoogleAccount checkGoogleToken(const Settings& settings, const std::string& idToken);

#endif
