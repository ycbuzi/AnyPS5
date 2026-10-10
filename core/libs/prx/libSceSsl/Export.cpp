#include "SslTypes.hpp"
#include "prx/libc/include/General.hpp"

#include <mbedtls/asn1.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/oid.h>
#include <mbedtls/pk.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr int ErrorNotFound = static_cast<int>(0x8095f004);
constexpr int ErrorInvalidArg = static_cast<int>(0x8095177a);

struct Pool {
    std::size_t size, current = 0, peak = 0;
    bool exhausted = false;
};

struct alignas(std::max_align_t) Allocation {
    Pool* pool;
    std::size_t size;
};

thread_local Pool* currentPool = nullptr;

void* PoolCalloc(std::size_t count, std::size_t size) noexcept {
    if (!currentPool) return nullptr;
    if (size && count > std::numeric_limits<std::size_t>::max() / size) {
        currentPool->exhausted = true;
        return nullptr;
    }
    const auto bytes = count * size;
    if (bytes > currentPool->size - currentPool->current || bytes > std::numeric_limits<std::size_t>::max() - sizeof(Allocation)) {
        currentPool->exhausted = true;
        return nullptr;
    }
    auto* allocation = static_cast<Allocation*>(std::calloc(1, sizeof(Allocation) + bytes));
    if (!allocation) {
        currentPool->exhausted = true;
        return nullptr;
    }
    allocation->pool = currentPool;
    allocation->size = bytes;
    currentPool->current += bytes;
    currentPool->peak = std::max(currentPool->peak, currentPool->current);
    return allocation + 1;
}

void PoolFree(void* pointer) noexcept {
    if (!pointer) return;
    auto* allocation = static_cast<Allocation*>(pointer) - 1;
    allocation->pool->current -= allocation->size;
    mbedtls_platform_zeroize(pointer, allocation->size);
    std::free(allocation);
}

struct PoolScope {
    Pool* previous;
    explicit PoolScope(Pool& pool) : previous(std::exchange(currentPool, &pool)) { pool.exhausted = false; }
    ~PoolScope() { currentPool = previous; }
};

struct PoolDeleter {
    void operator()(std::uint8_t* bytes) const { PoolFree(bytes); }
};

using Block = std::unique_ptr<std::uint8_t, PoolDeleter>;

Block Allocate(std::size_t size) {
    Block block(static_cast<std::uint8_t*>(PoolCalloc(1, std::max<std::size_t>(1, size))));
    if (!block) throw std::bad_alloc();
    return block;
}

std::size_t Add(std::size_t left, std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) throw std::length_error("SSL allocation size overflow");
    return left + right;
}

void CheckBackend(int error, const char* message) {
    if (error == 0) return;
    if (currentPool && currentPool->exhausted) throw std::bad_alloc();
    throw std::invalid_argument(message);
}

struct CertificateChain;

struct CertificateHandle {
    std::weak_ptr<CertificateChain> owner;
    const mbedtls_x509_crt* certificate;
};

struct CertificateChain {
    mbedtls_x509_crt root;
    std::vector<CertificateHandle*> handles;
    CertificateChain() { mbedtls_x509_crt_init(&root); }
    ~CertificateChain() { mbedtls_x509_crt_free(&root); }
};

struct PrivateKey {
    mbedtls_pk_context key;
    PrivateKey() { mbedtls_pk_init(&key); }
    ~PrivateKey() { mbedtls_pk_free(&key); }
};

struct Random {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    Random() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&drbg);
    }
    ~Random() {
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
    }
    void Seed() {
        CheckBackend(mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, nullptr, 0), "SSL random generator initialization failed");
    }
};

struct Identity {
    std::shared_ptr<CertificateChain> chain;
    PrivateKey key;
};

struct Snapshot {
    Block data;
    std::vector<std::shared_ptr<CertificateChain>> certificates;
    std::size_t count;
};

struct NameEntry {
    char* oid;
    std::size_t oidSize;
    std::uint8_t* value;
    std::size_t valueSize;
};

struct NameData {
    Block data;
    std::size_t count;
};

struct NameHandle {
    std::unique_ptr<NameData> name;
};

struct Context {
    Pool pool;
    std::vector<std::shared_ptr<CertificateChain>> authorities;
    std::shared_ptr<Identity> identity;
    std::vector<std::unique_ptr<CertificateHandle>> certificateHandles;
    std::vector<std::unique_ptr<NameHandle>> names;
    std::unordered_map<void*, Snapshot> caLists;
    std::unordered_map<void*, Snapshot> caCerts;
    explicit Context(std::size_t size) : pool{size} {}
};

