//
// Created by Omkar Desai on 10/17/20.
//

#ifndef LEVELDB_HYPERLOGLOG_H
#define LEVELDB_HYPERLOGLOG_H
#include <stdint.h>
#include <vector>

namespace leveldb {

class HyperLogLog {
 public:

  // Create the HyperLogLog object so that cardinality can be tracked within 1
  // or more intervals. Current time is in microseconds. Interval times are in
  // seconds. The num_sharding_bits value must be between 4 and 16, inclusive;
  // using a larger value will cause the object to consume more memory. The
  // granularity parameter specifies the size to which element values will be
  // rounded (using integer division).
  HyperLogLog(int num_sharding_bits);

  ~HyperLogLog() = default;

  // Add all the elements based on the hash value. This can be used by the
  // caller to avoid computing the hash more than once if multiple HyperLogLog
  // instances must be updated. For VDiskCacheSizeEstimator, we return true if
  // any bucket was changed; this indicates the cardinality has likely changed
  // and should be recomputed if needed.
  bool AddHash(int64_t hash);

  static int64_t MergedEstimate(const std::vector<HyperLogLog*>& hll_vector);

 private:

  // Compute an estimate of cardinality for a set of counters. This is an
  // internal helper method.
  static int64_t Estimate(const std::vector<int8_t>& counters, bool correct,
                          double alpha_num_buckets2);

  // The number of bits (b from Flajolet) used for sharding the input into
  // buckets. More bits will use more memory, but provide a more accurate
  // estimate of cardinality.
  const int num_sharding_bits_;

  // Precompute the number of buckets (m=2^b from Flajolet).
  const int num_buckets_;

  // Precompute the mask to determine the bucket.
  const int bucket_mask_;

  // Stores the maximum bit offset for each hash for each bucket in each
  // interval.
  std::vector<int8_t> counters_;

  // The value of alpha is given by Flajolet at the top of Figure 3 (p. 140).
  // Here, we store alpha * num_buckets_^2, which is used in Estimator().
  double alpha_num_buckets2_;
};

} // namespace

#endif  // LEVELDB_HYPERLOGLOG_H
