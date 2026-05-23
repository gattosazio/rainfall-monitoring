#pragma once

// Copy this file to `include/secrets.private.h` and fill in your own values.
// DO NOT commit `include/secrets.private.h` (it is git-ignored).
//
// - FIREBASE_API_KEY: Firebase Console -> Project settings -> General -> Web API Key
// - FIREBASE_AUTH_EMAIL / FIREBASE_AUTH_PASSWORD: create a dedicated Auth user (Email/Password)

namespace Secrets {
constexpr const char* FIREBASE_API_KEY = "";
constexpr const char* FIREBASE_AUTH_EMAIL = "";
constexpr const char* FIREBASE_AUTH_PASSWORD = "";
}  // namespace Secrets
