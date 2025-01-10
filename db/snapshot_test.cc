#include "db/db_impl.h"
#include "gtest/gtest.h"

namespace leveldb {

// Test to validate GetAllSnapshots retrieves all snapshots correctly.
TEST(SnapshotTest, GetAllSnapshots) {
  SnapshotList snapshot_list;

  // Create some snapshots
  SnapshotImpl* s1 = snapshot_list.New(1);
  SnapshotImpl* s2 = snapshot_list.New(2);
  SnapshotImpl* s3 = snapshot_list.New(3);

  // Use GetAllSnapshots to retrieve them
  std::vector<const Snapshot*> snapshots = snapshot_list.GetAllSnapshots();

  // Validate the results
  ASSERT_EQ(snapshots.size(), 3);
  EXPECT_EQ(static_cast<const SnapshotImpl*>(snapshots[0])->sequence_number(), 1);
  EXPECT_EQ(static_cast<const SnapshotImpl*>(snapshots[1])->sequence_number(), 2);
  EXPECT_EQ(static_cast<const SnapshotImpl*>(snapshots[2])->sequence_number(), 3);

  // Clean up
  snapshot_list.Delete(s1);
  snapshot_list.Delete(s2);
  snapshot_list.Delete(s3);
}

}  
