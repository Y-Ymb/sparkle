#include <jni.h>
#include <vector>
#include <algorithm>
#include <cstddef>
#include <utility>
#include <chrono>
#include <glog/logging.h>
#include "MapShuffleStoreWithObjKeys.h"
#include "KVPair.h"
#include "ShuffleConstants.h"
#include "ShuffleStoreManager.h"
#include "ShuffleDataSharedMemoryManager.h"
#include "ShuffleDataSharedMemoryManagerHelper.h"
#include "MapStatus.h"
#include "SimpleUtils.h"
#include "../jnishuffle/JniUtils.h"

using namespace std;

MapShuffleStoreWithObjKeys::
MapShuffleStoreWithObjKeys(int mapId, bool ordering)
  : mapId(mapId), doOrdering(ordering) {
  kvPairs.reserve(2 * 1024 * 1024);
}

void
MapShuffleStoreWithObjKeys::storeKVPairs(
    vector<jobject>& keys, int *keyHashes, unsigned char *values,
    int* voffsets, int* partitions, int numPairs) {

  int voffset = 0;
  for (int i=0; i<numPairs; ++i) {
    int serValueSize {*(voffsets+i) - voffset};
    byte* serValue {new byte[serValueSize]};
    memcpy(serValue, values+voffset, serValueSize);

    kvPairs.emplace_back(keys[i], *(keyHashes+i), serValue, serValueSize, *(partitions+i));
    voffset += serValueSize;

    // update # of partitions.
    numPartitions = max(numPartitions, *(partitions+i)+1);
  }

  return ;
}

unique_ptr<MapStatus>
MapShuffleStoreWithObjKeys::write(JNIEnv* env) {
  // sort pairs in the store.
  auto start = chrono::system_clock::now();
  if (needsOrdering()) {
    sortPairs(env);
  }
  auto end = chrono::system_clock::now();
  chrono::duration<double> elapsed_s = end - start;
  LOG(INFO) << "sorting " << kvPairs.size() << " pairs in the map store took " << elapsed_s.count() << "s";

  // finally start desrializing keys because we don't need POJO after ordering.
  start = chrono::system_clock::now();
  serializeKeys(env);
  end = chrono::system_clock::now();
  elapsed_s = end - start;
  LOG(INFO) << "serializing " << kvPairs.size() << " keys in the map store took " << elapsed_s.count() << "s";

  // transfer kvPairs to the global0.
  vector<byte*> offsets;
  offsets.reserve(numPartitions);
  NativeMapStatus stats;

  start = chrono::system_clock::now();
  writeIndexChunk(offsets, stats);
  end = chrono::system_clock::now();
  LOG(INFO) << "transfering the index chunk into global0 took " << elapsed_s.count() << "s";

  start = chrono::system_clock::now();
  writeDataChunk(offsets);
  end = chrono::system_clock::now();
  stats.writtenTimeNs = chrono::duration_cast<chrono::nanoseconds>(end - start).count();
  elapsed_s = end - start;
  LOG(INFO) << "transfering " << kvPairs.size() << " pairs into global0 took " << elapsed_s.count() << "s";

  // fill MapStatus with corresponding stats.
  unique_ptr<MapStatus> mapStatus(
    new MapStatus(stats.indexChunkAddr.first,
                  stats.indexChunkAddr.second, numPartitions, mapId));
  mapStatus->setWrittenTime(stats.writtenTimeNs);
  for (int i=0; i<(int)stats.bucketSizes.size(); ++i) {
    mapStatus->setBucketSize(i, stats.bucketSizes[i]);
  }

  return mapStatus;
}

