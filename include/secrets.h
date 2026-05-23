#pragma once

// This repo keeps secrets out of source control.
//
// Put your real credentials in `include/secrets.private.h` (git-ignored),
// or leave it missing to build without authenticated Firebase access.
//
// Template: `include/secrets.example.h`

#if defined(__has_include) && __has_include("secrets.private.h")
#  include "secrets.private.h"
#else
namespace Secrets {
constexpr const char* FIREBASE_API_KEY = "AIzaSyCzhyQ2pux9yH5pJU7d8wWVFldaDebg80E";
constexpr const char* FIREBASE_AUTH_EMAIL = "admin@gmail.com";
constexpr const char* FIREBASE_AUTH_PASSWORD = "adminmonitoring12345";
}  // namespace Secrets
#endif
