#include <hxcpp.h>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/md.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/version.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#endif

namespace {

class BlockingScope final {
public:
    BlockingScope()
        : attached_(::hx::gc::Runtime::instance().currentThreadAttached()) {
        if (attached_)
            ::hx::gc::Runtime::instance().enterNativeBlocking();
    }
    ~BlockingScope() {
        if (attached_)
            ::hx::gc::Runtime::instance().leaveNativeBlocking();
    }
private:
    bool attached_ = false;
};

class CallbackMutatorScope final {
public:
    CallbackMutatorScope()
        : attached_(::hx::gc::Runtime::instance().currentThreadAttached()) {
        if (attached_)
            ::hx::gc::Runtime::instance().leaveNativeBlocking();
    }
    ~CallbackMutatorScope() {
        if (attached_)
            ::hx::gc::Runtime::instance().enterNativeBlocking();
    }
private:
    bool attached_ = false;
};

[[noreturn]] void sslFailure(int code) {
    std::array<char, 256> message{};
    mbedtls_strerror(code, message.data(), message.size());
    throw ::Dynamic(::String(message.data()));
}

[[noreturn]] void sslFailure(const char* message) {
    throw ::Dynamic(::String(message));
}

bool sslWouldBlock(int result) noexcept {
    return result == MBEDTLS_ERR_SSL_WANT_READ ||
           result == MBEDTLS_ERR_SSL_WANT_WRITE;
}

void checkIoResult(int result, const char* operation) {
    if (sslWouldBlock(result))
        throw ::Dynamic(::String("Blocking"));
    if (result < 0)
        sslFailure(result == MBEDTLS_ERR_NET_RECV_FAILED ||
                   result == MBEDTLS_ERR_NET_SEND_FAILED
                       ? operation : "TLS I/O failed");
}

struct TlsGlobals final {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    std::once_flag initialized;

    TlsGlobals() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&random);
    }
    ~TlsGlobals() {
        mbedtls_ctr_drbg_free(&random);
        mbedtls_entropy_free(&entropy);
    }
};

TlsGlobals tlsGlobals;

void ensureTls() {
    std::call_once(tlsGlobals.initialized, [] {
        static constexpr unsigned char personalization[] =
            "NovaFlare hxcpp-zgc TLS";
        const int result = mbedtls_ctr_drbg_seed(
            &tlsGlobals.random, mbedtls_entropy_func, &tlsGlobals.entropy,
            personalization, sizeof(personalization) - 1);
        if (result != 0)
            sslFailure(result);
    });
}

struct CertificateChain final {
    mbedtls_x509_crt value;
    CertificateChain() { mbedtls_x509_crt_init(&value); }
    ~CertificateChain() { mbedtls_x509_crt_free(&value); }
};

struct NativeCertificate final {
    std::shared_ptr<CertificateChain> owner;
    mbedtls_x509_crt* current = nullptr;
};

struct NativeKey final {
    mbedtls_pk_context value;
    NativeKey() { mbedtls_pk_init(&value); }
    ~NativeKey() { mbedtls_pk_free(&value); }
};

struct NativeConfig final {
    mbedtls_ssl_config value;
    ::hx::gc::RefSlot callback{0};
    std::vector<::hx::gc::Handle> dependencies;

    NativeConfig() {
        mbedtls_ssl_config_init(&value);
        ::hx::gc::Runtime::instance().registerStaticRoot(callback);
    }
    ~NativeConfig() {
        auto& runtime = ::hx::gc::Runtime::instance();
        for (const auto handle : dependencies)
            if (handle.valid()) runtime.releaseHandle(handle);
        runtime.unregisterStaticRoot(callback);
        mbedtls_ssl_config_free(&value);
    }
};

struct NativeSsl final {
    mbedtls_ssl_context value;
    ::hx::gc::Handle config{};
    std::uintptr_t socket = 0;

    NativeSsl() { mbedtls_ssl_init(&value); }
    ~NativeSsl() {
        mbedtls_ssl_free(&value);
        if (config.valid())
            ::hx::gc::Runtime::instance().releaseHandle(config);
    }
};

template<class Native>
struct NativeShell final : ::hx::Object {
    Native* native = nullptr;
};

using SslShell = NativeShell<NativeSsl>;
using ConfigShell = NativeShell<NativeConfig>;
using CertificateShell = NativeShell<NativeCertificate>;
using KeyShell = NativeShell<NativeKey>;