struct Runtime {
    std::mutex mutex;
    std::unordered_map<int, std::unique_ptr<Context>> contexts;
    unsigned nextHandle = 1;
    Runtime() {
        if (mbedtls_platform_set_calloc_free(PoolCalloc, PoolFree) != 0)
            throw std::runtime_error("SSL allocator initialization failed");
    }
};

Runtime& State() {
    static auto* runtime = new Runtime;
    return *runtime;
}

Context& GetContext(int handle) {
    const auto found = State().contexts.find(handle);
    if (found == State().contexts.end()) throw std::invalid_argument("Invalid SSL context");
    return *found->second;
}

std::pair<std::shared_ptr<CertificateChain>, const mbedtls_x509_crt*> GetCertificate(Context& context, void* handle) {
    const auto found = std::find_if(context.certificateHandles.begin(), context.certificateHandles.end(),
        [&](const auto& entry) { return entry.get() == handle; });
    if (found == context.certificateHandles.end()) throw std::invalid_argument("Invalid SSL certificate handle");
    auto owner = (*found)->owner.lock();
    if (!owner) throw std::invalid_argument("Expired SSL certificate handle");
    return {std::move(owner), (*found)->certificate};
}

NameHandle& GetName(Context& context, void* handle) {
    const auto found = std::find_if(context.names.begin(), context.names.end(),
        [&](const auto& entry) { return entry.get() == handle; });
    if (found == context.names.end() || !(*found)->name) throw std::invalid_argument("Invalid SSL name handle");
    return **found;
}

struct Input {
    Block bytes;
    std::size_t size;
};

Input CopyInput(const SceSsl::Data& data) {
    if (!data.ptr || !data.size) throw std::invalid_argument("Empty SSL certificate/key data");
    auto bytes = Allocate(Add(data.size, 1));
    std::memcpy(bytes.get(), data.ptr, data.size);
    auto size = data.size;
    std::size_t first = 0;
    while (first < size && (bytes.get()[first] == ' ' || bytes.get()[first] == '\r' || bytes.get()[first] == '\n' || bytes.get()[first] == '\t')) ++first;
    const bool pem = first < size && bytes.get()[first] == '-';
    if (pem) {
        if (bytes.get()[size - 1] != 0) ++size;
        if (std::memchr(bytes.get(), 0, size - 1)) throw std::invalid_argument("Embedded null in SSL PEM data");
    } else {
        auto* position = bytes.get();
        std::size_t length = 0;
        CheckBackend(mbedtls_asn1_get_tag(&position, bytes.get() + size, &length, MBEDTLS_ASN1_CONSTRUCTED | MBEDTLS_ASN1_SEQUENCE),
            "Invalid SSL DER sequence");
        if (length != static_cast<std::size_t>(bytes.get() + size - position)) throw std::invalid_argument("Trailing SSL DER data");
    }
    return {std::move(bytes), size};
}

std::shared_ptr<CertificateChain> ParseCertificate(const SceSsl::Data& data) {
    auto chain = std::make_shared<CertificateChain>();
    const auto input = CopyInput(data);
    CheckBackend(mbedtls_x509_crt_parse(&chain->root, input.bytes.get(), input.size), "Invalid SSL certificate chain");
    return chain;
}

std::size_t CertificateCount(const Context& context) {
    std::size_t count = 0;
    for (const auto& chain : context.authorities) count = Add(count, chain->handles.size());
    return count;
}

void* CopyName(Context& context, const mbedtls_x509_name& name) {
    struct Entry {
        std::string oid;
        const mbedtls_x509_name* name;
    };
    std::vector<Entry> entries;
    std::size_t total = 0;
    for (auto* entry = &name; entry && entry->oid.p; entry = entry->next) {
        if (entry->oid.len > (std::numeric_limits<std::size_t>::max() - 32) / 4) throw std::length_error("SSL name OID size overflow");
        std::string oid(entry->oid.len * 4 + 32, '\0');
        const auto length = mbedtls_oid_get_numeric_string(oid.data(), oid.size(), &entry->oid);
        if (length < 0) throw std::runtime_error("Unsupported SSL name OID");
        oid.resize(static_cast<std::size_t>(length));
        total = Add(total, Add(sizeof(NameEntry), Add(oid.size() + 1, entry->val.len)));
        entries.push_back({std::move(oid), entry});
    }
    auto handle = std::make_unique<NameHandle>();
    handle->name = std::make_unique<NameData>();
    handle->name->data = Allocate(total);
    handle->name->count = entries.size();
    auto* output = reinterpret_cast<NameEntry*>(handle->name->data.get());
    auto* next = handle->name->data.get() + entries.size() * sizeof(NameEntry);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const auto& entry = entries[index];
        output[index].oid = reinterpret_cast<char*>(next);
        output[index].oidSize = entry.oid.size();
        std::memcpy(next, entry.oid.c_str(), entry.oid.size() + 1);
        next += entry.oid.size() + 1;
        output[index].value = next;
        output[index].valueSize = entry.name->val.len;
        std::memcpy(next, entry.name->val.p, entry.name->val.len);
        next += entry.name->val.len;
    }
    auto* result = handle.get();
    context.names.push_back(std::move(handle));
    return result;
}

}

