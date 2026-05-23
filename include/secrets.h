#pragma once

// Local secrets for Firebase authenticated writes.
// This file is git-ignored on purpose (`include/secrets.h`).
//
// Fill these in:
// - FIREBASE_API_KEY: Firebase Console -> Project settings -> General -> Web API Key
// - FIREBASE_AUTH_EMAIL / FIREBASE_AUTH_PASSWORD: create a dedicated Auth user (Email/Password)

namespace Secrets {
constexpr const char* FIREBASE_API_KEY = "AIzaSyCzhyQ2pux9yH5pJU7d8wWVFldaDebg80E";
constexpr const char* FIREBASE_AUTH_EMAIL = "device-node2@gmail.com";
constexpr const char* FIREBASE_AUTH_PASSWORD = "node2admin12345";
}