template<class Shell>
void finalizeShell(void* object) {
    auto* shell = static_cast<Shell*>(object);
    delete shell->native;
    shell->native = nullptr;
}

const ::hx::gc::TypeDescriptor SslDescriptor{
    0x4e4f5641544c5301ULL, "NovaGC.TlsContext", nullptr,
    sizeof(SslShell), alignof(SslShell), 0, ::hx::gc::TypeFinalizable,
    0, nullptr, nullptr, &finalizeShell<SslShell>};
const ::hx::gc::TypeDescriptor ConfigDescriptor{
    0x4e4f5641544c5302ULL, "NovaGC.TlsConfig", nullptr,
    sizeof(ConfigShell), alignof(ConfigShell), 0, ::hx::gc::TypeFinalizable,
    0, nullptr, nullptr, &finalizeShell<ConfigShell>};
const ::hx::gc::TypeDescriptor CertificateDescriptor{
    0x4e4f5641544c5303ULL, "NovaGC.TlsCertificate", nullptr,
    sizeof(CertificateShell), alignof(CertificateShell), 0,
    ::hx::gc::TypeFinalizable, 0, nullptr, nullptr,
    &finalizeShell<CertificateShell>};
const ::hx::gc::TypeDescriptor KeyDescriptor{
    0x4e4f5641544c5304ULL, "NovaGC.TlsKey", nullptr,
    sizeof(KeyShell), alignof(KeyShell), 0, ::hx::gc::TypeFinalizable,
    0, nullptr, nullptr, &finalizeShell<KeyShell>};

template<class Shell>
Shell* decodeShell(const ::Dynamic& value,
                   const ::hx::gc::TypeDescriptor& descriptor,
                   const char* message) {
    if (value.tag() != ::Dynamic::Tag::Object)
        sslFailure(message);
    auto* shell = static_cast<Shell*>(value.managedPointer());
    if (shell == nullptr ||
        ::hx::gc::Runtime::instance().typeOf(shell) != &descriptor ||
        shell->native == nullptr)
        sslFailure(message);
    return shell;
}

template<class Shell, class Native>
::Dynamic makeShell(const ::hx::gc::TypeDescriptor& descriptor,
                    Native* native) {
    auto shell = ::hx::allocate<Shell>(descriptor);
    shell->native = native;
    return ::Dynamic(shell);
}

::Dynamic makeCertificate(std::shared_ptr<CertificateChain> owner,
                          mbedtls_x509_crt* current = nullptr) {
    auto* native = new NativeCertificate();
    native->owner = std::move(owner);
    native->current = current == nullptr ? &native->owner->value : current;
    return makeShell<CertificateShell>(CertificateDescriptor, native);
}

mbedtls_x509_crt* certificateValue(CertificateShell* shell) {
    if (shell->native->current == nullptr)
        sslFailure("invalid TLS certificate");
    return shell->native->current;
}

void retainDependency(NativeConfig& config, void* object) {
    config.dependencies.push_back(::hx::gc::Runtime::instance().createHandle(
        object, ::hx::gc::HandleKind::Strong));
}

#ifdef _WIN32
int networkRead(void* context, unsigned char* buffer, std::size_t length) {
    const SOCKET socket = static_cast<SOCKET>(
        reinterpret_cast<std::uintptr_t>(context));
    const int result = ::recv(socket, reinterpret_cast<char*>(buffer),
        static_cast<int>(std::min<std::size_t>(length, INT_MAX)), 0);
    if (result != SOCKET_ERROR)
        return result;
    const int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEALREADY ||
        error == WSAETIMEDOUT)
        return MBEDTLS_ERR_SSL_WANT_READ;
    return MBEDTLS_ERR_NET_RECV_FAILED;
}

int networkWrite(void* context, const unsigned char* buffer,
                 std::size_t length) {
    const SOCKET socket = static_cast<SOCKET>(
        reinterpret_cast<std::uintptr_t>(context));
    const int result = ::send(socket, reinterpret_cast<const char*>(buffer),
        static_cast<int>(std::min<std::size_t>(length, INT_MAX)), 0);
    if (result != SOCKET_ERROR)
        return result;
    const int error = WSAGetLastError();
    if (error == WSAEWOULDBLOCK || error == WSAEALREADY ||
        error == WSAETIMEDOUT)
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    return MBEDTLS_ERR_NET_SEND_FAILED;
}
#endif

