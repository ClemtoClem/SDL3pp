#ifndef SDL3PP_RESOURCES_H_
#define SDL3PP_RESOURCES_H_

/**
 * @file SDL3pp_resources.h
 * @brief Async resource pool manager for SDL3pp.
 *
 * ## Architecture
 *
 * ```
 * ResourceManager  (singleton-friendly, owns all pools)
 *   └─ ResourcePool  (named, e.g. "game_1")
 *        └─ ResourceEntry<T>  (ref-counted, keyed by string)
 * ```
 *
 * ### Loading model
 *
 * Resources can be loaded in two ways:
 *
 * 1. **Synchronously** – `Pool::Add<T>(key, value)` stores an already-built
 *    resource.
 * 2. **Asynchronously** – `Pool::LoadAsync<T>(key, path, factory)` issues a
 *    `SDL::LoadFileAsync` call on a shared `SDL::AsyncIOQueue`.  A background
 *    thread (one per pool) blocks on `AsyncIOQueue::WaitResult()`, invokes the
 *    user-supplied *factory* to turn the raw bytes into a `T`, and stores the
 *    result.  The loading thread can be cancelled at any time.
 *
 * ### Factory signature
 *
 * ```cpp
 * // Returns the fully constructed resource (moved into the pool).
 * T factory(const char* key, void* buffer, size_t bytes);
 * ```
 *
 * ### Progress / cancellation
 *
 * ```cpp
 * pool->LoadingProgress()   // [0.0, 1.0]
 * pool->IsLoading()         // true while the background thread is alive
 * pool->CancelLoading()     // request stop + join
 * pool->WaitLoading()       // block until all pending tasks finish
 * ```
 *
 * ### Ref-counting & lifetime
 *
 * Every `Get<T>()` call returns a `ResourceHandle<T>` – a thin shared_ptr
 * wrapper.  The underlying `T` is freed when **all** handles are released **and**
 * the pool entry itself is gone (pool release or explicit remove).
 *
 * ### Fast string access
 *
 * Resources are stored in a `std::unordered_map<std::string, …>` for O(1)
 * average-case lookup by name.
 *
 * ### Clean shutdown
 *
 * `ResourcePool::Release()` stops the loading thread, drains remaining async
 * results, and destroys every entry (waiting for external ref-counts to drop
 * to 1 is the caller's responsibility – the map is simply cleared).
 * `ResourceManager::ReleaseAll()` calls Release() on every pool.
 */

#include <atomic>
#include <format>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "SDL3pp_asyncio.h"
#include "SDL3pp_log.h"
#include "SDL3pp_stdinc.h"

namespace SDL {

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────

class ResourcePool;
struct ResourcePoolPtr;
class ResourceManager;

// ─────────────────────────────────────────────────────────────────────────────
// ResourceHandle<T>  –  ref-counted handle returned to callers
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A ref-counted handle to a resource of type T stored inside a pool.
 *
 * Behaves like a std::shared_ptr: copyable, movable, comparable to nullptr.
 * The resource is kept alive as long as at least one handle (or the pool
 * entry) holds a reference.
 */
template<class T>
class ResourceHandle {
public:
  ResourceHandle() = default;
  explicit ResourceHandle(std::shared_ptr<T> p) : m_ptr(std::move(p)) {}

  T*       get()       noexcept { return m_ptr.get(); }
  const T* get() const noexcept { return m_ptr.get(); }
  T&       operator*()        { return *m_ptr; }
  const T& operator*() const  { return *m_ptr; }
  T*       operator->()       noexcept { return m_ptr.get(); }
  const T* operator->() const noexcept { return m_ptr.get(); }

  explicit operator bool() const noexcept { return m_ptr != nullptr; }
  bool     operator==(std::nullptr_t) const noexcept { return !m_ptr; }
  bool     operator!=(std::nullptr_t) const noexcept { return  m_ptr; }

  /// Number of handles (including the pool's own entry) sharing this resource.
  long use_count() const noexcept { return m_ptr.use_count(); }

  void Reset() { m_ptr.reset(); }

private:
  std::shared_ptr<T> m_ptr;
};

// ─────────────────────────────────────────────────────────────────────────────
// ResourceEntryBase  –  type-erased base stored in the map
// ─────────────────────────────────────────────────────────────────────────────

struct ResourceEntryBase {
  std::string    key;
  std::type_index typeId;
  bool           ready = false; ///< true once async load succeeded

