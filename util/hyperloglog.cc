//
// Created by Omkar Desai on 10/17/20.
//

#include <cassert>
#include <cmath>

#include "util/hyperloglog.h"

using namespace std;

namespace leveldb {

HyperLogLog::HyperLogLog(int num_sharding_bits) :
    num_sharding_bits_(num_sharding_bits),
    num_buckets_(1 << num_sharding_bits_),
    bucket_mask_(num_buckets_ - 1),
    counters_(num_buckets_, 0) {

  // From Flajolet, num_sharding_bits_ is the b value; num_buckets_ is the m
  // value; m = 2^b and the forumla for alpha is given in Figure 3. See
  // hyperloglog.h for a reference to the paper.
  double alpha;
  switch (num_buckets_) {
    case 16:
      alpha = 0.673;
      break;
    case 32:
      alpha = 0.697;
      break;
    case 64:
      alpha = 0.709;
      break;
    default:
      alpha = 0.7213 / (1.0 + 1.079 / num_buckets_);
      break;
  }

  alpha_num_buckets2_ = alpha * num_buckets_ * num_buckets_;
}

//-----------------------------------------------------------------------------

bool HyperLogLog::AddHash(int64_t hash) {
  bool bucket_changed = false;

  // Departing from Flajolet, we use the lower bits to determine the bucket
  // (so that we can later compute rho on the high bits without a shift).
  const int bucket = hash & bucket_mask_;

  // Make sure hash is the correct size for the __builtin_clzll used below.
  assert(sizeof(hash) == 8);

  // rho is defined in the papers as the position of the first 1 bit in hash
  // (e.g., rho(1...) = 1 and rho(0001...) = 4). Since we are using the clz
  // builtin, which returns the number of leading 0-bits, we add one to the
  // returned value.
  int rho = __builtin_clzll(hash) + 1;

  if (rho > 64 - num_sharding_bits_) {
    // If the first bit is in the sharding bit range, store a maximum value.
    rho = 64 - num_sharding_bits_ + 1;
  }

  if (rho > counters_[bucket]) {
    counters_[bucket] = rho;
    bucket_changed = true;
  }

  return bucket_changed;
}

//-----------------------------------------------------------------------------

int64_t HyperLogLog::MergedEstimate(const vector<HyperLogLog*>& hll_vector) {

  const int num_buckets = hll_vector[0]->num_buckets_;
  vector<int8_t> counters(num_buckets, 0);

  for (HyperLogLog* hll : hll_vector) {
    for (int bucket = 0; bucket < num_buckets; ++bucket) {
      counters[bucket] = max(counters[bucket],
                             hll->counters_[bucket]);
    }
  }
  return Estimate(counters, true, hll_vector[0]->alpha_num_buckets2_);
}

//-----------------------------------------------------------------------------

int64_t HyperLogLog::Estimate(const vector<int8_t>& counters,
                              bool correct,
                              double alpha_num_buckets2) {
  const int num_buckets = counters.size();
  double sum = 0.0;

  for (int bucket = 0; bucket < num_buckets; ++bucket) {
    sum += 1.0 / (1ULL << counters[bucket]);
  }

  // Compute a normalized version of the harmonic mean (see page 130 in
  // Flajolet, equations 2 and 3; and Figure 3, page 140).
  double estimate = alpha_num_buckets2 / sum;

  // Apply small range and large range corrections to the estimate (from pages
  // 140-141 of Flajolet).
  if (correct && estimate <= 2.5 * num_buckets) {
    int zeros = 0;

    for (int bucket = 0; bucket < num_buckets; ++bucket) {
      if (!counters[bucket]) {
        ++zeros;
      }
    }
    if (zeros) {
      // Apply the small range correction.
      estimate = num_buckets * log(static_cast<double>(num_buckets) /
                                   static_cast<double>(zeros));
    }
  }

  // Flajolet's correction for collisions at cardinalities close to 2^32 is
  // not needed because we are using a 64-bit hash. See Heule for discussion.

  return estimate;
}

} // namespace