int serverNameCallback(void* context, mbedtls_ssl_context* ssl,
                       const unsigned char* name, std::size_t length) {
    auto* config = static_cast<NativeConfig*>(context);
    if (config == nullptr || name == nullptr)
        return -1;
    try {
        CallbackMutatorScope mutator;
        void* callbackObject = ::hx::gc::Runtime::instance().loadBarrier(
            config->callback);
        if (callbackObject == nullptr)
            return -1;
        ::Dynamic callback(static_cast<::hx::Object*>(callbackObject));
        ::hx::gc::RefSlot* callbackRoots[]{&callback.mPtr};
        ::hx::gc::RootScope callbackRoot(callbackRoots, 1);
        ::Dynamic result = callback(::String(
            reinterpret_cast<const char*>(name), static_cast<int>(length)));
        ::hx::gc::RefSlot* resultRoots[]{&result.mPtr};
        ::hx::gc::RootScope resultRoot(resultRoots, 1);
        if (result.isNull())
            return -1;
        ::Dynamic certificate = result.__Field(::String("cert"),
                                                ::hx::paccDynamic);
        ::Dynamic key = result.__Field(::String("key"), ::hx::paccDynamic);
        ::hx::gc::RefSlot* roots[]{&certificate.mPtr, &key.mPtr};
        ::hx::gc::RootScope rootScope(roots, 2);
        ::hx::ScopedPin certificatePin(decodeShell<CertificateShell>(
            certificate, CertificateDescriptor, "invalid SNI certificate"));
        ::hx::ScopedPin keyPin(decodeShell<KeyShell>(
            key, KeyDescriptor, "invalid SNI key"));
        auto* certificateShell = static_cast<CertificateShell*>(
            certificatePin.get());
        auto* keyShell = static_cast<KeyShell*>(keyPin.get());
        return mbedtls_ssl_set_hs_own_cert(ssl,
            certificateValue(certificateShell), &keyShell->native->value);
    } catch (...) {
        return -1;
    }
}

struct PinnedBytes final {
    ::hx::ScopedPin arrayPin;
    ::hx::ScopedPin bufferPin;
    unsigned char* data;
    int length;

    explicit PinnedBytes(const ::Array<unsigned char>& value)
        : arrayPin(value.get()),
          bufferPin(reinterpret_cast<::hx::Object*>(
              ::hx::gc::Runtime::instance().loadBarrier(
                  static_cast<::Array_obj<unsigned char>*>(
                      arrayPin.get())->buffer))),
          data(bufferPin.get() == nullptr ? nullptr :
              ::hx::detail::arrayElements(
                  static_cast<::hx::detail::ArrayBuffer<unsigned char>*>(
                      reinterpret_cast<void*>(bufferPin.get())))),
          length(static_cast<::Array_obj<unsigned char>*>(
              arrayPin.get())->length) {}
};

::Array<unsigned char> makeBytes(const unsigned char* data,
                                 std::size_t length) {
    if (length > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        sslFailure("TLS byte result exceeds Haxe Array capacity");
    auto result = ::Array_obj<unsigned char>::__new(static_cast<int>(length));
    for (std::size_t index = 0; index < length; ++index)
        result->setCtx(nullptr, static_cast<int>(index), data[index]);
    return result;
}

const mbedtls_md_info_t* digestInfo(const ::String& algorithm) {
    const std::string name(algorithm.view());
    const auto* result = mbedtls_md_info_from_string(name.c_str());
    if (result == nullptr)
        sslFailure("Invalid hash algorithm");
    return result;
}

::String nameValue(const mbedtls_x509_name* name, const ::String& field) {
    const std::string wanted(field.view());
    for (auto* current = name; current != nullptr; current = current->next) {
        const char* shortName = nullptr;
        if (mbedtls_oid_get_attr_short_name(&current->oid, &shortName) == 0 &&
            shortName != nullptr && wanted == shortName)
            return ::String(reinterpret_cast<const char*>(current->val.p),
                            static_cast<int>(current->val.len));
    }
    return nullptr;
}

::Array<int> certificateTime(const mbedtls_x509_time& time) {
    const int values[]{time.year, time.mon, time.day,
                       time.hour, time.min, time.sec};
    return ::Array_obj<int>::fromData(values, 6);
}

} // namespace

