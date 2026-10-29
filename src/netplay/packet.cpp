#include "netplay/packet.h"

#include <cstring>
#include <utility>

namespace ssf2t::netplay {
namespace {

class Writer {
 public:
  Writer(uint8_t* out, size_t capacity) : out_(out), capacity_(capacity) {}

  void U8(uint8_t v) { Put(&v, 1); }
  void U16(uint16_t v) {
    const uint8_t b[2] = {uint8_t(v >> 8), uint8_t(v)};
    Put(b, 2);
  }
  void U32(uint32_t v) {
    const uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    Put(b, 4);
  }
  bool ok() const { return ok_; }
  size_t size() const { return size_; }

 private:
  void Put(const uint8_t* b, size_t n) {
    if (size_ + n > capacity_) {
      ok_ = false;
      return;
    }
    std::memcpy(out_ + size_, b, n);
    size_ += n;
  }

  uint8_t* out_;
  size_t capacity_;
  size_t size_ = 0;
  bool ok_ = true;
};

class Reader {
 public:
  Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  uint8_t U8() { return Take(1) ? data_[pos_ - 1] : 0; }
  uint16_t U16() {
    if (!Take(2)) return 0;
    return uint16_t(data_[pos_ - 2] << 8 | data_[pos_ - 1]);
  }
  uint32_t U32() {
    if (!Take(4)) return 0;
    return uint32_t(data_[pos_ - 4]) << 24 | uint32_t(data_[pos_ - 3]) << 16 | uint32_t(data_[pos_ - 2]) << 8 |
           data_[pos_ - 1];
  }
  bool ok() const { return ok_; }
  bool done() const { return pos_ == size_; }
  size_t remaining() const { return size_ - pos_; }

 private:
  bool Take(size_t n) {
    if (pos_ + n > size_) {
      ok_ = false;
      return false;
    }
    pos_ += n;
    return true;
  }

  const uint8_t* data_;
  size_t size_;
  size_t pos_ = 0;
  bool ok_ = true;
};

constexpr bool ValidGuest(uint8_t v) {
  return v == uint8_t(GuestId::kBase013) || v == uint8_t(GuestId::kTitleUpdate023);
}

void WriteBody(Writer& w, const Hello& b) {
  w.U8(uint8_t(b.guest));
  w.U8(0);
  w.U16(b.build);
  w.U32(b.nonce);
}

void WriteBody(Writer& w, const HelloAck& b) {
  w.U8(uint8_t(b.reject));
  w.U8(b.joiner_slot);
  w.U8(b.input_delay);
  w.U8(0);
  w.U32(b.nonce);
  w.U32(b.start_frame);
}

void WriteBody(Writer& w, const Input& b) {
  w.U32(b.next_wanted);
  w.U32(b.first);
  w.U8(uint8_t(b.words.size()));
  w.U8(0);
  for (const uint16_t word : b.words) w.U16(word);
}

void WriteBody(Writer& w, const Checksum& b) {
  w.U32(b.frame);
  w.U32(b.value);
}

void WriteBody(Writer& w, const Bye& b) { w.U8(uint8_t(b.reason)); }

std::optional<Body> ReadHello(Reader& r) {
  Hello b;
  const uint8_t guest = r.U8();
  r.U8();
  b.build = r.U16();
  b.nonce = r.U32();
  if (!r.ok() || !r.done() || !ValidGuest(guest)) return std::nullopt;
  b.guest = GuestId(guest);
  return b;
}

std::optional<Body> ReadHelloAck(Reader& r) {
  HelloAck b;
  const uint8_t reject = r.U8();
  b.joiner_slot = r.U8();
  b.input_delay = r.U8();
  r.U8();
  b.nonce = r.U32();
  b.start_frame = r.U32();
  if (!r.ok() || !r.done() || reject > uint8_t(RejectReason::kBusy) || b.joiner_slot > 1) return std::nullopt;
  b.reject = RejectReason(reject);
  return b;
}

std::optional<Body> ReadInput(Reader& r) {
  Input b;
  b.next_wanted = r.U32();
  b.first = r.U32();
  const uint8_t count = r.U8();
  r.U8();
  if (!r.ok() || count > kMaxInputsPerPacket || r.remaining() != size_t(count) * 2) return std::nullopt;
  b.words.reserve(count);
  for (uint8_t i = 0; i < count; ++i) b.words.push_back(r.U16());
  return b;
}

std::optional<Body> ReadChecksum(Reader& r) {
  Checksum b;
  b.frame = r.U32();
  b.value = r.U32();
  if (!r.ok() || !r.done()) return std::nullopt;
  return b;
}

std::optional<Body> ReadBye(Reader& r) {
  Bye b;
  const uint8_t reason = r.U8();
  if (!r.ok() || !r.done() || reason > uint8_t(ByeReason::kError)) return std::nullopt;
  b.reason = ByeReason(reason);
  return b;
}

}  // namespace

PacketType TypeOf(const Body& body) {
  struct Visitor {
    PacketType operator()(const Hello&) const { return PacketType::kHello; }
    PacketType operator()(const HelloAck&) const { return PacketType::kHelloAck; }
    PacketType operator()(const Input&) const { return PacketType::kInput; }
    PacketType operator()(const Checksum&) const { return PacketType::kChecksum; }
    PacketType operator()(const Bye&) const { return PacketType::kBye; }
  };
  return std::visit(Visitor{}, body);
}

size_t Encode(const Header& header, const Body& body, uint8_t* out, size_t capacity) {
  if (const Input* input = std::get_if<Input>(&body); input != nullptr && input->words.size() > kMaxInputsPerPacket) {
    return 0;
  }
  Writer w(out, capacity);
  w.U32(kPacketMagic);
  w.U8(kProtocolVersion);
  w.U8(uint8_t(TypeOf(body)));
  w.U16(0);
  w.U32(header.session);
  w.U16(header.seq);
  w.U16(header.ack);
  std::visit([&w](const auto& b) { WriteBody(w, b); }, body);
  return w.ok() ? w.size() : 0;
}

std::optional<Packet> Decode(const uint8_t* data, size_t size) {
  if (data == nullptr || size < kHeaderSize || size > kMaxPacketSize) return std::nullopt;
  Reader r(data, size);
  if (r.U32() != kPacketMagic || r.U8() != kProtocolVersion) return std::nullopt;
  const uint8_t type = r.U8();
  r.U16();
  Packet packet;
  packet.header.session = r.U32();
  packet.header.seq = r.U16();
  packet.header.ack = r.U16();
  std::optional<Body> body;
  switch (PacketType(type)) {
    case PacketType::kHello:
      body = ReadHello(r);
      break;
    case PacketType::kHelloAck:
      body = ReadHelloAck(r);
      break;
    case PacketType::kInput:
      body = ReadInput(r);
      break;
    case PacketType::kChecksum:
      body = ReadChecksum(r);
      break;
    case PacketType::kBye:
      body = ReadBye(r);
      break;
    default:
      return std::nullopt;
  }
  if (!body) return std::nullopt;
  packet.body = std::move(*body);
  return packet;
}

}  // namespace ssf2t::netplay
