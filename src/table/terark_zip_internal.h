/*
 * terark_zip_internal.h
 *
 *  Created on: 2017-05-02
 *      Author: zhaoming
 */

#pragma once

#ifndef TERARK_ZIP_INTERNAL_H_
#define TERARK_ZIP_INTERNAL_H_

// project headers
#include "terark_zip_table.h"
// std headers
#include <mutex>
// rocksdb headers
#include <rocksdb/slice.h>
#include <rocksdb/env.h>
#include <rocksdb/table.h>
// terark headers
#include <terark/fstring.hpp>
#include <terark/valvec.hpp>
#include <terark/stdtypes.hpp>
#include <terark/util/profiling.hpp>


//#define TERARK_SUPPORT_UINT64_COMPARATOR
//#define DEBUG_TWO_PASS_ITER



namespace rocksdb {

using terark::fstring;
using terark::valvec;
using terark::byte_t;


extern terark::profiling g_pf;

extern const uint64_t kTerarkZipTableMagicNumber;

extern const std::string kTerarkZipTableIndexBlock;
extern const std::string kTerarkZipTableValueTypeBlock;
extern const std::string kTerarkZipTableValueDictBlock;
extern const std::string kTerarkZipTableOffsetBlock;
extern const std::string kTerarkZipTableCommonPrefixBlock;
extern const std::string kTerarkEmptyTableKey;


template<class ByteArray>
inline Slice SliceOf(const ByteArray& ba) {
  BOOST_STATIC_ASSERT(sizeof(ba[0] == 1));
  return Slice((const char*)ba.data(), ba.size());
}

inline static fstring fstringOf(const Slice& x) {
  return fstring(x.data(), x.size());
}

template<class ByteArrayView>
inline ByteArrayView SubStr(const ByteArrayView& x, size_t pos) {
  assert(pos <= x.size());
  return ByteArrayView(x.data() + pos, x.size() - pos);
}


enum class ZipValueType : unsigned char {
  kZeroSeq = 0,
  kDelete = 1,
  kValue = 2,
  kMulti = 3,
};
//const size_t kZipValueTypeBits = 2;

struct ZipValueMultiValue {
  // TODO: use offset[0] as num, and do not store offsets[num]
  // when unzip, reserve num+1 cells, set offsets[0] to 0,
  // and set offsets[num] to length of value pack
  //	uint32_t num;
  uint32_t offsets[1];

  ///@size size include the extra uint32, == encoded_size + 4
  static
    const ZipValueMultiValue* decode(void* data, size_t size, size_t* pNum) {
    // data + 4 is the encoded data
    auto me = (ZipValueMultiValue*)(data);
    size_t num = me->offsets[1];
    assert(num > 0);
    memmove(me->offsets + 1, me->offsets + 2, sizeof(uint32_t)*(num - 1));
    me->offsets[0] = 0;
    me->offsets[num] = size - sizeof(uint32_t)*(num + 1);
    *pNum = num;
    return me;
  }
  static
    const ZipValueMultiValue* decode(valvec<byte_t>& buf, size_t* pNum) {
    return decode(buf.data(), buf.size(), pNum);
  }
  Slice getValueData(size_t nth, size_t num) const {
    assert(nth < num);
    size_t offset0 = offsets[nth + 0];
    size_t offset1 = offsets[nth + 1];
    size_t dlength = offset1 - offset0;
    const char* base = (const char*)(offsets + num + 1);
    return Slice(base + offset0, dlength);
  }
  static size_t calcHeaderSize(size_t n) {
    return sizeof(uint32_t) * (n);
  }
};



class TerarkZipTableFactory : public TableFactory, boost::noncopyable {
public:
  explicit
    TerarkZipTableFactory(const TerarkZipTableOptions& tzto, TableFactory* fallback)
    : table_options_(tzto), fallback_factory_(fallback) {
    adaptive_factory_ = NewAdaptiveTableFactory();
  }
  ~TerarkZipTableFactory() {
      delete fallback_factory_;
      delete adaptive_factory_;
  }

  const char* Name() const override { return "TerarkZipTable"; }

  Status
    NewTableReader(const TableReaderOptions& table_reader_options,
      unique_ptr<RandomAccessFileReader>&& file,
      uint64_t file_size,
      unique_ptr<TableReader>* table,
      bool prefetch_index_and_filter_in_cache) const override;

  TableBuilder*
    NewTableBuilder(const TableBuilderOptions& table_builder_options,
      uint32_t column_family_id,
      WritableFileWriter* file) const override;

  std::string GetPrintableTableOptions() const override;

  // Sanitizes the specified DB Options.
  Status SanitizeOptions(const DBOptions& db_opts,
    const ColumnFamilyOptions& cf_opts) const override;

  void* GetOptions() override { return &table_options_; }

  bool IsDeleteRangeSupported() const override { return true; }

private:
  TerarkZipTableOptions table_options_;
  TableFactory* fallback_factory_;
  TableFactory* adaptive_factory_; // just for open table
  mutable size_t nth_new_terark_table_ = 0;
  mutable size_t nth_new_fallback_table_ = 0;
};


}  // namespace rocksdb

#endif /* TERARK_ZIP_INTERNAL_H_ */
