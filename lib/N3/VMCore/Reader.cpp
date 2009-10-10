//===--------------- Reader.cpp - Open and read files ---------------------===//
//
//                                N3
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include <stdio.h>
#include <string.h>

#include "types.h"

#include "MSCorlib.h"
#include "N3.h"
#include "VMArray.h"
#include "VMClass.h"
#include "VMThread.h"
#include "Reader.h"

using namespace n3;

double Reader::readDouble(int first, int second) {
  int values[2];
  double res[1];
#if defined(__PPC__)
  values[0] = second;
  values[1] = first;
#else
  values[0] = first;
  values[1] = second;
#endif
  memcpy(res, values, 8); 
  return res[0];
}

sint64 Reader::readLong(int first, int second) {
  int values[2];
  sint64 res[1];
#if defined(__PPC__)
  values[0] = second;
  values[1] = first;
#else
  values[0] = first;
  values[1] = second;
#endif
  memcpy(res, values, 8); 
  return res[0];
}

const int Reader::SeekSet = SEEK_SET;
const int Reader::SeekCur = SEEK_CUR;
const int Reader::SeekEnd = SEEK_END;

ByteCode::ByteCode(mvm::BumpPtrAllocator &allocator, int size) {
	this->size = size;
	this->elements = (uint8*)allocator.Allocate(size * sizeof(uint8), "uint8[]");
}

ByteCode* Reader::openFile(mvm::BumpPtrAllocator &allocator, char* path) {
  FILE* fp = fopen(path, "r");
  ByteCode* res = 0;
  if (fp != 0) {
    fseek(fp, 0, SeekEnd);
    long nbb = ftell(fp);
    fseek(fp, 0, SeekSet);
		res = new(allocator, "ByteCode") ByteCode(allocator, nbb);
    fread(res->elements, nbb, 1, fp);
    fclose(fp);
  }
  return res;
}

uint8 Reader::readU1() {
	if(cursor >= (uint32)bytes->size)
		VMThread::get()->vm->error("readU1 outside the buffer");
  return bytes->elements[cursor++];
}

sint8 Reader::readS1() {
  return readU1();
}

uint16 Reader::readU2() {
  uint16 tmp = ((uint16)(readU1()));
  return tmp | (((uint16)(readU1())) << 8);
}

sint16 Reader::readS2() {
  sint16 tmp = ((sint16)(readS1()));
  return tmp | (((sint16)(readS1())) << 8);
}

uint32 Reader::readU4() {
  uint32 tmp = ((uint32)(readU2()));
  return tmp | (((uint32)(readU2())) << 16);
}

sint32 Reader::readS4() {
  sint32 tmp = ((sint32)(readS2()));
  return tmp | (((sint32)(readS2())) << 16);
}

uint64 Reader::readU8() {
  uint64 tmp = ((uint64)(readU4()));
  return tmp | (((uint64)(readU4())) << 32);
}

sint64 Reader::readS8() {
  sint64 tmp = ((sint64)(readS8()));
  return tmp | (((sint64)(readS8())) << 32);
}

Reader::Reader(ByteCode* array, uint32 start, uint32 end) {
  if (!end) 
		end = array->size;

  bytes = array;
  cursor = start;
  min = start;
  max = start + end;
}

unsigned int Reader::tell() {
  return cursor - min;
}

// Reader* Reader::derive(uint32 nbb) {
//   return new(allocator, "Reader") Reader(allocator, bytes, cursor, nbb);
// }

void Reader::seek(uint32 pos, int from) {
  uint32 n = 0;
  uint32 start = min;
  uint32 end = max;
  
  if (from == SeekCur) n = cursor + pos;
  else if (from == SeekSet) n = start + pos;
  else if (from == SeekEnd) n = end + pos;
  

  if ((n < start) || (n > end))
    VMThread::get()->vm->unknownError("out of range %d %d", n, end);

  cursor = n;
}

void Reader::print(mvm::PrintBuffer* buf) const {
  buf->write("Reader<>");
}