::Dynamic _hx_ssl_init() {
    ensureTls();
    return nullptr;
}

::Dynamic _hx_ssl_conf_new(bool server) {
    ensureTls();
    auto* native = new NativeConfig();
    const int result = mbedtls_ssl_config_defaults(&native->value,
        server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) {
        delete native;
        sslFailure(result);
    }
    mbedtls_ssl_conf_rng(&native->value, mbedtls_ctr_drbg_random,
                         &tlsGlobals.random);
    return makeShell<ConfigShell>(ConfigDescriptor, native);
}

::Dynamic _hx_ssl_conf_close(::Dynamic config) {
    ::hx::ScopedPin pin(decodeShell<ConfigShell>(config, ConfigDescriptor,
                                                  "closed TLS config"));
    auto* shell = static_cast<ConfigShell*>(pin.get());
    delete shell->native;
    shell->native = nullptr;
    return nullptr;
}

::Dynamic _hx_ssl_conf_set_cert(::Dynamic config, ::Dynamic certificate,
                                ::Dynamic key) {
    ::hx::ScopedPin configPin(decodeShell<ConfigShell>(
        config, ConfigDescriptor, "invalid TLS config"));
    ::hx::ScopedPin certificatePin(decodeShell<CertificateShell>(
        certificate, CertificateDescriptor, "invalid TLS certificate"));
    ::hx::ScopedPin keyPin(decodeShell<KeyShell>(
        key, KeyDescriptor, "invalid TLS key"));
    auto* native = static_cast<ConfigShell*>(configPin.get())->native;
    auto* cert = static_cast<CertificateShell*>(certificatePin.get());
    auto* nativeKey = static_cast<KeyShell*>(keyPin.get());
    const int result = mbedtls_ssl_conf_own_cert(&native->value,
        certificateValue(cert), &nativeKey->native->value);
    if (result != 0) sslFailure(result);
    retainDependency(*native, cert);
    retainDependency(*native, nativeKey);
    return nullptr;
}

::Dynamic _hx_ssl_conf_set_ca(::Dynamic config, ::Dynamic certificate) {
    ::hx::ScopedPin configPin(decodeShell<ConfigShell>(
        config, ConfigDescriptor, "invalid TLS config"));
    auto* native = static_cast<ConfigShell*>(configPin.get())->native;
    if (certificate.isNull()) {
        mbedtls_ssl_conf_ca_chain(&native->value, nullptr, nullptr);
        return nullptr;
    }
    ::hx::ScopedPin certificatePin(decodeShell<CertificateShell>(
        certificate, CertificateDescriptor, "invalid TLS certificate"));
    auto* cert = static_cast<CertificateShell*>(certificatePin.get());
    mbedtls_ssl_conf_ca_chain(&native->value, certificateValue(cert), nullptr);
    retainDependency(*native, cert);
    return nullptr;
}

::Dynamic _hx_ssl_conf_set_verify(::Dynamic config, int mode) {
    ::hx::ScopedPin pin(decodeShell<ConfigShell>(config, ConfigDescriptor,
                                                  "invalid TLS config"));
    mbedtls_ssl_conf_authmode(&static_cast<ConfigShell*>(pin.get())->native->value,
        mode == 1 ? MBEDTLS_SSL_VERIFY_REQUIRED :
        mode == 2 ? MBEDTLS_SSL_VERIFY_OPTIONAL : MBEDTLS_SSL_VERIFY_NONE);
    return nullptr;
}

::Dynamic _hx_ssl_conf_set_servername_callback(::Dynamic config,
                                                ::Dynamic callback) {
    ::hx::ScopedPin pin(decodeShell<ConfigShell>(config, ConfigDescriptor,
                                                  "invalid TLS config"));
    auto* native = static_cast<ConfigShell*>(pin.get())->native;
    ::hx::gc::Runtime::instance().storeBarrier(nullptr, native->callback,
                                                callback.managedPointer());
    mbedtls_ssl_conf_sni(&native->value, serverNameCallback, native);
    return nullptr;
}