unique_ptr<MapStatus>
MapShuffleStoreWithObjKeys::write(byte* directBuffer,
                                  int numPartitions, int* lengths) {
  static_assert(SHMShuffleGlobalConstants::USING_RMB, "RMB should be enabled.");
  RRegion::TPtr<void> global_null_ptr;

  ShuffleDataSharedMemoryManager *memoryManager {
    ShuffleStoreManager::getInstance()->getShuffleDataSharedMemoryManager()};

  size_t indexChunkSize =
    sizeof(int) // NUMA node identifier.
    + sizeof(int) // # of buckets(=partitions)
    // # of (regionId, offset, sizeof(bucket)) for each partition.
    + numPartitions * (sizeof(uint64_t)*2 + sizeof(int));

  RRegion::TPtr<void> indexChunkGlobalPointer
    = memoryManager->allocate_indexchunk(indexChunkSize);
  assert(indexChunkGlobalPointer != global_null_ptr);

  NativeMapStatus status;
  status.indexChunkAddr
    = make_pair(indexChunkGlobalPointer.region_id(), indexChunkGlobalPointer.offset());
  idxChunkPtr = status.indexChunkAddr;
  byte* localOffset = (byte*) indexChunkGlobalPointer.get();

  int numa {OsUtil::getCurrentNumaNode()};
  memcpy(localOffset, &numa, sizeof(numa));
  localOffset += sizeof(numa);

  memcpy(localOffset, &numPartitions, sizeof(int));
  localOffset += sizeof(int);

  // make data chunks, then write shuffle data into the chunk.
  for (int i=0; i<numPartitions; ++i) {
    int curBucketSize {*(lengths + i)};
    // keep BucketSize before hand.
    status.bucketSizes.emplace_back(curBucketSize);

    RRegion::TPtr<void> chunk
      = memoryManager->allocate_datachunk(curBucketSize);
    assert(chunk != global_null_ptr);

    // keep the chunk identifer to free this region on the store shutdown.
    dataChunkPtrs.emplace_back(chunk.region_id(), chunk.offset());

    uint64_t regionId {chunk.region_id()};
    memcpy(localOffset, &regionId, sizeof(uint64_t));
    localOffset += sizeof(uint64_t);

    uint64_t chunkOffset {chunk.offset()};
    memcpy(localOffset, &chunkOffset, sizeof(uint64_t));
    localOffset += sizeof(uint64_t);

    memcpy(localOffset, &curBucketSize, sizeof(int));
    localOffset += sizeof(int);

    // measure written time for Shuffle Metrics.
    auto start = chrono::system_clock::now();
    memcpy(chunk.get(), directBuffer, curBucketSize);
    auto end = chrono::system_clock::now();
    status.writtenTimeNs =
      chrono::duration_cast<chrono::nanoseconds>(end - start).count();

    // advance buffer position to the next partition.
    directBuffer += curBucketSize;
  }

  // fill MapStatus with corresponding stats.
  unique_ptr<MapStatus> mapStatus(
    new MapStatus(status.indexChunkAddr.first,
                  status.indexChunkAddr.second, numPartitions, mapId));
  mapStatus->setWrittenTime(status.writtenTimeNs);
  for (int i=0; i<(int)status.bucketSizes.size(); ++i) {
    mapStatus->setBucketSize(i, status.bucketSizes[i]);
  }

  return mapStatus;
}

void
MapShuffleStoreWithObjKeys::deleteJobjectKeys(JNIEnv* env) {
  for (auto& pair : kvPairs) {
    env->DeleteGlobalRef(pair.getKey());
  }
}

void
MapShuffleStoreWithObjKeys::shutdown() {
  LOG(INFO) << "map shuffle store with obj keys with mapId: " << mapId << " is shutting down";

  ShuffleDataSharedMemoryManager *memoryManager
    = ShuffleStoreManager::getInstance()->getShuffleDataSharedMemoryManager();

  {
    RRegion::TPtr<void> gptr(idxChunkPtr.first, idxChunkPtr.second);
    memoryManager->free_indexchunk(gptr);
  }

  for (auto ptr : dataChunkPtrs) {
    RRegion::TPtr<void> gptr(ptr.first, ptr.second);
    memoryManager->free_datachunk(gptr);
  }
}


/*
 * private methods
 */

void
MapShuffleStoreWithObjKeys::sortPairs(JNIEnv* env) {
  //stable_sort(kvPairs.begin(), kvPairs.end(), shuffle::MapComparator(env));
}

void
MapShuffleStoreWithObjKeys::serializeKeys(JNIEnv* env) {
  // org/apache/commons/lang3/SerializationUtils is too slow.
  // It takes 15 sec to serialize 5,000,000 int keys.
  static int kPoolSize = 1024; // 1kB

  jclass initiatorClazz {env->FindClass("com/twitter/chill/KryoInstantiator")};
  jobject kryoInitiator
    {env->NewObject(initiatorClazz, env->GetMethodID(initiatorClazz, "<init>", "()V"))};

  jclass serClazz {env->FindClass("com/twitter/chill/KryoPool")};
  jmethodID factoryMid
  {env->GetStaticMethodID(serClazz, "withByteArrayOutputStream", "(ILcom/twitter/chill/KryoInstantiator;)Lcom/twitter/chill/KryoPool;")};
  jobject kryo {env->CallStaticObjectMethod(serClazz, factoryMid, kPoolSize, kryoInitiator)};

  jmethodID serMid {env->GetMethodID(serClazz, "toBytesWithClass", "(Ljava/lang/Object;)[B")};
  for (auto& kvPair : kvPairs) {
    jbyteArray byteArray =
      (jbyteArray)env->CallObjectMethod(kryo, serMid, kvPair.getKey());

    jbyte* bytes =
      env->GetByteArrayElements(byteArray , NULL);

    kvPair.setSerKey(reinterpret_cast<byte*>(bytes));
    kvPair.setSerKeySize(env->GetArrayLength(byteArray));
  }
}

