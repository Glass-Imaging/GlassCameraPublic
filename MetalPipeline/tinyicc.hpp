// TinyICC
// Copyright(c) 2019 by nikq/Hajime UCHIMURA.
// support reading/writing ICC profiles.

#ifndef __TINYICC_H_20190724_5B186D8F9924
#define __TINYICC_H_20190724_5B186D8F9924

#include <stdint.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace TinyICC {
typedef enum {
    TYPE_XYZ = 0x58595A20,   // 'XYZ '
    TYPE_PARA = 0,           // 'para'
    TYPE_RGB = 1,            // 'RGB '
    TAG_WTPT = 0x77747074,   // 'wtpt'
    TAG_R_XYZ = 0x7258595A,  // 'rXYZ'
    TAG_R_TRC = 0x72545243,  // 'rTRC'
    TAG_G_XYZ = 0x6758595A,  // 'gXYZ'
    TAG_G_TRC = 0x67545243,  // 'gTRC'
    TAG_B_XYZ = 0x6258595A,  // 'bXYZ'
    TAG_B_TRC = 0x62545243,  // 'bTRC'
} TYPES;

class Profile {
   public:
    struct DateTime {
        uint16_t year_;
        uint16_t month_;
        uint16_t day_;
        uint16_t hour_;
        uint16_t min_;
        uint16_t sec_;
    };

    struct XYZ {
        float x_;
        float y_;
        float z_;
    };

    struct Tag {
        uint32_t type_;
        std::vector<uint8_t> data_;
        Tag() { ; }
        Tag(const uint32_t type, std::vector<uint8_t> data) : type_(type), data_(data) { ; }
    };

    uint32_t profileSize_;
    uint32_t cmmType_;
    uint32_t profileVersion_;
    uint32_t profileClass_;
    uint32_t colorSpace_;
    uint32_t connectionSpace_;
    DateTime dtime_;
    uint32_t acsp_;
    uint32_t platform_;
    uint32_t flags_;
    uint32_t deviceManufacture_;
    uint32_t deviceModel_;
    uint64_t deviceAttrib_;
    uint32_t intent_;
    XYZ connectionIllum_;
    uint32_t creator_;

    std::map<uint32_t, Tag> tags_;

    static std::array<float, 3> parseXYZTag(const Profile::Tag& t);

    void dumpTag4(const char* name, const uint32_t u) {
        printf("%16s %c%c%c%c\n", name, (u >> 24) & 0xFF, (u >> 16) & 0xFF, (u >> 8) & 0xFF, u & 0xFF);
    }
    void dump(void) {
        printf("%16s %x\n", "size", profileSize_);
        dumpTag4("cmmType", cmmType_);
        printf("%16s %x(version %d)\n", "version", profileVersion_, (profileVersion_ >> 24) & 0xFF);
        dumpTag4("profileClass", profileClass_);
        dumpTag4("colorSpace", colorSpace_);
        dumpTag4("connectSpace", connectionSpace_);
        printf("%16s %d-%d-%d,%d:%d,%d\n", "date", dtime_.year_, dtime_.month_, dtime_.day_, dtime_.hour_, dtime_.min_,
               dtime_.sec_);
        dumpTag4("sig acsp", acsp_);
        dumpTag4("platform", platform_);
        printf("%16s %x\n", "flags", flags_);
        dumpTag4("manufacture", deviceManufacture_);
        dumpTag4("model", deviceModel_);
        printf("%16s %llx\n", "deviceAttrib", deviceAttrib_);
        printf("%16s %d\n", "intent", intent_);
        printf("%16s (%f,%f,%f)\n", "connectIllum", connectionIllum_.x_, connectionIllum_.y_, connectionIllum_.z_);
        dumpTag4("creator", creator_);
        for (const auto& t : tags_) {
            union {
                uint32_t data;
                uint8_t c[4];
            } tag, typ;

            tag.data = t.first;
            typ.data = t.second.type_;

            printf("%c%c%c%c:%c%c%c%c\n", tag.c[3], tag.c[2], tag.c[1], tag.c[0], typ.c[3], typ.c[2], typ.c[1], typ.c[0]);

            for (const auto& c : t.second.data_) {
                printf("%02x ", c);
            }
            printf("\n");
        }
    }
};