::Dynamic _hx_ssl_new(::Dynamic config) {
    ::hx::ScopedPin configPin(decodeShell<ConfigShell>(
        config, ConfigDescriptor, "invalid TLS config"));
    auto* native = new NativeSsl();
    const int result = mbedtls_ssl_setup(&native->value,
        &static_cast<ConfigShell*>(configPin.get())->native->value);
    if (result != 0) {
        delete native;
        sslFailure(result);
    }
    native->config = ::hx::gc::Runtime::instance().createHandle(
        configPin.get(), ::hx::gc::HandleKind::Strong);
    return makeShell<SslShell>(SslDescriptor, native);
}

::Dynamic _hx_ssl_set_socket(::Dynamic ssl, ::Dynamic socket) {
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "invalid TLS context"));
    auto* native = static_cast<SslShell*>(pin.get())->native;
    native->socket = _hx_std_socket_native_handle(socket);
#ifdef _WIN32
    mbedtls_ssl_set_bio(&native->value,
        reinterpret_cast<void*>(native->socket), networkWrite, networkRead,
        nullptr);
#endif
    return nullptr;
}

::Dynamic _hx_ssl_set_hostname(::Dynamic ssl, ::String hostname) {
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "invalid TLS context"));
    const int result = mbedtls_ssl_set_hostname(
        &static_cast<SslShell*>(pin.get())->native->value, hostname.c_str());
    if (result != 0) sslFailure(result);
    return nullptr;
}

::Dynamic _hx_ssl_handshake(::Dynamic ssl) {
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "invalid TLS context"));
    BlockingScope blocking;
    const int result = mbedtls_ssl_handshake(
        &static_cast<SslShell*>(pin.get())->native->value);
    if (result != 0) checkIoResult(result, "TLS handshake failed");
    return nullptr;
}

::Dynamic _hx_ssl_close(::Dynamic ssl) {
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "closed TLS context"));
    auto* shell = static_cast<SslShell*>(pin.get());
    if (shell->native != nullptr) {
        {
            BlockingScope blocking;
            mbedtls_ssl_close_notify(&shell->native->value);
        }
        delete shell->native;
        shell->native = nullptr;
    }
    return nullptr;
}

int _hx_ssl_send(::Dynamic ssl, ::Array<unsigned char> bytes,
                 int position, int length) {
    if (!bytes) sslFailure("null TLS send buffer");
    PinnedBytes pinned(bytes);
    if (position < 0 || length < 0 || position > pinned.length ||
        length > pinned.length - position)
        sslFailure("invalid TLS send range");
    ::hx::ScopedPin sslPin(decodeShell<SslShell>(ssl, SslDescriptor,
                                                  "invalid TLS context"));
    BlockingScope blocking;
    const int result = mbedtls_ssl_write(
        &static_cast<SslShell*>(sslPin.get())->native->value,
        pinned.data + position, static_cast<std::size_t>(length));
    checkIoResult(result, "TLS send failed");
    return result;
}

::Dynamic _hx_ssl_send_char(::Dynamic ssl, int value) {
    if (value < 0 || value > 255) sslFailure("invalid TLS byte");
    unsigned char byte = static_cast<unsigned char>(value);
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "invalid TLS context"));
    BlockingScope blocking;
    const int result = mbedtls_ssl_write(
        &static_cast<SslShell*>(pin.get())->native->value, &byte, 1);
    checkIoResult(result, "TLS send failed");
    return nullptr;
}

::Dynamic _hx_ssl_write(::Dynamic ssl, ::Array<unsigned char> bytes) {
    if (!bytes) sslFailure("null TLS write buffer");
    PinnedBytes pinned(bytes);
    ::hx::ScopedPin sslPin(decodeShell<SslShell>(ssl, SslDescriptor,
                                                  "invalid TLS context"));
    int position = 0;
    while (position < pinned.length) {
        BlockingScope blocking;
        const int result = mbedtls_ssl_write(
            &static_cast<SslShell*>(sslPin.get())->native->value,
            pinned.data + position,
            static_cast<std::size_t>(pinned.length - position));
        checkIoResult(result, "TLS write failed");
        if (result == 0) sslFailure("TLS connection closed during write");
        position += result;
    }
    return nullptr;
}

