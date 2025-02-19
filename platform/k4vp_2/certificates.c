#include <stdint.h>
#include <esp_https_server.h>

// This is generated with
// openssl req -newkey rsa:2048 -nodes -keyout prvtkey.pem -x509 -days 3650 -out servercert.pem -subj "/CN=K6BP HT Firmware"
//


// This is enough to get a LetsEncrypt certificate
// for a public IP, and we might also work out a way to issue device users
// individual certificates which stay private.

// FIX: Might not need the newline character here.
// Test that after this works.
#if 0
static const uint8_t	ca_public_pem[] = "\
-----BEGIN CERTIFICATE-----\n\
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\n\
dMz+MJYNPA==\n\
-----END CERTIFICATE-----\n";
#endif

static const uint8_t	public_pem[] = "\
-----BEGIN CERTIFICATE-----\n\
MIIDKzCCAhOgAwIBAgIUR1W8AFe8ybzciU5Tu5qhWU15l60wDQYJKoZIhvcNAQEL\n\
BQAwJTEjMCEGA1UEAwwaRVNQMzIgSFRUUFMgc2VydmVyIGV4YW1wbGUwHhcNMjUw\n\
MjE5MDEzMDA4WhcNMzUwMjE3MDEzMDA4WjAlMSMwIQYDVQQDDBpFU1AzMiBIVFRQ\n\
UyBzZXJ2ZXIgZXhhbXBsZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEB\n\
ALdexY9ryWz+LIdbzMVkXU5lTxGC9D+BLadOtpeO1yUMr7SjLnZjVkK/q+oRdMan\n\
QZR9zoxWlsv7XW26ZYf1fPx4qmpGvDFRDlAYfEvNlazlW+Bpmk5XgglnQ7xyduoy\n\
8JX9Ftx6iwqhd0v5vpG+WbBI7GWzdGu1PJBqXU9jhrEDhsouVEm7Roub9Hzyr7X0\n\
j0GvyZS+PBN68UaFXAQgoMt+4kTEBFTcHolAIfLvS3V5tMFValROv3OEWhglBLaB\n\
8aQilOqzk6f/qxYONBvxPdj0giV4+mINiuBMqk7A1tnxSqN4uMA8U1SK+2IjwOJ8\n\
bjFcCGzFHZFtB0tnrEzbi48CAwEAAaNTMFEwHQYDVR0OBBYEFPPZQQYgkBbUft0I\n\
VBscGUF/PlFsMB8GA1UdIwQYMBaAFPPZQQYgkBbUft0IVBscGUF/PlFsMA8GA1Ud\n\
EwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBAEiows7Kedax1gHYbfkXf2eb\n\
qAwQGKDewCpkf6rgvLYUG7kC8QTkTx2ik7ZWliMFqxJ7n/31XChdWYQmoJl5zUon\n\
7vw3hPpHAT3v7AJ0NeCY3tE0i+kRLk7WlnFbjbIa0bK3G7ewrxaO3lxt6HFhQGh/\n\
zFDZybUZARuY26Gj2euGnLrPTj+MABodcP6uomdr8OKm0jRXPHOC0y7ot1KBgOQh\n\
EJC9JqxiJQZXEHC8pVS+n+hyzpwm02e8cf2tPWDQRGd18loLwHazzN4PzQfOB2IL\n\
SwzdhB2OWzwpPuAHgIF9FWzt2SjeG2BCWiJLmK6ZV/m8CbJ/SMwPR1lmmQfz7fA=\n\
-----END CERTIFICATE-----\n";