void
MapShuffleStoreWithObjKeys::writeIndexChunk(vector<byte*>& dataChunkLocalOffsets, NativeMapStatus& mapStatus) {
  static_assert(SHMShuffleGlobalConstants::USING_RMB, "RMB should be enabled.");

  ShuffleDataSharedMemoryManager *memoryManager {
    ShuffleStoreManager::getInstance()->getShuffleDataSharedMemoryManager()};
  RRegion::TPtr<void> global_null_ptr;
  size_t indexChunkSize =
    sizeof(int) // NUMA node identifier.
    + sizeof(int) // Key's type.
    + sizeof(int) // # of buckets(=partitions)
    // # of (regionId, offset, sizeof(bucket), sizeof(numPairs)) for each partition.
    + numPartitions * (sizeof(uint64_t)*2 + sizeof(int)*2);

  RRegion::TPtr<void> indexChunkGlobalPointer
    = memoryManager->allocate_indexchunk (indexChunkSize);
  assert(indexChunkGlobalPointer != global_null_ptr);
  mapStatus.indexChunkAddr
    = make_pair(indexChunkGlobalPointer.region_id(), indexChunkGlobalPointer.offset());
  idxChunkPtr = mapStatus.indexChunkAddr;
  byte* localOffset = (byte*) indexChunkGlobalPointer.get();

  {
    int numa {OsUtil::getCurrentNumaNode()};
    memcpy(localOffset, &numa, sizeof(numa));
    localOffset += sizeof(numa);
  }

  {
    int keyTypeId {KValueTypeId::Object};
    memcpy(localOffset, &keyTypeId, sizeof(KValueTypeId));
    localOffset += sizeof(KValueTypeId);
  }

  {
    memcpy(localOffset, &numPartitions, sizeof(int));
    localOffset += sizeof(int);
  }

  // alloc data chunks, then write their meta data into the index chunk.
  vector<int> bucketSizes(numPartitions); // byte
  // # of pairs(not aggregated) for each partition.
  vector<int> numPairs(numPartitions);

  for (auto& pair : kvPairs) {
    bucketSizes[pair.getPartition()] +=
      sizeof(int)*2 + pair.getSerKeySize() + sizeof(int) + pair.getSerValueSize();
    numPairs[pair.getPartition()] += 1;
  }

  for (int i=0; i<numPartitions; ++i) {
    // keep BucketSize before hand.
    mapStatus.bucketSizes.emplace_back(bucketSizes[i]);

    // Allocate Data Chuncks using bucketSize.
    // Then, keep the (regionId, offset) pairs in this instance.
    RRegion::TPtr<void> chunk
      = memoryManager->allocate_datachunk(bucketSizes[i]);
    assert(chunk != global_null_ptr);
    dataChunkPtrs.emplace_back(chunk.region_id(), chunk.offset());

    {
      uint64_t regionId {chunk.region_id()};
      memcpy(localOffset, &regionId, sizeof(uint64_t));
      localOffset += sizeof(uint64_t);

      uint64_t chunkOffset {chunk.offset()};
      memcpy(localOffset, &chunkOffset, sizeof(uint64_t));
      localOffset += sizeof(uint64_t);

      memcpy(localOffset, &bucketSizes[i], sizeof(int));
      localOffset += sizeof(int);

      memcpy(localOffset, &numPairs[i], sizeof(int));
      localOffset += sizeof(int);
    }

    // save the data chunk head pointers to store pairs into this chunk.
    dataChunkLocalOffsets.emplace_back((byte*) chunk.get());
  }
}

void
MapShuffleStoreWithObjKeys::writeDataChunk(vector<byte*>& localOffsets) {
  /*
   * [(key-hash, key-size, serKey, value-size, serValue)] for each partition.
   */
  for (auto& pair : kvPairs) {
    byte* localOffset = localOffsets[pair.getPartition()];

    {
      int keyHash = pair.getKeyHash();
      memcpy(localOffset, &keyHash, sizeof(int));
      localOffset += sizeof(int);

      int keySize = pair.getSerKeySize();
      memcpy(localOffset, &keySize, sizeof(int));
      localOffset += sizeof(int);

      memcpy(localOffset, pair.getSerKey(), keySize);
      localOffset += keySize;
    }

    {
      int valueSize = pair.getSerValueSize();
      memcpy(localOffset, &valueSize, sizeof(int));
      localOffset += sizeof(int);

      memcpy(localOffset, pair.getSerValue(), valueSize);
      localOffset += valueSize;
    }

    localOffsets[pair.getPartition()] = localOffset;
  }
}