int _hx_ssl_recv(::Dynamic ssl, ::Array<unsigned char> bytes,
                 int position, int length) {
    if (!bytes) sslFailure("null TLS receive buffer");
    PinnedBytes pinned(bytes);
    if (position < 0 || length < 0 || position > pinned.length ||
        length > pinned.length - position)
        sslFailure("invalid TLS receive range");
    ::hx::ScopedPin sslPin(decodeShell<SslShell>(ssl, SslDescriptor,
                                                  "invalid TLS context"));
    BlockingScope blocking;
    const int result = mbedtls_ssl_read(
        &static_cast<SslShell*>(sslPin.get())->native->value,
        pinned.data + position, static_cast<std::size_t>(length));
    if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        return 0;
    checkIoResult(result, "TLS receive failed");
    return result;
}

int _hx_ssl_recv_char(::Dynamic ssl) {
    unsigned char byte = 0;
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "invalid TLS context"));
    BlockingScope blocking;
    const int result = mbedtls_ssl_read(
        &static_cast<SslShell*>(pin.get())->native->value, &byte, 1);
    if (result <= 0) {
        if (result < 0) checkIoResult(result, "TLS receive failed");
        sslFailure("TLS end of stream");
    }
    return byte;
}

::Dynamic _hx_ssl_read(::Dynamic ssl) {
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "invalid TLS context"));
    std::vector<unsigned char> output;
    std::array<unsigned char, 4096> buffer{};
    for (;;) {
        int result;
        {
            BlockingScope blocking;
            result = mbedtls_ssl_read(
                &static_cast<SslShell*>(pin.get())->native->value,
                buffer.data(), buffer.size());
        }
        if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
            break;
        checkIoResult(result, "TLS read failed");
        output.insert(output.end(), buffer.begin(), buffer.begin() + result);
        if (result < static_cast<int>(buffer.size()))
            break;
    }
    return ::Dynamic(::Array_obj<unsigned char>::fromData(
        output.data(), static_cast<int>(output.size())));
}

::Dynamic _hx_ssl_get_peer_certificate(::Dynamic ssl) {
    ::hx::ScopedPin pin(decodeShell<SslShell>(ssl, SslDescriptor,
                                               "invalid TLS context"));
    const auto* peer = mbedtls_ssl_get_peer_cert(
        &static_cast<SslShell*>(pin.get())->native->value);
    if (peer == nullptr) return nullptr;
    auto owner = std::make_shared<CertificateChain>();
    const int result = mbedtls_x509_crt_parse_der(
        &owner->value, peer->raw.p, peer->raw.len);
    if (result != 0) sslFailure(result);
    return makeCertificate(std::move(owner));
}

::Dynamic _hx_ssl_cert_add_pem(::Dynamic certificate, ::String pem) {
    std::shared_ptr<CertificateChain> owner;
    if (certificate.isNull()) {
        owner = std::make_shared<CertificateChain>();
    } else {
        ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
            CertificateDescriptor, "invalid TLS certificate"));
        owner = static_cast<CertificateShell*>(pin.get())->native->owner;
    }
    std::vector<unsigned char> data(pem.view().begin(), pem.view().end());
    data.push_back(0);
    const int result = mbedtls_x509_crt_parse(
        &owner->value, data.data(), data.size());
    if (result < 0) sslFailure(result);
    return certificate.isNull() ? makeCertificate(std::move(owner)) : certificate;
}

::Dynamic _hx_ssl_cert_add_der(::Dynamic certificate,
                               ::Array<unsigned char> der) {
    if (!der) sslFailure("null DER certificate");
    std::shared_ptr<CertificateChain> owner;
    if (certificate.isNull()) {
        owner = std::make_shared<CertificateChain>();
    } else {
        ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
            CertificateDescriptor, "invalid TLS certificate"));
        owner = static_cast<CertificateShell*>(pin.get())->native->owner;
    }
    PinnedBytes pinned(der);
    const int result = mbedtls_x509_crt_parse_der(
        &owner->value, pinned.data, static_cast<std::size_t>(pinned.length));
    if (result < 0) sslFailure(result);
    return certificate.isNull() ? makeCertificate(std::move(owner)) : certificate;
}

::Dynamic _hx_ssl_cert_load_file(::String path) {
    auto owner = std::make_shared<CertificateChain>();
    const int result = mbedtls_x509_crt_parse_file(&owner->value, path.c_str());
    if (result != 0) sslFailure(result);
    return makeCertificate(std::move(owner));
}

