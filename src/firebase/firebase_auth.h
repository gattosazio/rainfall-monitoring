#ifndef FIREBASE_AUTH_H
#define FIREBASE_AUTH_H

#include <Arduino.h>

// Returns a valid Firebase Auth idToken (email/password sign-in).
// Caches the token until shortly before expiry.
bool firebaseEnsureIdToken(String& outIdToken);

#endif

