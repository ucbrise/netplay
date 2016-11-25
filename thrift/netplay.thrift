namespace cpp netplay.thrift

struct Storage {
  1: i64 dlog_size,
  2: i64 olog_size,
  3: list<i64> idx_sizes,
  4: list<i64> stream_sizes,
}

service NetPlayQueryService {
	// Supported operations
	set<i64> filter(1:string query),
	binary get(1:i64 record_id),
	binary extract(1:i64 record_id, 2:i16 off, 3:i16 len),

	i64 numRecords(),
  Storage storageFootprint(),
}