::Dynamic _hx_ssl_cert_load_path(::String path) {
    auto owner = std::make_shared<CertificateChain>();
    const int result = mbedtls_x509_crt_parse_path(&owner->value, path.c_str());
    if (result != 0) sslFailure(result);
    return makeCertificate(std::move(owner));
}

::Dynamic _hx_ssl_cert_load_defaults() {
#ifdef _WIN32
    auto owner = std::make_shared<CertificateChain>();
    HCERTSTORE store = CertOpenSystemStoreW(0, L"ROOT");
    if (store == nullptr) return nullptr;
    PCCERT_CONTEXT certificate = nullptr;
    int loaded = 0;
    while ((certificate = CertEnumCertificatesInStore(store, certificate)) !=
           nullptr) {
        if (mbedtls_x509_crt_parse_der(&owner->value,
                certificate->pbCertEncoded, certificate->cbCertEncoded) == 0)
            ++loaded;
    }
    CertCloseStore(store, 0);
    return loaded == 0 ? ::Dynamic(nullptr) : makeCertificate(std::move(owner));
#else
    return nullptr;
#endif
}

::Dynamic _hx_ssl_cert_get_altnames(::Dynamic certificate) {
    ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
        CertificateDescriptor, "invalid TLS certificate"));
    auto* value = certificateValue(static_cast<CertificateShell*>(pin.get()));
    auto result = ::Array_obj<::String>::__new();
#if MBEDTLS_VERSION_MAJOR >= 3
    const bool hasAlternativeNames = mbedtls_x509_crt_has_ext_type(
        value, MBEDTLS_X509_EXT_SUBJECT_ALT_NAME);
#else
    const bool hasAlternativeNames =
        (value->ext_types & MBEDTLS_X509_EXT_SUBJECT_ALT_NAME) != 0;
#endif
    if (hasAlternativeNames) {
        for (auto* current = &value->subject_alt_names; current != nullptr;
             current = current->next)
            result->push(::String(reinterpret_cast<const char*>(current->buf.p),
                                  static_cast<int>(current->buf.len)));
    }
    return ::Dynamic(result);
}

::String _hx_ssl_cert_get_subject(::Dynamic certificate, ::String field) {
    ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
        CertificateDescriptor, "invalid TLS certificate"));
    return nameValue(&certificateValue(
        static_cast<CertificateShell*>(pin.get()))->subject, field);
}

::String _hx_ssl_cert_get_issuer(::Dynamic certificate, ::String field) {
    ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
        CertificateDescriptor, "invalid TLS certificate"));
    return nameValue(&certificateValue(
        static_cast<CertificateShell*>(pin.get()))->issuer, field);
}

::Array<int> _hx_ssl_cert_get_notbefore(::Dynamic certificate) {
    ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
        CertificateDescriptor, "invalid TLS certificate"));
    return certificateTime(certificateValue(
        static_cast<CertificateShell*>(pin.get()))->valid_from);
}

::Array<int> _hx_ssl_cert_get_notafter(::Dynamic certificate) {
    ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
        CertificateDescriptor, "invalid TLS certificate"));
    return certificateTime(certificateValue(
        static_cast<CertificateShell*>(pin.get()))->valid_to);
}

::Dynamic _hx_ssl_cert_get_next(::Dynamic certificate) {
    ::hx::ScopedPin pin(decodeShell<CertificateShell>(certificate,
        CertificateDescriptor, "invalid TLS certificate"));
    auto* native = static_cast<CertificateShell*>(pin.get())->native;
    return native->current->next == nullptr
        ? ::Dynamic(nullptr)
        : makeCertificate(native->owner, native->current->next);
}

::Dynamic _hx_ssl_key_from_der(::Array<unsigned char> data, bool isPublic) {
    if (!data) sslFailure("null DER key");
    ensureTls();
    auto* key = new NativeKey();
    PinnedBytes pinned(data);
    const int result = isPublic
        ? mbedtls_pk_parse_public_key(&key->value, pinned.data, pinned.length)
#if MBEDTLS_VERSION_MAJOR >= 3
        : mbedtls_pk_parse_key(&key->value, pinned.data, pinned.length,
            nullptr, 0, mbedtls_ctr_drbg_random, &tlsGlobals.random);
#else
        : mbedtls_pk_parse_key(&key->value, pinned.data, pinned.length,
            nullptr, 0);
#endif
    if (result != 0) {
        delete key;
        sslFailure(result);
    }
    return makeShell<KeyShell>(KeyDescriptor, key);
}