static const uint8_t	private_pem[] = "\
-----BEGIN PRIVATE KEY-----\n\
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQC3XsWPa8ls/iyH\n\
W8zFZF1OZU8RgvQ/gS2nTraXjtclDK+0oy52Y1ZCv6vqEXTGp0GUfc6MVpbL+11t\n\
umWH9Xz8eKpqRrwxUQ5QGHxLzZWs5VvgaZpOV4IJZ0O8cnbqMvCV/RbceosKoXdL\n\
+b6RvlmwSOxls3RrtTyQal1PY4axA4bKLlRJu0aLm/R88q+19I9Br8mUvjwTevFG\n\
hVwEIKDLfuJExARU3B6JQCHy70t1ebTBVWpUTr9zhFoYJQS2gfGkIpTqs5On/6sW\n\
DjQb8T3Y9IIlePpiDYrgTKpOwNbZ8UqjeLjAPFNUivtiI8DifG4xXAhsxR2RbQdL\n\
Z6xM24uPAgMBAAECggEARrbgFi6Zh5Q/pNWUktzhFL4B731iZukQq5Ax3yGhO3L4\n\
gX9aiCJH3kbfa81ejMtsgXDAyiMMVU6zGYFD7VoQ4KZCBekQYy5giCfhKzR1j68F\n\
rvXaoXKivbCakR5NWITbfeQTwGProeZhxKVjxf2zxcVhkNjTQUQrvn+eS2LUywcr\n\
eTBy9AVLNrUBfcdVItp6CTQIet7gsIFesUPCQD+3Oll1AkHu6d04u05mJPUnMsR8\n\
WsdwPBNjQnsvPfBFkOqC+g6ffxNlkaRh13KDagMz/rdNjnduvButcdb3h1hn+dkR\n\
1rDl4kImeXTDqqfb9g0lBBtC30+niRjaK2wHQoUGLQKBgQDds6F+zALArYZ1ctb6\n\
Eu1MpCbkyrzs6qH0VB4w5EXMxCzalJHAsowNG6rNOg94DiLeybO+b4AJ+Bh4gnC2\n\
VPri3752mZen4jITsK0zbLht9I7404RMd/hWY/hrEdJ3beKC5n5i4S5if/VRFos8\n\
QvtQg2ch+6FTktndB65rK/xSZQKBgQDTvQtbLnUYejPxTIRYShfMF4j3wxQEaEfy\n\
PS1Z1H0p9C3w8OWpFb+pDTPp+3ScoYJerFab5k92ccdr5Hvi9YOiljS/rYYWrfRU\n\
pC5sii7w7J0KpZj/f9/HMQoscfkpgcY1MQkjc6JTbpDsa2A2uwOf8DV2vI5vjGJ2\n\
FXOWzj7M4wKBgF0pBXy1l5aFlFG4HxPYbjTdaaaMdtULR2DXFBSxZ49DBCIZeiHC\n\
JU3AyYYRlrx5HDgIA+rLCb+mrAm/rkM/9GjvYiaOJgca6rYRcMaCMgGqWYW/xAEq\n\
DIWo3pOHWqxq1VryJjvAqfAkGt6nHX0GHHrZrLW/+iuXMstJVgac5httAoGBAMlE\n\
cX8C8huKTcWYzlR+WZCO7otUy7pExd7leC5jZXZzvRfCZQwMuFqhSi+n7njUrXAA\n\
sXMPwYj9LmrIjVp3teDeltV8xHDaed3bEqXp1CT/RWfRcVWs37IgU2NcE1P5H8eA\n\
DQe96xcTin6wsoxTyGZHlwp+wYYXE6DbUzyOX/OxAoGAKezgOfwlb60oVy4CokXk\n\
b378JsTzR+pDnCgvZDXY1eu8ufGfrc5lBYfQpTu+03qsbpbPNoNFi5qBp0PLlwVU\n\
gE18NeVr2kOFxNjzGp18LkPpUGJ6zaWbGdATT9Z94wgBIf3rGc2SCn0VgUelvJ5A\n\
piA8CO37C+b7hu9uh7SQssg=\n\
-----END PRIVATE KEY-----\n";

// Call this on a structure that has already been initialized with
// HTTPD_SSL_CONFIG_DEFAULT().
void
gm_self_signed_ssl_certificates(struct httpd_ssl_config * c)
{
  c->servercert = public_pem;
  c->servercert_len = sizeof(public_pem);
  // c->cacert_pem = ca_public_pem;
  // c->cacert_len = sizeof(ca_public_pem);
  c->prvtkey_pem = private_pem;
  c->prvtkey_len = sizeof(private_pem);
}
