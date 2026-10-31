// wss:// through the system's OpenSSL, loaded at run time, against a TLS front for the local lobby that tools/test.sh
// starts with a throw-away certificate. The system certificate configuration (SSL_CERT_FILE) decides what is trusted.
// Exits 77 (not run) unless SSF2T_TLS_TEST_URL and SSF2T_TLS_TEST_CA name that front and its certificate.
// Run: ssf2t_online_websocket_tls_tests

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "online/websocket.h"

using namespace ssf2t::online;

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                 \
  do {                                                              \
    ++g_checks;                                                     \
    if (!(cond)) {                                                  \
      ++g_failures;                                                 \
      std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                               \
  } while (0)

std::string ReadFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

bool Has(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

void TrustOnly(const std::string& file) {
  setenv("SSL_CERT_FILE", file.c_str(), 1);
  setenv("SSL_CERT_DIR", "/nonexistent-ssf2t-certificates", 1);
}

std::string WithHost(const std::string& url, const std::string& host) {
  WsUrl parts;
  parse_ws_url(url, parts, nullptr);
  return "wss://" + host + ":" + std::to_string(parts.port) + parts.path;
}

void TestLoaderSource() {
  std::printf("the OpenSSL loader names both supported libraries and is not linked\n");
  const std::string source = ReadFile(std::string(SSF2T_SRC_DIR) + "/online/websocket_posix.cpp");
  CHECK(Has(source, "\"libssl.so.3\""));
  CHECK(Has(source, "\"libssl.so.1.1\""));
  CHECK(Has(source, "dlopen(name, RTLD_NOW | RTLD_LOCAL)"));
  CHECK(Has(source, "SSL_CTX_set_default_verify_paths"));
  CHECK(Has(source, "SSL_VERIFY_PEER"));
  CHECK(Has(source, "SSL_set1_host"));
  CHECK(!Has(source, "#include <openssl/"));
}

void TestTrustedCertificate(const std::string& url, const std::string& ca) {
  std::printf("a certificate the system configuration trusts is accepted\n");
  TrustOnly(ca);
  std::string error;
  HttpResult health;
  CHECK(http_get(url, "/health", 5000, health, &error));
  CHECK(health.status == 200);
  CHECK(Has(health.body, "\"status\":\"ok\""));
  auto socket = WebSocket::connect(url, 5000, &error);
  CHECK(socket != nullptr);
  if (socket) {
    socket->close();
  }
  const std::string maps = ReadFile("/proc/self/maps");
  CHECK(Has(maps, "libssl.so.3") || Has(maps, "libssl.so.1.1"));
  CHECK(!Has(maps, "libcrypto.so") || Has(maps, "libssl.so"));
}

void TestRefusals(const std::string& url, const std::string& ca) {
  std::printf("a certificate the system does not trust is refused\n");
  TrustOnly("/dev/null");
  std::string error;
  CHECK(WebSocket::connect(url, 5000, &error) == nullptr);
  CHECK(Has(error, "certificate"));
  HttpResult health;
  CHECK(!http_get(url, "/health", 5000, health, &error));

  std::printf("a certificate for another host name is refused\n");
  TrustOnly(ca);
  error.clear();
  CHECK(WebSocket::connect(WithHost(url, "127.0.0.1"), 5000, &error) == nullptr);
  CHECK(Has(error, "certificate"));
}

}  // namespace

int main() {
  const char* url = std::getenv("SSF2T_TLS_TEST_URL");
  const char* ca = std::getenv("SSF2T_TLS_TEST_CA");
  if (!url || !ca) {
    std::printf("NOT RUN: SSF2T_TLS_TEST_URL and SSF2T_TLS_TEST_CA are not set (tools/test.sh starts the TLS front)\n");
    return 77;
  }
  TestLoaderSource();
  TestTrustedCertificate(url, ca);
  TestRefusals(url, ca);
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