::Dynamic _hx_ssl_key_from_pem(::String data, bool isPublic, ::String pass) {
    ensureTls();
    auto* key = new NativeKey();
    std::vector<unsigned char> bytes(data.view().begin(), data.view().end());
    bytes.push_back(0);
    const auto password = pass.isNull() ? std::string() :
        std::string(pass.view());
    const int result = isPublic
        ? mbedtls_pk_parse_public_key(&key->value, bytes.data(), bytes.size())
#if MBEDTLS_VERSION_MAJOR >= 3
        : mbedtls_pk_parse_key(&key->value, bytes.data(), bytes.size(),
            password.empty() ? nullptr :
                reinterpret_cast<const unsigned char*>(password.data()),
            password.size(), mbedtls_ctr_drbg_random, &tlsGlobals.random);
#else
        : mbedtls_pk_parse_key(&key->value, bytes.data(), bytes.size(),
            password.empty() ? nullptr :
                reinterpret_cast<const unsigned char*>(password.data()),
            password.size());
#endif
    if (result != 0) {
        delete key;
        sslFailure(result);
    }
    return makeShell<KeyShell>(KeyDescriptor, key);
}

::Array<unsigned char> _hx_ssl_dgst_make(::Array<unsigned char> data,
                                         ::String algorithm) {
    if (!data) sslFailure("null digest input");
    PinnedBytes input(data);
    const auto* info = digestInfo(algorithm);
    std::array<unsigned char, MBEDTLS_MD_MAX_SIZE> digest{};
    const int status = mbedtls_md(info, input.data, input.length,
                                  digest.data());
    if (status != 0)
        sslFailure(status);
    return makeBytes(digest.data(), mbedtls_md_get_size(info));
}

::Array<unsigned char> _hx_ssl_dgst_sign(::Array<unsigned char> data,
                                         ::Dynamic key,
                                         ::String algorithm) {
    if (!data) sslFailure("null signature input");
    PinnedBytes input(data);
    const auto* info = digestInfo(algorithm);
    std::array<unsigned char, MBEDTLS_MD_MAX_SIZE> digest{};
    int status = mbedtls_md(info, input.data, input.length, digest.data());
    if (status != 0)
        sslFailure(status);

    ensureTls();
    ::hx::ScopedPin keyPin(decodeShell<KeyShell>(
        key, KeyDescriptor, "invalid TLS signing key"));
    auto* nativeKey = static_cast<KeyShell*>(keyPin.get())->native;
    std::array<unsigned char, MBEDTLS_PK_SIGNATURE_MAX_SIZE> signature{};
    std::size_t signatureLength = 0;
    status = mbedtls_pk_sign(&nativeKey->value, mbedtls_md_get_type(info),
                             digest.data(), mbedtls_md_get_size(info),
                             signature.data(),
#if MBEDTLS_VERSION_MAJOR >= 3
                             signature.size(),
#endif
                             &signatureLength, mbedtls_ctr_drbg_random,
                             &tlsGlobals.random);
    if (status != 0)
        sslFailure(status);
    return makeBytes(signature.data(), signatureLength);
}

bool _hx_ssl_dgst_verify(::Array<unsigned char> data,
                         ::Array<unsigned char> signature,
                         ::Dynamic key, ::String algorithm) {
    if (!data || !signature)
        sslFailure("null signature verification input");
    PinnedBytes input(data);
    PinnedBytes signatureInput(signature);
    const auto* info = digestInfo(algorithm);
    std::array<unsigned char, MBEDTLS_MD_MAX_SIZE> digest{};
    const int digestStatus = mbedtls_md(
        info, input.data, input.length, digest.data());
    if (digestStatus != 0)
        sslFailure(digestStatus);

    ::hx::ScopedPin keyPin(decodeShell<KeyShell>(
        key, KeyDescriptor, "invalid TLS verification key"));
    auto* nativeKey = static_cast<KeyShell*>(keyPin.get())->native;
    return mbedtls_pk_verify(&nativeKey->value, mbedtls_md_get_type(info),
                             digest.data(), mbedtls_md_get_size(info),
                             signatureInput.data,
                             signatureInput.length) == 0;
}