extern "C" {

int APS5_VABI sceSslInit_nid_postfix(std::uint64_t poolSize) {
    if (!poolSize) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    if (state.nextHandle > static_cast<unsigned>(std::numeric_limits<int>::max())) throw std::overflow_error("SSL context handles exhausted");
    const auto handle = static_cast<int>(state.nextHandle);
    state.contexts.emplace(handle, std::make_unique<Context>(poolSize));
    ++state.nextHandle;
    return handle;
}

int APS5_VABI sceSslTerm_nid_postfix(int sslCtxId) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    GetContext(sslCtxId);
    state.contexts.erase(sslCtxId);
    return 0;
}

int APS5_VABI sceSslGetMemoryPoolStats(int sslCtxId, SceSsl::MemoryPoolStats* stats) {
    if (!stats) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto& pool = GetContext(sslCtxId).pool;
    *stats = {pool.size, pool.peak, pool.current, 0};
    return 0;
}

int APS5_VABI sceSslLoadCert(int sslCtxId, int caCertNum, SceSsl::Data** caList, SceSsl::Data* cert, SceSsl::Data* privateKey) {
    if (caCertNum < 0 || (caCertNum && !caList) || (!!cert != !!privateKey)) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    PoolScope scope(context.pool);
    std::vector<std::shared_ptr<CertificateChain>> authorities;
    std::vector<std::unique_ptr<CertificateHandle>> handles;
    for (int index = 0; index < caCertNum; ++index) {
        if (!caList[index]) return ErrorInvalidArg;
        auto chain = ParseCertificate(*caList[index]);
        for (auto* entry = &chain->root; entry; entry = entry->next) {
            auto handle = std::make_unique<CertificateHandle>(CertificateHandle{chain, entry});
            chain->handles.push_back(handle.get());
            handles.push_back(std::move(handle));
        }
        authorities.push_back(std::move(chain));
    }
    std::shared_ptr<Identity> identity;
    if (cert) {
        identity = std::make_shared<Identity>();
        identity->chain = ParseCertificate(*cert);
        const auto key = CopyInput(*privateKey);
        Random random;
        random.Seed();
        CheckBackend(mbedtls_pk_parse_key(&identity->key.key, key.bytes.get(), key.size, nullptr, 0,
            mbedtls_ctr_drbg_random, &random.drbg), "Invalid SSL private key");
        CheckBackend(mbedtls_pk_check_pair(&identity->chain->root.pk, &identity->key.key, mbedtls_ctr_drbg_random, &random.drbg),
            "SSL client certificate and private key do not match");
    }
    context.authorities.reserve(Add(context.authorities.size(), authorities.size()));
    context.certificateHandles.reserve(Add(context.certificateHandles.size(), handles.size()));
    for (auto& chain : authorities) context.authorities.push_back(std::move(chain));
    for (auto& handle : handles) context.certificateHandles.push_back(std::move(handle));
    if (identity) context.identity = std::move(identity);
    return 0;
}

int APS5_VABI sceSslUnloadCert(int sslCtxId) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    context.authorities.clear();
    context.identity.reset();
    return 0;
}

int APS5_VABI sceSslGetCaList(int sslCtxId, SceSsl::CaList* output) {
    if (!output) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    PoolScope scope(context.pool);
    const auto count = CertificateCount(context);
    if (!count) {
        *output = {};
        return ErrorNotFound;
    }
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Too many SSL authorities");
    Snapshot snapshot{Allocate(count * sizeof(void*)), context.authorities, count};
    auto** pointers = reinterpret_cast<void**>(snapshot.data.get());
    std::size_t index = 0;
    for (const auto& chain : snapshot.certificates) for (auto* handle : chain->handles) pointers[index++] = handle;
    context.caLists.emplace(pointers, std::move(snapshot));
    *output = {pointers, static_cast<int>(count)};
    return 0;
}

int APS5_VABI sceSslFreeCaList(int sslCtxId, SceSsl::CaList* list) {
    if (!list) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    if (!list->certs && list->num == 0) return 0;
    const auto found = context.caLists.find(list->certs);
    if (found == context.caLists.end() || list->num < 0 || static_cast<std::size_t>(list->num) != found->second.count)
        throw std::invalid_argument("Invalid SSL CA list allocation");
    context.caLists.erase(found);
    *list = {};
    return 0;
}

