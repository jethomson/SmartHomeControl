/* This header file outlines the credential variables required without containing actual credentials.
 * You may fill in your credentials here or put them in credentials_private.h using this file as a reference.
 */

#if __has_include("credentials_private.h")
#include "credentials_private.h"
#else
const char* wifi_ssid = "YOUR_WIFI_SSID";
const char* wifi_password = "YOUR_WIFI_PASSWORD";


/** The smtp host name e.g. smtp.gmail.com for GMail or smtp.office365.com for Outlook or smtp.mail.yahoo.com */
const char* smtp_host = "YOUR_SMTP_HOST";
const int smtp_port = 587; // gmail TLS


/** For Gmail, the app password will be used for log in
 *  Check out https://github.com/mobizt/ESP-Mail-Client#gmail-smtp-and-imap-required-app-passwords-to-sign-in
 *
 * For Yahoo mail, log in to your yahoo mail in web browser and generate app password by go to
 * https://login.yahoo.com/account/security/app-passwords/add/confirm?src=noSrc
 *
 * To use Gmail and Yahoo's App Password to sign in, define the AUTHOR_PASSWORD with your App Password
 * and AUTHOR_EMAIL with your account email.
 */

const char* smtp_author_email = "YOUR_SENDER_EMAIL";
const char* smtp_author_password = "YOUR_APP_PASSWORD";

const char* smtp_recipient_email = "YOUR_RECEIVER_EMAIL";

const char* smtp_message_id = "Message-ID: <YOUR_SENDER_EMAIL>";
#endif

