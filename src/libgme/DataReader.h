// Data reader interface for uniform access

// File_Extractor 0.4.0
#ifndef DATA_READER_H
#define DATA_READER_H

#include "blargg_common.h"

#ifdef HAVE_ZLIB_H
#include <zlib.h>
#endif

// Supports reading and finding out how many bytes are remaining
class DataReader {
 public:
  virtual ~DataReader() {}

  static const char eof_error[];  // returned by read() when request goes beyond end

  // Read at most count bytes and return number actually read, or <= 0 if
  // error
  virtual long read_avail(void *, long n) = 0;

  // Read exactly count bytes and return error if they couldn't be read
  virtual blargg_err_t read(void *, long count);

  // Number of bytes remaining until end of file
  virtual long remain() const = 0;

  // Read and discard count bytes
  virtual blargg_err_t skip(long count);

 public:
  DataReader() {}
  typedef blargg_err_t error_t;  // deprecated
 private:
  // noncopyable
  DataReader(const DataReader &);
  DataReader &operator=(const DataReader &);
};

// Supports seeking in addition to DataReader operations
class FileReader : public DataReader {
 public:
  // Size of file
  virtual long size() const = 0;

  // Current position in file
  virtual long tell() const = 0;

  // Go to new position
  virtual blargg_err_t seek(long) = 0;

  long remain() const;
  blargg_err_t skip(long n);
};

// Disk file reader
class StdFileReader : public FileReader {
 public:
  blargg_err_t open(const char *path);
  void close();

 public:
  StdFileReader();
  ~StdFileReader();
  long size() const;
  blargg_err_t read(void *, long);
  long read_avail(void *, long);
  long tell() const;
  blargg_err_t seek(long);

 private:
  void *file_;  // Either FILE* or zlib's gzFile
#ifdef HAVE_ZLIB_H
  long size_;  // TODO: Fix ABI compat
#endif         /* HAVE_ZLIB_H */
};

// Treats range of memory as a file
class MemFileReader : public FileReader {
 public:
  MemFileReader(const void *, long size);
#ifdef HAVE_ZLIB_H
  ~MemFileReader();
#endif /* HAVE_ZLIB_H */

 public:
  long size() const;
  long read_avail(void *, long);
  long tell() const;
  blargg_err_t seek(long);

 private:
#ifdef HAVE_ZLIB_H
  bool gz_decompress();
#endif /* HAVE_ZLIB_H */

  const char *m_begin;
  long m_size;
  long m_pos;
#ifdef HAVE_ZLIB_H
  bool m_ownedPtr = false;  // set if we must free m_begin
#endif                      /* HAVE_ZLIB_H */
};

// Makes it look like there are only count bytes remaining
class SubsetReader : public DataReader {
 public:
  SubsetReader(DataReader *, long count);

 public:
  long remain() const;
  long read_avail(void *, long);

 private:
  DataReader *in;
  long remain_;
};

// Joins already-read header and remaining data into original file (to avoid
// seeking)
class RemainingReader : public DataReader {
 public:
  RemainingReader(void const *header, long size, DataReader *);

 public:
  long remain() const;
  long read_avail(void *, long);
  blargg_err_t read(void *, long);

 private:
  char const *header;
  char const *header_end;
  DataReader *in;
  long read_first(void *out, long count);
};

// Invokes callback function to read data. Size of data must be specified in
// advance.
class CallbackReader : public DataReader {
 public:
  typedef const char *(*callback_t)(void *data, void *out, int count);
  CallbackReader(callback_t, long size, void *data = 0);

 public:
  long read_avail(void *, long);
  blargg_err_t read(void *, long);
  long remain() const;

 private:
  callback_t const callback;
  void *const data;
  long remain_;
};

#endif