int APS5_VABI sceSslGetCaCerts(int sslCtxId, SceSsl::CaCerts* output) {
    if (!output) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    PoolScope scope(context.pool);
    const auto count = CertificateCount(context);
    if (!count) {
        *output = {};
        return ErrorNotFound;
    }
    if (count > std::numeric_limits<std::size_t>::max() / sizeof(SceSsl::Data)) throw std::length_error("Too many SSL authorities");
    auto total = count * sizeof(SceSsl::Data);
    for (const auto& chain : context.authorities)
        for (auto* entry = &chain->root; entry; entry = entry->next) total = Add(total, entry->raw.len);
    Snapshot snapshot{Allocate(total), {}, count};
    auto* data = reinterpret_cast<SceSsl::Data*>(snapshot.data.get());
    auto* next = snapshot.data.get() + count * sizeof(SceSsl::Data);
    std::size_t index = 0;
    for (const auto& chain : context.authorities) {
        for (auto* entry = &chain->root; entry; entry = entry->next) {
            data[index++] = {next, entry->raw.len};
            std::memcpy(next, entry->raw.p, entry->raw.len);
            next += entry->raw.len;
        }
    }
    context.caCerts.emplace(data, std::move(snapshot));
    *output = {data, count, data};
    return 0;
}

int APS5_VABI sceSslFreeCaCerts(int sslCtxId, SceSsl::CaCerts* certificates) {
    if (!certificates) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    if (!certificates->certs && !certificates->num && !certificates->pool) return 0;
    const auto found = context.caCerts.find(certificates->pool);
    if (found == context.caCerts.end() || certificates->certs != certificates->pool || certificates->num != found->second.count)
        throw std::invalid_argument("Invalid SSL CA certificate allocation");
    context.caCerts.erase(found);
    *certificates = {};
    return 0;
}

void* APS5_VABI sceSslGetSubjectName(int sslCtxId, void* certificate) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    const auto [owner, parsed] = GetCertificate(context, certificate);
    PoolScope scope(context.pool);
    return CopyName(context, parsed->subject);
}

void* APS5_VABI sceSslGetIssuerName(int sslCtxId, void* certificate) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    auto& context = GetContext(sslCtxId);
    const auto [owner, parsed] = GetCertificate(context, certificate);
    PoolScope scope(context.pool);
    return CopyName(context, parsed->issuer);
}

int APS5_VABI sceSslFreeSslCertName(int sslCtxId, void* name) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    GetName(GetContext(sslCtxId), name).name.reset();
    return 0;
}

int APS5_VABI sceSslGetNameEntryCount(int sslCtxId, void* name) {
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto count = GetName(GetContext(sslCtxId), name).name->count;
    if (count > static_cast<std::size_t>(std::numeric_limits<int>::max())) throw std::length_error("Too many SSL name entries");
    return static_cast<int>(count);
}

int APS5_VABI sceSslGetNameEntryInfo(int sslCtxId, void* name, int index, char* oid, std::size_t oidCapacity,
    std::uint8_t* value, std::size_t valueCapacity, std::size_t* valueLength) {
    if (!valueLength || (!oid && oidCapacity) || (!value && valueCapacity)) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto& parsed = *GetName(GetContext(sslCtxId), name).name;
    if (index < 0 || static_cast<std::size_t>(index) >= parsed.count) return ErrorInvalidArg;
    const auto& entry = reinterpret_cast<const NameEntry*>(parsed.data.get())[index];
    *valueLength = entry.valueSize;
    if ((oid && oidCapacity <= entry.oidSize) || (value && valueCapacity < entry.valueSize))
        throw std::length_error("SSL name output buffer is too small");
    if (oid) std::memcpy(oid, entry.oid, entry.oidSize + 1);
    if (value) {
        std::memcpy(value, entry.value, entry.valueSize);
        if (valueCapacity > entry.valueSize) value[entry.valueSize] = 0;
    }
    return 0;
}

int APS5_VABI sceSslGetSerialNumber(int sslCtxId, void* certificate, std::uint8_t* bytes, std::size_t* length) {
    if (!length) return ErrorInvalidArg;
    auto& state = State();
    std::lock_guard lock(state.mutex);
    const auto [owner, parsed] = GetCertificate(GetContext(sslCtxId), certificate);
    const auto capacity = *length;
    *length = parsed->serial.len;
    if (bytes && capacity < parsed->serial.len) throw std::length_error("SSL serial output buffer is too small");
    if (bytes) std::memcpy(bytes, parsed->serial.p, parsed->serial.len);
    return 0;
}

int APS5_VABI sceSslClose() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

int APS5_VABI sceSslGetPem() {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}

}
