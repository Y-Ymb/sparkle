#ifndef __KVPAIR_H__
#define __KVPAIR_H__

#include <jni.h>
#include <memory>
#include <cstddef>

using namespace std;

/*
 * It it better to decouple the map-side pair and that of reduce-size.
 * The reasons are shown below.
 * 1) Memory management is quite differecne between maps and reduces.
 *    For examaple, serialized value is managed by JVM on the map-side,
 *    but on the other side it is nothing to do with JVM.
 * 2) Eliminate unnecessary setters from the reduce-side pairs.
 */
class MapKVPair {
 public:
 MapKVPair(const jobject& key, int khash, byte* value, int vSize, int partition) :
  key(key), khash(khash), value(value), vSize(vSize), partition(partition) {}
  ~MapKVPair() {
    delete [] value;
  }

  inline int getPartition() const {return partition;}
  inline jobject getKey() const {return key;}
  inline void setKey(jobject& _key) {key = _key;};
  inline byte* getSerKey() const {return serKey;}
  inline void setSerKey(byte* bytes) {
    serKey = bytes;
  }
  inline int getSerKeySize() const {return serKeySize;}
  inline void setSerKeySize(int size) {
    serKeySize = size;
  }
  inline int getKeyHash() const {return khash;}
  inline byte* getSerValue() const {return value;}
  inline int getSerValueSize() const {return vSize;}

 private:
  jobject key {nullptr};
  int khash {-1};
  byte* serKey {nullptr};
  int serKeySize {-1};

  byte* value {nullptr};
  int vSize {-1};

  int partition {-1};
};

class ReduceKVPair {
public:
 ReduceKVPair(byte* serKey,int serKeySize, int keyHash, byte* value, int vSize, int partition):
  serKey(serKey),
    serKeySize(serKeySize),
    khash(keyHash),
    value(value),
    vSize(vSize), partition(partition) {}
  ReduceKVPair(const ReduceKVPair& pair) :
  key(pair.key),
    serKeySize(pair.serKeySize),
    khash(pair.khash),
    vSize(pair.vSize),
    partition(pair.partition) {
    serKey = new byte[serKeySize];
    memcpy(serKey, pair.serKey, serKeySize);

    value = new byte[vSize];
    memcpy(value, pair.value, vSize);
    }
 ReduceKVPair(ReduceKVPair&& pair) noexcept :
  key(pair.key),
    serKey(pair.serKey),
    serKeySize(pair.serKeySize),
    khash(pair.khash),
    value(pair.value),
    vSize(pair.vSize),
    partition(pair.partition)
  {
    pair.serKey = nullptr;
    pair.value = nullptr;
  }
  ~ReduceKVPair() {
    delete [] serKey;
    delete [] value;
  }

  ReduceKVPair& operator=(ReduceKVPair&& pair) {
    if (&pair == this) {
      return *this;
    }

    key = pair.key;
    serKeySize = pair.serKeySize;
    khash = pair.khash;

    delete [] serKey;
    serKey = pair.serKey;
    pair.serKey = nullptr;

    vSize = pair.vSize;

    delete [] value;
    value = pair.value;
    pair.value = nullptr;

    partition = pair.partition;

    return *this;
  }

  inline int getPartition() const {return partition;}
  inline jobject getKey() const {return key;}
  inline void setKey(jobject& _key) {key = _key;};
  inline byte* getSerKey() const {return serKey;}
  inline int getSerKeySize() const {return serKeySize;}
  inline int getKeyHash() const {return khash;}
  inline byte* getSerValue() const {return value;}
  inline int getSerValueSize() const {return vSize;}
private:
  jobject key {nullptr};
  byte* serKey {nullptr};
  int serKeySize {-1};
  int khash {-1};

  byte* value {nullptr};
  int vSize {-1};

  int partition {-1};
};
#endif
