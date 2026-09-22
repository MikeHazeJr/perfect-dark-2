#include "catch.hpp"
#include "file_transfer_storage.h"
#include "sha256.h"
#include "save_atomic.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <vector>
#include <string>
#include <cstring>

namespace {
namespace fs = std::filesystem;
struct Temp {
    fs::path dir;
    Temp() {
        const auto root = fs::weakly_canonical(fs::temp_directory_path());
        dir = root / ("pd2-ft-storage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        REQUIRE(dir.parent_path() == root);
        REQUIRE(fs::create_directory(dir));
    }
    ~Temp() { std::error_code error; fs::remove_all(dir,error); }
};
std::vector<uint8_t> data() { std::vector<uint8_t> bytes(4097); for(size_t i=0;i<bytes.size();++i)bytes[i]=(uint8_t)(i*37+11); return bytes; }
ft_receipt_key_t keyFor(const std::vector<uint8_t>& bytes) {
    ft_receipt_key_t key{}; key.local=1001; key.peer=2002; key.id=1; key.bytes=bytes.size(); key.chunks=(uint32_t)((bytes.size()+1023)/1024);
    sha256Hash(bytes.data(),bytes.size(),key.digest); key.request_digest[0]=7; return key;
}
std::string read(const fs::path &path) { std::ifstream in(path,std::ios::binary); return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()}; }
size_t candidates(const fs::path &path) { size_t count=0; for(const auto&e:fs::directory_iterator(path))if(e.path().filename().string().find(".pd2tmp-")!=std::string::npos)++count; return count; }
}
TEST_CASE("File transfer records a saved receipt only after successful atomic commit", "[net][file-transfer][storage]") {
    Temp temp; const auto path=temp.dir/"inbound.bin"; auto bytes=data(); auto key=keyFor(bytes); ft_receipt_store_t store{}; char error[192];
    saveAtomicDebugFailNextCommit();
    REQUIRE_FALSE(fileTransferStoreCommit(&store,&key,path.string().c_str(),bytes.data(),bytes.size(),100,error,sizeof(error)));
    REQUIRE(std::strlen(error)>0); REQUIRE_FALSE(fs::exists(path)); REQUIRE(candidates(temp.dir)==0);
    REQUIRE(fileTransferReceiptFind(&store,key.local,key.peer,key.id,100)==nullptr);
    REQUIRE(fileTransferStoreCommit(&store,&key,path.string().c_str(),bytes.data(),bytes.size(),120,error,sizeof(error)));
    REQUIRE(read(path)==std::string((const char*)bytes.data(),bytes.size())); REQUIRE(candidates(temp.dir)==0);
    REQUIRE(fileTransferReceiptFind(&store,key.local,key.peer,key.id,120)!=nullptr);
}
TEST_CASE("File transfer rejects corrupted bytes and preserves conflicting inbox content", "[net][file-transfer][storage]") {
    Temp temp; const auto path=temp.dir/"inbound.bin"; auto bytes=data(); auto key=keyFor(bytes); ft_receipt_store_t store{}; char error[192];
    bytes[0]^=1;
    REQUIRE_FALSE(fileTransferStoreCommit(&store,&key,path.string().c_str(),bytes.data(),bytes.size(),100,error,sizeof(error)));
    REQUIRE_FALSE(fs::exists(path)); bytes[0]^=1;
    std::ofstream(path,std::ios::binary)<<"prior local content";
    REQUIRE_FALSE(fileTransferStoreCommit(&store,&key,path.string().c_str(),bytes.data(),bytes.size(),100,error,sizeof(error)));
    REQUIRE(read(path)=="prior local content"); REQUIRE(candidates(temp.dir)==0);
    REQUIRE(fileTransferReceiptFind(&store,key.local,key.peer,key.id,100)==nullptr);
}
TEST_CASE("File transfer saved receipts match exact metadata and final chunk content", "[net][file-transfer][storage]") {
    Temp temp; const auto path=temp.dir/"inbound.bin"; auto bytes=data(); auto key=keyFor(bytes); ft_receipt_store_t store{}; char error[192];
    REQUIRE(fileTransferStoreCommit(&store,&key,path.string().c_str(),bytes.data(),bytes.size(),100,error,sizeof(error)));
    auto receipt=fileTransferReceiptFind(&store,key.local,key.peer,key.id,101); REQUIRE(receipt);
    REQUIRE(fileTransferReceiptMatchesInit(receipt,&key)); auto other=key; other.request_digest[0]^=1;
    REQUIRE_FALSE(fileTransferReceiptMatchesInit(receipt,&other)); other=key; other.digest[0]^=1;
    REQUIRE_FALSE(fileTransferReceiptMatchesInit(receipt,&other));
    REQUIRE(fileTransferReceiptMatchesChunk(receipt,5,4,bytes.data()+4096,1));
    REQUIRE_FALSE(fileTransferReceiptMatchesChunk(receipt,5,4,bytes.data()+4096,0));
    REQUIRE_FALSE(fileTransferReceiptMatchesChunk(receipt,4,4,bytes.data()+4096,1));
    REQUIRE_FALSE(fileTransferReceiptMatchesChunk(receipt,5,0,bytes.data(),1024));
    bytes[4096]^=1; REQUIRE_FALSE(fileTransferReceiptMatchesChunk(receipt,5,4,bytes.data()+4096,1));
    REQUIRE(fileTransferReceiptFind(&store,9009,key.peer,key.id,101)==nullptr);
    REQUIRE(fileTransferReceiptFind(&store,key.local,3003,key.id,101)==nullptr);
}
TEST_CASE("File transfer receipt cache is bounded expires safely and clears with its owner", "[net][file-transfer][storage]") {
    Temp temp; const auto path=temp.dir/"inbound.bin"; auto bytes=data(); auto key=keyFor(bytes); ft_receipt_store_t store{}; char error[192];
    REQUIRE(fileTransferStoreCommit(&store,&key,path.string().c_str(),bytes.data(),bytes.size(),UINT32_MAX-10,error,sizeof(error)));
    REQUIRE(fileTransferReceiptFind(&store,key.local,key.peer,key.id,20));
    REQUIRE_FALSE(fileTransferReceiptFind(&store,key.local,key.peer,key.id,60000));
    for(uint64_t id=2;id<=FT_RECEIPT_CAPACITY+1;++id) {
        key.id=id; REQUIRE(fileTransferStoreCommit(&store,&key,path.string().c_str(),bytes.data(),bytes.size(),100,error,sizeof(error)));
    }
    REQUIRE_FALSE(fileTransferReceiptFind(&store,key.local,key.peer,1,100));
    REQUIRE(fileTransferReceiptFind(&store,key.local,key.peer,key.id,100));
    std::memset(&store,0,sizeof(store)); REQUIRE_FALSE(fileTransferReceiptFind(&store,key.local,key.peer,key.id,100));
}
TEST_CASE("File transfer inbox names cannot become remote paths or reserved device names", "[net][file-transfer][storage]") {
    auto bytes=data(); auto key=keyFor(bytes); char folder[16],file[96];
    for(const char* name : {"../../CON","..","C:\\outside\\a.txt","bad\"name\n.pdmod",""}) {
        REQUIRE(fileTransferStorageNames(2002,name,key.digest,folder,sizeof(folder),file,sizeof(file)));
        REQUIRE(std::string(folder)=="000007d2"); REQUIRE(std::strlen(file)>32);
        REQUIRE(std::string(file).find_first_of("/\\:\"\n") == std::string::npos);
        REQUIRE(file[32]=='_'); REQUIRE(std::string(file)!="CON");
    }
    const auto longName=std::string(90,'a')+".pdmod";
    REQUIRE(fileTransferStorageNames(2002,longName.c_str(),key.digest,folder,sizeof(folder),file,sizeof(file)));
    REQUIRE(std::string(file).substr(std::strlen(file)-6)==".pdmod");
    auto original=std::string(file); key.digest[0]^=1;
    REQUIRE(fileTransferStorageNames(2002,longName.c_str(),key.digest,folder,sizeof(folder),file,sizeof(file)));
    REQUIRE(std::string(file)!=original);
    REQUIRE_FALSE(fileTransferStorageNames(2002,"a",key.digest,folder,8,file,sizeof(file)));
    REQUIRE_FALSE(fileTransferStorageNames(2002,"a",key.digest,folder,sizeof(folder),file,32));
}
