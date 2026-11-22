/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "stdafx.h"

#include "config.h"

// Aethergram: TDESKTOP_API_ID / TDESKTOP_API_HASH are passed as compile
// definitions to this file only (see Telegram/CMakeLists.txt), so changing
// the API credentials rebuilds this single translation unit instead of the
// whole app. config.h declares ApiId / ApiHash as extern constants and every
// other file links against the values defined here.
#if defined TDESKTOP_API_ID && defined TDESKTOP_API_HASH

extern const int ApiId = TDESKTOP_API_ID;
extern const char *const ApiHash = QT_STRINGIFY(TDESKTOP_API_HASH);

#else // TDESKTOP_API_ID && TDESKTOP_API_HASH

// To build your version of Telegram Desktop you're required to provide
// your own 'api_id' and 'api_hash' for the Telegram API access.
//
// How to obtain your 'api_id' and 'api_hash' is described here:
// https://core.telegram.org/api/obtaining_api_id
//
// If you're building the application not for deployment,
// but only for test purposes you can comment out the error below.
//
// This will allow you to use TEST ONLY 'api_id' and 'api_hash' which are
// very limited by the Telegram API server.
//
// Your users will start getting internal server errors on login
// if you deploy an app using those 'api_id' and 'api_hash'.

#error You are required to provide API_ID and API_HASH.

extern const int ApiId = 17349;
extern const char *const ApiHash = "344583e45741c457fe1862106095a5eb";

#endif // TDESKTOP_API_ID && TDESKTOP_API_HASH
