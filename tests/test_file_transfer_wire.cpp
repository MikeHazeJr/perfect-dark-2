#include "catch.hpp"
#include "file_transfer_wire.h"
#include "ed25519.h"
#include <array>
#include <cstring>
#include <limits>

namespace {
std::array<uint8_t, 32> seed{};
int32_t signTest(const void *message, uint32_t length, uint8_t *signature) {
    return ed25519Sign(seed.data(), message, length, signature);
}
void put32(uint8_t *p, uint32_t value) { for (int i=0;i<4;++i) p[i]=(uint8_t)(value>>(i*8)); }
void put64(uint8_t *p, uint64_t value) { for (int i=0;i<8;++i) p[i]=(uint8_t)(value>>(i*8)); }
void initialize(uint8_t *p, uint8_t kind=0, uint32_t sender=2002, uint32_t recipient=1001) {
    std::memset(p, 0, FT_WIRE_FRAME_LEN); std::memcpy(p, FT_WIRE_MAGIC, 5);
    p[5]=FT_WIRE_VERSION; p[6]=kind; put32(p+8,sender); put32(p+12,recipient); put64(p+16,123456);
}
std::array<uint8_t,32> publicKey() {
    seed.fill(7); std::array<uint8_t,32> pub{}; REQUIRE(ed25519DerivePubkey(seed.data(),pub.data())); return pub;
}
}
TEST_CASE("File transfer signed frame includes the complete signature without overwriting canaries", "[net][file-transfer][wire]") {
    auto pub=publicKey(); std::array<uint8_t,FT_WIRE_FRAME_LEN+32> guarded;
    REQUIRE(FT_WIRE_FRAME_LEN == 1320);
    for(uint8_t kind=0;kind<=4;++kind) {
        guarded.fill(0xa5); auto *frame=guarded.data()+16; initialize(frame,kind);
        REQUIRE(fileTransferWireSign(frame,FT_WIRE_FRAME_LEN,pub.data(),signTest));
        REQUIRE(fileTransferWireVerify(frame,FT_WIRE_FRAME_LEN,1001));
        for(int i=0;i<16;++i) { REQUIRE(guarded[i]==0xa5); REQUIRE(guarded[FT_WIRE_FRAME_LEN+16+i]==0xa5); }
    }
}
TEST_CASE("File transfer rejects every truncated frame and an oversized frame before signing or verifying", "[net][file-transfer][wire]") {
    auto pub=publicKey(); std::array<uint8_t,FT_WIRE_FRAME_LEN+1> frame{}; initialize(frame.data());
    const auto before=frame;
    for(size_t length=0;length<FT_WIRE_FRAME_LEN;++length) {
        REQUIRE_FALSE(fileTransferWireSign(frame.data(),length,pub.data(),signTest));
        REQUIRE_FALSE(fileTransferWireVerify(frame.data(),length,1001));
    }
    REQUIRE_FALSE(fileTransferWireSign(frame.data(),frame.size(),pub.data(),signTest));
    REQUIRE_FALSE(fileTransferWireVerify(frame.data(),frame.size(),1001));
    REQUIRE(frame==before);
}
TEST_CASE("File transfer header rejects old versions invalid kinds and wrong recipients", "[net][file-transfer][wire]") {
    auto pub=publicKey(); std::array<uint8_t,FT_WIRE_FRAME_LEN> frame{}; initialize(frame.data());
    REQUIRE(fileTransferWireSign(frame.data(),frame.size(),pub.data(),signTest));
    REQUIRE_FALSE(fileTransferWireVerify(frame.data(),frame.size(),999));
    REQUIRE_FALSE(fileTransferWireVerify(frame.data(),frame.size(),0));
    for(int variant=0;variant<6;++variant) {
        auto bad=frame;
        if(variant==0)bad[5]=1; if(variant==1)bad[6]=5; if(variant==2)bad[7]=1;
        if(variant==3)put32(bad.data()+8,0); if(variant==4)put32(bad.data()+12,0); if(variant==5)put64(bad.data()+16,0);
        REQUIRE_FALSE(fileTransferWireHeaderValid(bad.data(),bad.size(),1001));
        REQUIRE_FALSE(fileTransferWireSign(bad.data(),bad.size(),pub.data(),signTest));
    }
}
TEST_CASE("File transfer authenticates payload public key and final signature bytes", "[net][file-transfer][wire]") {
    auto pub=publicKey(); std::array<uint8_t,FT_WIRE_FRAME_LEN> frame{}; initialize(frame.data(),1);
    REQUIRE(fileTransferWireSign(frame.data(),frame.size(),pub.data(),signTest));
    for(size_t offset : {size_t(200),size_t(1223),size_t(FT_WIRE_PUBKEY_OFFSET),size_t(FT_WIRE_SIG_OFFSET),size_t(FT_WIRE_FRAME_LEN-1)}) {
        auto bad=frame; bad[offset]^=1;
        REQUIRE_FALSE(fileTransferWireVerify(bad.data(),bad.size(),1001));
    }
}
TEST_CASE("File transfer chunk geometry is derived from size before allocation", "[net][file-transfer][wire]") {
    constexpr uint64_t limit=250ull*1024*1024;
    for(uint64_t bytes : {1ull,1024ull,1025ull,250ull*1024*1024}) {
        const auto chunks=static_cast<uint32_t>((bytes-1)/1024+1);
        REQUIRE(fileTransferWireGeometry(bytes,chunks,limit));
        REQUIRE_FALSE(fileTransferWireGeometry(bytes,chunks+1,limit));
        REQUIRE_FALSE(fileTransferWireGeometry(bytes,chunks-1,limit));
    }
    REQUIRE_FALSE(fileTransferWireGeometry(0,1,limit));
    REQUIRE_FALSE(fileTransferWireGeometry(limit+1,256001,limit));
    REQUIRE_FALSE(fileTransferWireGeometry(1,UINT32_MAX,limit));
    REQUIRE_FALSE(fileTransferWireGeometry(UINT64_MAX,UINT32_MAX,UINT64_MAX));
}
TEST_CASE("File transfer chunk copy range never exceeds the payload or admitted file", "[net][file-transfer][wire]") {
    uint64_t offset=9; uint32_t length=9;
    REQUIRE(fileTransferWireChunkRange(1025,2,0,2,&offset,&length)); REQUIRE(offset==0); REQUIRE(length==1024);
    REQUIRE(fileTransferWireChunkRange(1025,2,1,2,&offset,&length)); REQUIRE(offset==1024); REQUIRE(length==1);
    for(int bad=0;bad<4;++bad) {
        offset=length=99;
        REQUIRE_FALSE(fileTransferWireChunkRange(bad==0?100000:1025,bad==0?1:2,bad==1?2:0,bad==2?3:2,bad==3?nullptr:&offset,&length));
        REQUIRE(length==0); if(bad!=3)REQUIRE(offset==0);
    }
}
TEST_CASE("File transfer controls bind the verified signer recipient and transfer together", "[net][file-transfer][wire]") {
    auto pub=publicKey(); std::array<uint8_t,FT_WIRE_FRAME_LEN> frame{};
    for(uint8_t kind : {uint8_t(3),uint8_t(4)}) {
        initialize(frame.data(),kind); REQUIRE(fileTransferWireSign(frame.data(),frame.size(),pub.data(),signTest));
        REQUIRE(fileTransferWireVerify(frame.data(),frame.size(),1001));
        REQUIRE(fileTransferWirePeerMatches(frame.data(),frame.size(),1001,2002,123456));
        REQUIRE_FALSE(fileTransferWirePeerMatches(frame.data(),frame.size(),1001,3003,123456));
        REQUIRE_FALSE(fileTransferWirePeerMatches(frame.data(),frame.size(),999,2002,123456));
        REQUIRE_FALSE(fileTransferWirePeerMatches(frame.data(),frame.size(),1001,2002,654321));
    }
    uint32_t advanced=0;
    REQUIRE(fileTransferWireAckProgress(UINT32_MAX,3,UINT32_MAX,&advanced)); REQUIRE(advanced==0);
    REQUIRE_FALSE(fileTransferWireAckProgress(1,3,UINT32_MAX,&advanced)); REQUIRE(advanced==1);
    REQUIRE_FALSE(fileTransferWireAckProgress(1,3,0,&advanced)); REQUIRE(advanced==1);
    REQUIRE_FALSE(fileTransferWireAckProgress(1,3,2,&advanced)); REQUIRE(advanced==1);
    REQUIRE(fileTransferWireAckProgress(1,3,1,&advanced)); REQUIRE(advanced==2);
    REQUIRE(fileTransferWireAckProgress(2,3,2,&advanced)); REQUIRE(advanced==3);
    REQUIRE_FALSE(fileTransferWireAckProgress(3,3,3,&advanced));
}