class Stream {
   public:
    const uint8_t* buffer_;
    size_t size_;
    size_t index_;

    Stream(const uint8_t* buffer, const size_t size, const size_t index = 0)
        : buffer_(buffer), size_(size), index_(index) {}
    virtual ~Stream(void) {
        buffer_ = NULL;
        size_ = 0;
    }

    bool eof(void) const { return index_ >= size_; }
    void skip(const size_t bytes) {
        index_ += bytes;
        if (index_ > size_) index_ = size_ - 1;
    }

    const uint32_t seek(const size_t index) {
        const size_t current = index_;
        index_ = index;
        if (index_ > size_) index_ = size_ - 1;
        return (uint32_t) current;
    }

    const uint8_t uint8(void) {
        if (index_ >= size_) return 0;  // EOF
        const uint8_t ret = buffer_[index_];
        index_++;
        return ret;
    }
    const int8_t int8(void) { return (int8_t)uint8(); }

    // ICC profile uses big endian only.
    const uint16_t uint16(void) { return (uint8() << 8) | (uint8()); }
    const int16_t int16(void) { return (int8() << 8) | (uint8()); }
    const uint32_t uint32(void) { return (uint16() << 16) | (uint16()); }
    const int32_t int32(void) { return (int16() << 16) | (uint16()); }
    const uint64_t uint64(void) { return ((uint64_t)uint32() << 32) | (uint32()); }

    const float s15Fixed16(void) { return (float)int32() / 0x10000; }
    const float u16Fixed16(void) { return (float)uint32() / 0x10000; }
    const float u8Fixed8(void) { return (float)uint16() / 0x100; }

    std::vector<uint8_t> array(const size_t s) {
        std::vector<uint8_t> ret(s);
        std::copy(buffer_ + index_, buffer_ + index_ + s, ret.begin());
        return ret;
    }

    const Profile::DateTime dateTime(void) {
        Profile::DateTime dt;
        dt.year_ = uint16();
        dt.month_ = uint16();
        dt.day_ = uint16();
        dt.hour_ = uint16();
        dt.min_ = uint16();
        dt.sec_ = uint16();
        return dt;
    }

    const Profile::XYZ xyz(void) {
        Profile::XYZ xyz;
        xyz.x_ = s15Fixed16();
        xyz.y_ = s15Fixed16();
        xyz.z_ = s15Fixed16();
        return xyz;
    }
};

inline static bool loadFromMem(Profile& p, const uint8_t* buffer, const size_t size) {
    Stream stream(buffer, size);
    p.profileSize_ = stream.uint32();
    p.cmmType_ = stream.uint32();
    p.profileVersion_ = stream.uint32();
    p.profileClass_ = stream.uint32();
    p.colorSpace_ = stream.uint32();
    p.connectionSpace_ = stream.uint32();
    p.dtime_ = stream.dateTime();
    p.acsp_ = stream.uint32();  // must be acsp
    p.platform_ = stream.uint32();
    p.flags_ = stream.uint32();
    p.deviceManufacture_ = stream.uint32();
    p.deviceModel_ = stream.uint32();
    p.deviceAttrib_ = stream.uint64();
    p.intent_ = stream.uint32();
    p.connectionIllum_ = stream.xyz();
    p.creator_ = stream.uint32();
    stream.seek(128);  // header

    // Sanity check
    if (p.acsp_ != 0x61637370) {
        // not valid Signature 'acsp'
        return false;
    }
    if (stream.eof()) return false;

    uint32_t tagCount = stream.uint32();
    for (uint32_t u = 0; u < tagCount; u++) {
        uint32_t sig = stream.uint32();
        uint32_t offs = stream.uint32();
        uint32_t size = stream.uint32();
        size_t current = stream.seek(offs);
        p.tags_[sig] = Profile::Tag(stream.uint32(), stream.array(size));
        stream.seek(current);
    }

    return true;
}

inline std::array<float, 3> Profile::parseXYZTag(const Profile::Tag& t) {
    auto stream = Stream(t.data_.data(), t.data_.size());

    const auto zero = stream.s15Fixed16();
    assert(zero == 0.0);

    std::array<float, 3> data = {
        stream.s15Fixed16(),
        stream.s15Fixed16(),
        stream.s15Fixed16()
    };
    return data;
}

}  // namespace TinyICC

#endif
