#ifndef __KVPAIRLOADER_H_
#define __KVPAIRLOADER_H_

#include <vector>
#include <unordered_map>
#include <utility>
#include <cstdint>
#include <stdexcept>
#include "KVPair.h"

using namespace std;

typedef uint64_t region_id;
typedef uint64_t offset;
typedef uint64_t chunk_id;
typedef vector<KVPair> chunk;

class KVPairLoader {
public:
  KVPairLoader(int _reducerId, vector<pair<region_id, offset>>& _chunkPtrs)
    : reducerId(_reducerId), chunkPtrs(_chunkPtrs) {
    size = load(reducerId);
  }
  virtual ~KVPairLoader() {}
  /**
   * load the whole chunks in memory as kv pairs.
   */
  size_t load(int reducerId);
  /**
   * fetch the number of kv pairs.
   * Note: we should call `load` before this method.
   */
  virtual vector<KVPair> fetch(int num) {
    throw new domain_error("not valid here.");
  }
  virtual vector<vector<KVPair>> fetchAggregatedPairs(int num) {
    throw new domain_error("not valid here.");
  }

protected:
  const int reducerId;
  vector<pair<region_id, offset>>& chunkPtrs; //index chunk pointers.
  vector<pair<chunk_id, chunk>> dataChunks;
  uint64_t size {0};
  byte* dropUntil(int partitionId, byte* indexChunkPtr);
};

class PassThroughLoader : public KVPairLoader {
public:
  PassThroughLoader(int _reducerId, vector<pair<region_id, offset>>& _chunkPtrs)
    : KVPairLoader(_reducerId, _chunkPtrs) {
    flatten();
  }
  ~PassThroughLoader() {
    for (KVPair& pair : flatChunk) {
      delete [] pair.getSerKey();
      delete [] pair.getSerValue();
    }
  }

  vector<KVPair> fetch(int num) override;

private:
  vector<KVPair> flatChunk;
  void flatten();
};

class HashMapLoader : public KVPairLoader {
public:
  HashMapLoader(int _reducerId, vector<pair<region_id, offset>>& _chunkPtrs)
    : KVPairLoader(_reducerId, _chunkPtrs) {
  }
  ~HashMapLoader() {
    delete hashmap;
  }

  void prepare(JNIEnv* env) override;
  vector<vector<KVPair>> fetchAggregatedPairs(int num) override;
private:
  void aggregate(JNIEnv* env);

  // implement Equal-related functors here.
  struct EqualTo {
  public:
    EqualTo(JNIEnv* env) : env(env) {}
    ~EqualTo() {}

    bool operator()(const jobject& lhs, const jobject& rhs) const {
      jclass clazz {env->GetObjectClass(lhs)};
      jmethodID equals
        {env->GetMethodID(clazz, "equals", "(Ljava/lang/Object;)Z")};
      return env->CallBooleanMethod(lhs, equals, rhs);
    }

  private:
    JNIEnv* env {nullptr};
  };

  struct Hasher {
  public:
    Hasher(JNIEnv* env) : env(env) {}
    ~Hasher() {}

    uint64_t operator()(const jobject& key) const {
      jclass clazz {env->GetObjectClass(key)};
      jmethodID hasher
        {env->GetMethodID(clazz, "hashCode", "()I")};
      return (uint64_t) env->CallIntMethod(key, hasher);
    }

  private:
    JNIEnv* env {nullptr};
  };

  unordered_map<jobject, vector<KVPair>, Hasher, EqualTo>* hashmap;
};
#endif
