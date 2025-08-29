/***********************************************************************
 *
 * Copyright 2025 Austin Li <atl63@cornell.edu>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "autobahn_callback.h"
#include "lib/transport.h"
#include "lib/repltransport.h"
#include "lib/assert.h"
#include "lib/message.h"
#include "store/common/util.h"
#include <iostream>
#include <string>

namespace autobahn {

void autobahn_callback(int64_t handle, uint64_t slot_num, rust::Slice<const uint8_t> buf) {
  TransportReceiver* replica = reinterpret_cast<TransportReceiver*>(handle);
  ReplTransportAddress* repl_addr = new ReplTransportAddress("client", "");

  // parse like in tcptransport.cc
  size_t capacity = buf.size();
  const char *req = reinterpret_cast<const char*>(buf.data());

  const uint32_t *magic = reinterpret_cast<const uint32_t*>(req);
  UW_ASSERT(*magic == MAGIC);

  const size_t *sz = (const size_t*) (req + sizeof(*magic));

  size_t totalSize = *sz;
  UW_ASSERT(totalSize < 1073741826);
  UW_ASSERT(totalSize == capacity);

  const char *ptr = req + sizeof(*sz) + sizeof(*magic);

  size_t typeLen = *((const size_t *)ptr);
  ptr += sizeof(size_t);
  UW_ASSERT((size_t)(ptr-req) < totalSize);

  UW_ASSERT((size_t)(ptr+typeLen-req) < totalSize);
  std::string msgType(ptr, typeLen);
  ptr += typeLen;

  size_t msgLen = *((const size_t *)ptr);
  ptr += sizeof(size_t);
  UW_ASSERT((size_t)(ptr-req) < totalSize);

  UW_ASSERT((size_t)(ptr+msgLen-req) <= totalSize);
  std::string msg(ptr, msgLen);
  ptr += msgLen;
  Debug("start sending the message to the receiver!");

  Debug("slot_num: %lu, msgType: %s", slot_num, msgType.c_str());

  replica->ReceiveMessage(*repl_addr, msgType, msg, nullptr);
}

} // namespace autobahn
