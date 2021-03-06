#ifndef CONFLUO_SCHEMA_SCHEMA_SNAPSHOT_H_
#define CONFLUO_SCHEMA_SCHEMA_SNAPSHOT_H_

#include "schema/column_snapshot.h"
#include "types/byte_string.h"
#include "types/immutable_value.h"
#include "types/raw_data.h"

namespace confluo {

/**
 * Snapshot of the schema for backups
 */
class schema_snapshot {
 public:
  /**
   * Constructs an empty snapshot of the schema
   */
  schema_snapshot() = default;

  /**
   * Adds a column to the snapshot of the schema
   *
   * @param snap The snapshot of the column to add
   */
  void add_column(const column_snapshot &snap);

  /**
   * Adds an r value reference column snapshot to the schema snapshot
   *
   * @param snap The r value reference to a column snapshot
   */
  void add_column(column_snapshot &&snap);

  /**
   * Gets the data for a specific column in the schema snapshot
   *
   * @param data The data in the column
   * @param i The index for finding the column
   *
   * @return An immutable value representing the data at the column
   */
  immutable_value get(void *data, uint32_t i) const;

  /**
   * Gets the time key of the schema snapshot
   *
   * @param time_block The time_block of the schema snapshot
   *
   * @return The byte string containing the time
   */
  byte_string time_key(int64_t time_block) const;

  /**
   * Gets the key of the schema snapshot
   *
   * @param ptr The pointer to wherere the key is
   * @param i The index of the column snapshot to find the key
   *
   * @return Byte string containing the key of the snapshot
   */
  byte_string get_key(void *ptr, uint32_t i) const;

  /**
   * Gets the timestamp of the schema snapshot
   *
   * @param ptr The pointer to where the timestamp is in the schema
   * snapshot
   *
   * @return The timestamp of the schema snapshot
   */
  int64_t get_timestamp(void *ptr) const;

  /**
   * Whether the specified column snapshot is indexed
   *
   * @param i The index of the column snapshot
   *
   * @return True if the column snapshot is indexed, false otherwise
   */
  bool is_indexed(size_t i) const;

  /**
   * Gets the id of the index of a column snapshot
   *
   * @param i The index of the column snapshot
   *
   * @return The identifier for the index
   */
  uint32_t index_id(size_t i) const;

  /**
   * Gets the bucket size of a column snapshot
   *
   * @param i The index of the column snapshot
   *
   * @return The bucket size of the specified column snapshot
   */
  double index_bucket_size(size_t i) const;

  /**
   * Gets the number of columns in the schema snapshot
   *
   * @return The number of columns in the schema snapshot
   */
  size_t num_columns() const;

 private:
  std::vector<column_snapshot> snapshot_;
};

}

#endif /* CONFLUO_SCHEMA_SCHEMA_SNAPSHOT_H_ */
