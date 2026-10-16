#pragma once
#include "archive.h"
#include "append_archive.h"
#include <atomic>
#include <chrono>
#include <sstream>
namespace movement::neuralio {
namespace fs=std::filesystem;
class Input:public std::istream {
 std::unique_ptr<std::istream> owner;
public:
 explicit Input(const fs::path&p,uint64_t limit=archive::DefaultLimit,archive::Cancel cancel={}):std::istream(nullptr){
  if(appendlog::IsAppend(p))owner=std::make_unique<appendlog::Input>(p,limit,std::move(cancel));
  else owner=std::make_unique<archive::Input>(p,limit,std::move(cancel));
  rdbuf(owner->rdbuf());exceptions(std::ios::badbit);
 }
};
inline uint64_t LogicalSize(const fs::path&p,uint64_t limit=archive::DefaultLimit,const archive::Cancel&cancel={}){
 Input in(p,limit,cancel);std::array<char,65536>b{};uint64_t bytes=0;while(in){archive::Poll(cancel);in.read(b.data(),b.size());auto n=uint64_t(in.gcount());archive::Require(n<=limit-bytes,"Neural input exceeds logical size limit");bytes+=n;}archive::Require(in.eof(),"Neural input read failed");return bytes;
}
inline void Same(const fs::path&a,const fs::path&b){Input left(a),right(b);std::array<char,65536>x{},y{};while(left){left.read(x.data(),x.size());right.read(y.data(),y.size());auto n=left.gcount();archive::Require(n==right.gcount()&&std::equal(x.begin(),x.begin()+n,y.begin()),"Neural conversion verification failed");}archive::Require(left.eof()&&right.eof(),"Neural conversion read failed");}
inline std::string Token(){static std::atomic<uint64_t>serial=0;return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial);}
// Only called on the selected new working folder, never the original Learning tree.
// Preserve the complete previous logical stream as a compressed backup on conversion.
inline void EnsureAppend(const fs::path&p){
 if(!fs::exists(p))return;
 if(appendlog::IsAppend(p)){auto report=appendlog::Recover(p);if(report.repaired){auto notice=layout::Managed(p.parent_path())?recovery::Backup(p.parent_path(),"AppendRepair","append-recovery-"+Token()+".txt"):p.parent_path()/"append-recovery.txt";auto temp=notice;temp+=".tmp-"+Token();auto name=report.backup.generic_u8string();std::ofstream out(temp,std::ios::binary);out<<"Recovered complete Zstandard frames; retained full damaged file: "<<std::string(name.begin(),name.end())<<"\nCommitted logical bytes: "<<report.logicalBytes<<"\nIncomplete physical tail bytes: "<<(report.physicalBytes-report.validBytes)<<"\n";archive::Require(bool(out.flush()),"Cannot write append recovery notice");out.close();archive::Flush(temp);archive::AtomicReplace(temp,notice);}return;}
 auto token=Token();auto stage=p;stage+=".append-stage-"+token;auto backup=recovery::Backup(p.parent_path(),"BeforeAppend",p.filename().string()+".before-append-"+token+".ncz");
 auto bytes=LogicalSize(p);auto physical=fs::file_size(p);auto time=fs::last_write_time(p);
 {Input in(p);appendlog::CreateFrom(in,bytes,stage);}Same(p,stage);
 {Input in(p);archive::Pack(in,bytes,backup);}Same(p,backup);
 archive::Require(fs::file_size(p)==physical&&fs::last_write_time(p)==time,"Working source changed during compression");archive::AtomicReplace(stage,p);
}
}