  explicit ResourceEntryBase(std::string k, std::type_index t)
    : key(std::move(k)), typeId(t) {}

  virtual ~ResourceEntryBase() = default;
};

// ─────────────────────────────────────────────────────────────────────────────
// ResourceEntry<T>  –  typed entry holding a shared_ptr<T>
// ─────────────────────────────────────────────────────────────────────────────

template<class T>
struct ResourceEntry : ResourceEntryBase {
  std::shared_ptr<T> resource;

  ResourceEntry(std::string k, std::shared_ptr<T> r)
    : ResourceEntryBase(std::move(k), typeid(T))
    , resource(std::move(r)) {
    ready = (resource != nullptr);
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// PendingLoad  –  describes one async load task in flight
// ─────────────────────────────────────────────────────────────────────────────

struct PendingLoad {
  std::string   key;
  std::function<void(const char* key, void* buf, size_t bytes,
                     ResourceEntryBase& entry)> finalize;
};

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A named collection of resources, optionally loaded asynchronously.
 *
 * Typical usage:
 * ```cpp
 * auto pool = rm->CreatePool("level_1");
 *
 * // Sync add of an already-created resource
 * pool->Add<SDL::Texture>("ui_font", myTexture);
 *
 * // Async load from file – factory converts raw bytes → T
 * pool->LoadAsync<SDL::Texture>("hero", path,
 *   [&](const char*, void* buf, size_t n) {
 *     SDL::Surface surf{
 *        SDL::IOFromConstMem({buf, n})
 *     };
 *     return SDL::CreateTextureFromSurface(renderer, surf);
 *   });
 *
 * // Main-loop: pump results (call every frame)
 * pool->Update();
 *
 * // Retrieve
 * auto tex = pool->Get<SDL::Texture>("hero");
 * if (tex) { renderer.RenderTexture(*tex, …, …); }
 *
 * // Cleanup
 * pool->Release();
 * ```
 */
class ResourcePool {
public:
  // ── Construction ──────────────────────────────────────────────────────────

  explicit ResourcePool(std::string name)
    : m_name(std::move(name)) {
  }

  ~ResourcePool() { Release(); }

  ResourcePool(const ResourcePool&)            = delete;
  ResourcePool& operator=(const ResourcePool&) = delete;
  ResourcePool(ResourcePool&&)                 = delete;
  ResourcePool& operator=(ResourcePool&&)      = delete;

  // ── Name ──────────────────────────────────────────────────────────────────

  const std::string& GetName() const noexcept { return m_name; }

  // ─────────────────────────────────────────────────────────────────────────
  // Synchronous Add
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Store an already-constructed resource under `key`.
   *
   * If a resource with the same key and type already exists, the existing
   * entry is silently replaced.
   *
   * @param key   Unique name within this pool.
   * @param value The resource value (moved in).
   */
  template<class T>
  void Add(const std::string& key, T value) {
    auto entry = std::make_unique<ResourceEntry<T>>(
      key, std::make_shared<T>(std::move(value)));

    std::lock_guard lock(m_mutex);
    m_resources[key] = std::move(entry);
  }
  
  /*template<class T, typename U>
  void Add(const std::string& key, U&& value) {
    auto resourcePtr = std::make_shared<T>(std::forward<U>(value));
    
    auto entry = std::make_unique<ResourceEntry<T>>(
      key, std::move(resourcePtr)
    );

    std::lock_guard lock(m_mutex);
    m_resources[key] = std::move(entry);
  }*/

  // ─────────────────────────────────────────────────────────────────────────
  // Asynchronous LoadAsync
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Schedule an async file load for a resource of type T.
   *
   * The `factory` callable is invoked on the **loading thread** once the file
   * data is available.  It receives the raw bytes and must return a fully
   * constructed `T` (moved into the pool).
   *
   * Factory signature:
   * ```cpp
   * T factory(const char* key, void* buffer, size_t bytes_transferred);
   * ```
   *
   * @param key     Resource name.
   * @param path    File path passed to `SDL::LoadFileAsync`.
   * @param factory Conversion function raw-bytes → T.
   */
  template<class T, class Factory>
  void LoadAsync(const std::string& key, const std::string& path,
                 Factory&& factory) {
    // Create a placeholder entry (not ready yet)
    {
      std::lock_guard lock(m_mutex);
      if (m_resources.count(key)) {
        // Already loaded – skip.
        return;
      }
      auto placeholder = std::make_unique<ResourceEntry<T>>(key, nullptr);
      placeholder->ready = false;
      m_resources[key] = std::move(placeholder);
      m_pendingCount.fetch_add(1, std::memory_order_relaxed);
      m_totalQueued.fetch_add(1, std::memory_order_relaxed);
    }

    // Capture finalize logic (type-erased into PendingLoad)
    PendingLoad pending;
    pending.key = key;

    // We capture factory by value so it survives the async gap.
    auto factoryCopy = std::forward<Factory>(factory);
    pending.finalize = [factoryCopy = std::move(factoryCopy)](
                         const char* k, void* buf, size_t bytes,
                         ResourceEntryBase& baseEntry) mutable {
      auto& typedEntry = static_cast<ResourceEntry<T>&>(baseEntry);
      try {
        T result = factoryCopy(k, buf, bytes);
        typedEntry.resource = std::make_shared<T>(std::move(result));
        typedEntry.ready    = true;
      } catch (const std::exception& e) {
        SDL::LogError(SDL::LOG_CATEGORY_APPLICATION,
                      "ResourcePool: factory failed for '%s': %s", k, e.what());
      }
    };

    // Queue the SDL async read.
    // We pass a heap-allocated PendingLoad* as userdata so the worker thread
    // can reconstruct which key/finalize matches this outcome.
    auto* userData = new PendingLoad(std::move(pending));
    SDL::LoadFileAsync(path, m_queue, static_cast<void*>(userData));

    // Ensure the background worker is running.
    EnsureWorkerRunning();
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Update  –  pump async results on the calling thread (main-thread safe)
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Drain completed async I/O results without blocking.
   *
   * Call this once per frame (or whenever convenient) from the **main thread**
   * if you cannot call it from the loading thread.  If you use LoadAsync() the
   * background worker handles draining automatically; Update() is an
   * additional optional polling point.
   */
  void Update() {
    while (auto outcome = m_queue.GetResult()) {
      HandleOutcome(*outcome);
    }
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Get
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Retrieve a resource by key.
   *
   * Returns an empty handle if the key does not exist, the type does not
   * match, or the resource has not finished loading yet.
   *
   * @param key Resource name.
   * @returns A ResourceHandle<T>; falsy if not found / not ready.
   */
  template<class T>
  ResourceHandle<T> Get(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(key);
    if (it == m_resources.end()) return {};
    auto* base = it->second.get();
    if (base->typeId != typeid(T)) return {};
    auto* entry = static_cast<ResourceEntry<T>*>(base);
    if (!entry->ready) return {};
    return ResourceHandle<T>{entry->resource};
  }

  /**
   * Returns the raw pointer to a resource (no ref-count bump).
   *
   * Faster than Get<T> but the pointer may become dangling if the pool or
   * entry is released while you hold it.  Prefer Get<T>() for safety.
   */
  template<class T>
  T* GetRaw(const std::string& key) const {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(key);
    if (it == m_resources.end()) return nullptr;
    auto* base = it->second.get();
    if (base->typeId != typeid(T)) return nullptr;
    auto* entry = static_cast<ResourceEntry<T>*>(base);
    return entry->ready ? entry->resource.get() : nullptr;
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Remove
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Explicitly remove a resource entry.
   *
   * If external ResourceHandle<T> instances still hold a reference, the
   * underlying T won't be freed until those handles are released too.
   */
  void Remove(const std::string& key) {
    std::lock_guard lock(m_mutex);
    m_resources.erase(key);
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Progress / state
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Loading progress in [0.0, 1.0].
   *
   * Returns 1.0 if nothing has been queued, or once all async tasks are done.
   */
  float LoadingProgress() const noexcept {
    int total   = m_totalQueued.load(std::memory_order_relaxed);
    int pending = m_pendingCount.load(std::memory_order_relaxed);
    if (total == 0) return 1.0f;
    int done = total - pending;
    return static_cast<float>(done) / static_cast<float>(total);
  }

  /// True while the background loading thread is alive.
  bool IsLoading() const noexcept {
    return m_worker.joinable() &&
           m_pendingCount.load(std::memory_order_relaxed) > 0;
  }

  /// Number of resources currently in this pool (including not-yet-ready ones).
  size_t Size() const {
    std::lock_guard lock(m_mutex);
    return m_resources.size();
  }

  /// True if the pool contains no resources.
  bool IsEmpty() const {
    std::lock_guard lock(m_mutex);
    return m_resources.empty();
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Cancellation / Wait
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Request the background loading thread to stop.
   *
   * In-flight I/O tasks that already reached the OS cannot be recalled, but
   * their results will be discarded rather than finalised.  This call blocks
   * until the thread exits.
   */
  void CancelLoading() {
    m_cancelRequested.store(true, std::memory_order_relaxed);
    m_queue.Signal(); // wake the blocking WaitResult()
    JoinWorker();
    // Drain leftover outcomes so memory is not leaked.
    DrainDiscardAll();
  }

  /**
   * Block the calling thread until all pending async loads have completed.
   *
   * The background worker will still be running; this just waits for
   * `LoadingProgress() == 1.0`.
   */
  void WaitLoading() {
    while (m_pendingCount.load(std::memory_order_relaxed) > 0 &&
           m_worker.joinable()) {
      SDL_Delay(1); // yield; a condition_variable would be marginally better
    }
  }

  // ─────────────────────────────────────────────────────────────────────────
  // Release
  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Stop all loading, free all entries, and reset counters.
   *
   * After calling Release() the pool is usable again (you can load new
   * resources into it).
   */
  void Release() {
    CancelLoading();
    std::lock_guard lock(m_mutex);
    m_resources.clear();
    m_totalQueued.store(0, std::memory_order_relaxed);
    m_pendingCount.store(0, std::memory_order_relaxed);
    m_cancelRequested.store(false, std::memory_order_relaxed);
  }

  // ─────────────────────────────────────────────────────────────────────────
private:
  // ─────────────────────────────────────────────────────────────────────────

  std::string m_name;

  // Resource map: key → typed entry
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, std::unique_ptr<ResourceEntryBase>> m_resources;

  // Async I/O
  SDL::AsyncIOQueue m_queue;
  std::atomic<int>  m_pendingCount{0};
  std::atomic<int>  m_totalQueued{0};
  std::atomic<bool> m_cancelRequested{false};

  // Background thread
  std::thread m_worker;

  // ── Worker management ─────────────────────────────────────────────────────

  void EnsureWorkerRunning() {
    if (!m_worker.joinable()) {
      m_cancelRequested.store(false, std::memory_order_relaxed);
      m_worker = std::thread([this] { WorkerLoop(); });
    }
  }

  void JoinWorker() {
    if (m_worker.joinable()) {
      m_worker.join();
    }
  }

  void WorkerLoop() {
    while (!m_cancelRequested.load(std::memory_order_relaxed)) {
      // Block until a result arrives or Signal() is called.
      auto outcome = m_queue.WaitResult();
      if (!outcome) {
        // Signal() woke us up; recheck cancel flag.
        continue;
      }
      HandleOutcome(*outcome);

      if (m_pendingCount.load(std::memory_order_relaxed) <= 0) {
        // All tasks done; thread exits naturally.
        break;
      }
    }
  }

  // ── Outcome processing ────────────────────────────────────────────────────

  void HandleOutcome(SDL::AsyncIOOutcome& outcome) {
    auto* pending = static_cast<PendingLoad*>(outcome.userdata);
    if (!pending) return;

    if (outcome.result == SDL::ASYNCIO_COMPLETE &&
        !m_cancelRequested.load(std::memory_order_relaxed)) {
      std::lock_guard lock(m_mutex);
      auto it = m_resources.find(pending->key);
      if (it != m_resources.end()) {
        pending->finalize(pending->key.c_str(),
                          outcome.buffer,
                          static_cast<size_t>(outcome.bytes_transferred),
                          *it->second);
      }
    } else if (outcome.result == SDL::ASYNCIO_FAILURE) {
      SDL::LogError(SDL::LOG_CATEGORY_APPLICATION,
                    "ResourcePool '%s': async load failed for '%s'",
                    m_name.c_str(), pending ? pending->key.c_str() : "?");
    }

    SDL::Free(outcome.buffer);
    delete pending;

    m_pendingCount.fetch_sub(1, std::memory_order_acq_rel);
  }

  void DrainDiscardAll() {
    // Non-blocking drain after cancel.
    while (auto outcome = m_queue.GetResult()) {
      auto* pending = static_cast<PendingLoad*>(outcome->userdata);
      SDL::Free(outcome->buffer);
      delete pending;
      m_pendingCount.fetch_sub(1, std::memory_order_acq_rel);
    }
  }
};

struct ResourcePoolPtr {
  ResourcePool* pool;

  /**
   * A non-owning reference to a ResourcePool, used for passing pools around
   * without transferring ownership.  This is just a thin wrapper around a raw
   */
  ResourcePoolPtr() : pool(nullptr) {}

  /**
   * Construct from a raw pointer.  This does not take ownership; the caller is
   * responsible for ensuring the pointer remains valid while the ResourcePoolPtr is used.
   */
  explicit ResourcePoolPtr(ResourcePool* p) : pool(p) {}

  /**
   * Allow implicit conversion from ResourcePool* to ResourcePoolPtr for convenience.
   * This does not take ownership; the caller is responsible for ensuring the pointer remains valid while the ResourcePoolPtr is used.
   */
  ResourcePool* operator->() { return pool; }
  const ResourcePool* operator->() const { return pool; }

  explicit operator bool() const noexcept { return pool != nullptr; }
};

// ─────────────────────────────────────────────────────────────────────────────
// ResourceManager
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Top-level manager that owns a collection of named ResourcePool objects.
 *
 * ```cpp
 * SDL::ResourceManager rm;
 * auto pool1 = rm.CreatePool("level_1");
 * auto pool2 = rm.CreatePool("level_2");
 *
 * pool1->LoadAsync<SDL::Texture>(...);
 * // ...
 * pool1->Release();    // or rm.ReleaseAll() at shutdown
 * ```
 */
class ResourceManager {
public:
  ResourceManager()  = default;
  ~ResourceManager() { ReleaseAll(); }

  ResourceManager(const ResourceManager&)            = delete;
  ResourceManager& operator=(const ResourceManager&) = delete;

  // ─────────────────────────────────────────────────────────────────────────

  /**
   * Create a new pool with the given name, or return the existing one if a
   * pool with that name already exists.
   *
   * @returns Raw (non-owning) pointer to the pool.  Ownership stays with the
   *          ResourceManager.
   */
  ResourcePool* CreatePool(const std::string& name) {
    std::lock_guard lock(m_mutex);
    auto it = m_pools.find(name);
    if (it != m_pools.end()) return it->second.get();
    auto pool = std::make_unique<ResourcePool>(name);
    auto* ptr = pool.get();
    m_pools.emplace(name, std::move(pool));
    return ptr;
  }

  /**
   * Look up an existing pool by name.
   * @returns nullptr if not found.
   */
  ResourcePool* GetPool(const std::string& name) const {
    std::lock_guard lock(m_mutex);
    auto it = m_pools.find(name);
    return it != m_pools.end() ? it->second.get() : nullptr;
  }

  /**
   * Destroy a pool (releases all resources and stops its loading thread).
   */
  void DestroyPool(const std::string& name) {
    std::lock_guard lock(m_mutex);
    m_pools.erase(name);
  }

  /**
   * Release all pools.  Call at application shutdown.
   */
  void ReleaseAll() {
    std::lock_guard lock(m_mutex);
    m_pools.clear();
  }

  /**
   * Update all pools (pump async results).  Call once per frame if you cannot
   */
  void UpdateAll() {
    std::lock_guard lock(m_mutex);
    for (auto& [name, pool] : m_pools) {
      pool->Update();
    }
  }

  /// Number of pools currently managed.
  size_t PoolCount() const {
    std::lock_guard lock(m_mutex);
    return m_pools.size();
  }

  /**
   * Overall loading progress across all pools, averaged by task count.
   * Returns 1.0 if all pools are idle.
   */
  float TotalLoadingProgress() const {
    std::lock_guard lock(m_mutex);
    if (m_pools.empty()) return 1.0f;
    float sum = 0.0f;
    for (auto& [name, pool] : m_pools) sum += pool->LoadingProgress();
    return sum / static_cast<float>(m_pools.size());
  }

  /// True if any pool is still loading.
  bool IsLoading() const {
    std::lock_guard lock(m_mutex);
    for (auto& [name, pool] : m_pools) {
      if (pool->IsLoading()) return true;
    }
    return false;
  }

private:
  mutable std::mutex m_mutex;
  std::unordered_map<std::string, std::unique_ptr<ResourcePool>> m_pools;
};

} // namespace SDL

#endif // SDL3PP_RESOURCES_H_