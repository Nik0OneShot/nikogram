#pragma once
#include "archive.h"
#include <atomic>
#include <chrono>
#include <sstream>
#include <cctype>
namespace movement::layout {
namespace fs=std::filesystem;
// Windows internal migration IO must also reach retained paths beyond MAX_PATH.
// Public routing helpers keep ordinary paths; only native filesystem operations
// receive the extended absolute spelling.
inline fs::path Native(const fs::path&p){
#ifdef _WIN32
 auto absolute=fs::absolute(p).lexically_normal();auto value=absolute.native();if(value.starts_with(L"\\\\?\\"))return absolute;if(value.starts_with(L"\\\\"))return fs::path(L"\\\\?\\UNC\\"+value.substr(2));return fs::path(L"\\\\?\\"+value);
#else
 return p;
#endif
}

inline std::string Token(){static std::atomic<uint64_t>n{0};return std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+std::to_string(++n);}
inline bool Digits(const std::string&s){return !s.empty()&&s.size()<80&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';});}
inline void Safe(const fs::path&p){auto ordinary=p;
#ifdef _WIN32
 auto value=ordinary.native();if(value.starts_with(L"\\\\?\\UNC\\"))ordinary=fs::path(L"\\\\"+value.substr(8));else if(value.starts_with(L"\\\\?\\"))ordinary=fs::path(value.substr(4));
#endif
 for(auto q=fs::absolute(ordinary);!q.empty();q=q.parent_path()) {auto native=Native(q);if(fs::exists(native)){
#ifdef _WIN32
 auto attributes=GetFileAttributesW(native.c_str());archive::Require(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT),"Managed layout rejects reparse paths");
#else
 archive::Require(!fs::is_symlink(native),"Managed layout rejects symbolic links");
#endif
 }auto parent=q.parent_path();if(parent==q)break;}}
inline std::string Marker(const fs::path&working){auto p=Native(working)/"managed-layout-v1.txt";if(!fs::exists(p))return {};Safe(p);archive::Require(fs::file_size(p)<256,"Invalid managed layout marker");std::ifstream in(p);std::string magic,token;int version=0;archive::Require(bool(in>>magic>>version>>token)&&magic=="NIKO_LAYOUT"&&version==1&&Digits(token),"Invalid managed layout marker");in>>std::ws;archive::Require(in.eof(),"Unexpected managed layout marker");return token;}
inline bool Managed(const fs::path&working){return !Marker(working).empty();}
inline fs::path Working(const fs::path&base){return base/"Internal";}
inline void Leaf(const fs::path&leaf){archive::Require(!leaf.empty()&&!leaf.is_absolute()&&!leaf.has_root_name(),"Invalid managed relative path");for(auto&e:leaf)archive::Require(e!=".."&&e!=".","Invalid managed relative path");}
inline fs::path Transfer(const fs::path&working,const fs::path&leaf){Leaf(leaf);auto p=(Managed(working)?working.parent_path()/"Transfers":working)/leaf;Safe(p);return p;}
inline fs::path Recovery(const fs::path&working,const fs::path&leaf){Leaf(leaf);auto p=(Managed(working)?working.parent_path()/"Recovery":working)/leaf;Safe(p);return p;}
inline fs::path Previous(const fs::path&file){auto p=Managed(file.parent_path())?Recovery(file.parent_path(),"PreviousFiles")/file.filename():file;p+=".previous";return p;}
inline fs::path Unique(const fs::path&p){if(!fs::exists(p))return p;for(int i=0;i<100;++i){auto q=p;q+="-"+Token();if(!fs::exists(q))return q;}throw std::runtime_error("Cannot allocate unique recovery path");}
inline void Text(const fs::path&p,const std::string&s){Safe(p);fs::create_directories(p.parent_path());auto t=p;t+=".tmp-"+Token();{std::ofstream out(t,std::ios::binary);archive::Require(bool(out<<s)&&bool(out.flush()),"Cannot write layout journal");}archive::Flush(t);archive::AtomicReplace(t,p);}
inline void MoveNew(const fs::path&from,const fs::path&to){Safe(from);Safe(to);archive::Require(!fs::exists(to),"Layout destination exists; retained both copies");
#ifdef _WIN32
 archive::Require(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_WRITE_THROUGH)!=0,"Cannot move managed layout directory");
#else
 fs::rename(from,to);
#endif
}
inline fs::path Journal(const fs::path&base){auto p=base;p+=".layout.txt";return p;}
inline fs::path Destination(const fs::path&relative);
inline void ValidateCopy(const fs::path&source,const fs::path&stage);
inline void RecoverUpgrade(const fs::path&requestedBase){const auto base=Native(requestedBase);auto j=Journal(base);Safe(base);Safe(j);if(!fs::exists(j))return;archive::Require(fs::file_size(j)<512,"Invalid layout journal");std::ifstream in(j);std::string magic,phase,token;int version=0;archive::Require(bool(in>>magic>>version>>phase>>token)&&magic=="NIKO_LAYOUT_TRANSACTION"&&version==1&&Digits(token)&&(phase=="PREPARED"||phase=="COMMITTED"),"Invalid layout journal; no fallback allowed");in>>std::ws;archive::Require(in.eof(),"Unexpected layout journal");in.close();
 auto stage=base;stage+=".layout-stage-"+token;auto old=base;old+=".before-layout-"+token;auto retained=base/"Recovery"/("BeforeLayout-"+token);Safe(stage);Safe(old);Safe(retained);
 if(phase=="COMMITTED"){archive::Require(Marker(base/"Internal")==token&&fs::is_directory(retained),"Committed managed layout missing; refusing older fallback");return;}
 if(fs::exists(stage)){archive::Require(Marker(stage/"Internal")==token,"Prepared layout stage identity mismatch");if(fs::exists(base)){archive::Require(!fs::exists(old)&&!Managed(base/"Internal"),"Ambiguous layout swap; all copies retained");ValidateCopy(base,stage);MoveNew(base,old);}archive::Require(fs::is_directory(old),"Original flat layout missing; refusing publish");ValidateCopy(old,stage);MoveNew(stage,base);}
 archive::Require(Marker(base/"Internal")==token,"Prepared layout missing; refusing older fallback");
 if(fs::exists(old)){ValidateCopy(old,base);archive::Require(!fs::exists(retained),"Duplicate layout recovery tree; both retained");MoveNew(old,retained);}archive::Require(fs::is_directory(retained),"Original flat layout recovery copy missing");ValidateCopy(retained,base);Text(j,"NIKO_LAYOUT_TRANSACTION 1\nCOMMITTED "+token+"\n");
}
inline void ExactCopy(const fs::path&requestedFrom,const fs::path&requestedTo){const auto from=Native(requestedFrom),to=Native(requestedTo);Safe(from);Safe(to);archive::Require(fs::is_regular_file(from),"Unsupported layout source file");fs::create_directories(to.parent_path());archive::Require(!fs::exists(to),"Layout mapping collision; originals retained");auto size=fs::file_size(from);archive::Require(size<=archive::DefaultLimit,"Layout file exceeds 32 GiB bound; original retained");auto time=fs::last_write_time(from);fs::copy_file(from,to);archive::Flush(to);std::ifstream a(from,std::ios::binary),b(to,std::ios::binary);std::array<char,65536>x{},y{};while(a){a.read(x.data(),x.size());auto n=a.gcount();b.read(y.data(),n);archive::Require(b.gcount()==n&&std::equal(x.begin(),x.begin()+n,y.begin()),"Layout copy differs");}archive::Require(a.eof()&&!a.bad()&&b.peek()==EOF&&!b.bad()&&fs::file_size(from)==size&&fs::last_write_time(from)==time,"Layout source changed; originals retained");}
inline fs::path Destination(const fs::path&relative){auto it=relative.begin();auto first=it->string();auto category=first;
#ifdef _WIN32
 std::transform(category.begin(),category.end(),category.begin(),[](unsigned char c){return char(std::tolower(c));});
#else
 // These are application-owned category names; matching spelling is portable.
#endif
 fs::path tail;for(++it;it!=relative.end();++it)tail/=*it;
 if(tail.empty()){
  if(category.find(".before-append-")!=std::string::npos)return fs::path("Recovery")/"AppendConversion"/first;
  if(category.find(".torn-backup-")!=std::string::npos)return fs::path("Recovery")/"AppendRepair"/first;
  if(category.starts_with("import-backup-")||category=="observations.txt.import-backup")return fs::path("Recovery")/"Imports"/first;
  if(category.starts_with("player-reset-")&&category.ends_with(".backup"))return fs::path("Recovery")/"PlayerResets"/first;
  if(category.find(".previous-")!=std::string::npos)return fs::path("Recovery")/"PreviousHistory"/first;
 }
 if(tail.empty()&&category.ends_with(".previous"))return fs::path("Recovery")/"PreviousFiles"/first;
 auto is=[&](const char*canonical){std::string compare=canonical;
#ifdef _WIN32
 std::transform(compare.begin(),compare.end(),compare.begin(),[](unsigned char c){return char(std::tolower(c));});
#endif
 return category==compare;};
 if(is("Datasets"))return fs::path("Datasets")/tail;
 if(is("DatasetBackups"))return fs::path("Recovery")/"Datasets"/tail;
 for(auto n:{"Exports","Import","SequenceImport"})if(is(n))return fs::path("Transfers")/n/tail;
 for(auto n:{"ResetBackups","ImportBackups"})if(is(n))return fs::path("Recovery")/n/tail;
 return fs::path("Internal")/relative;
}

inline void ValidateCopy(const fs::path&requestedSource,const fs::path&requestedStage){const auto source=Native(requestedSource),stage=Native(requestedStage);auto record=stage/"Internal"/"layout-source-count.txt";Safe(record);std::ifstream countFile(record);uint64_t expected=0;archive::Require(bool(countFile>>expected),"Missing layout source inventory");countFile>>std::ws;archive::Require(countFile.eof(),"Invalid layout source inventory");uint64_t count=0;
 for(auto&e:fs::recursive_directory_iterator(source)){Safe(e.path());auto mapped=stage/Destination(e.path().lexically_relative(source));Safe(mapped);++count;if(e.is_directory()){archive::Require(fs::is_directory(mapped),"Layout source directories changed");continue;}archive::Require(e.is_regular_file()&&fs::is_regular_file(mapped)&&fs::file_size(e.path())==fs::file_size(mapped),"Layout source files changed; newer data retained");std::ifstream a(e.path(),std::ios::binary),b(mapped,std::ios::binary);std::array<char,65536>x{},y{};while(a){a.read(x.data(),x.size());auto n=a.gcount();b.read(y.data(),n);archive::Require(b.gcount()==n&&std::equal(x.begin(),x.begin()+n,y.begin()),"Layout source bytes changed; newer data retained");}archive::Require(a.eof()&&!a.bad()&&!b.bad(),"Cannot revalidate layout copy");}
 archive::Require(count==expected,"Layout source inventory changed; newer data retained");
}

inline void Upgrade(const fs::path&requestedBase){const auto base=Native(requestedBase);RecoverUpgrade(base);if(Managed(base/"Internal"))return;Safe(base);archive::Require(fs::is_directory(base),"Flat working folder missing");archive::Require(!fs::exists(base/"dataset-load-journal.txt"),"Resolve old database load before layout migration");
 const auto token=Token();auto stage=base;stage+=".layout-stage-"+token;archive::Require(fs::create_directory(stage),"Layout stage already exists");for(auto n:{"Internal","Datasets","Recovery","Transfers"})fs::create_directory(stage/n);
 struct Entry{fs::path relative;uintmax_t bytes;fs::file_time_type modified;bool directory;};std::vector<Entry> entries;
 for(auto&e:fs::recursive_directory_iterator(base)){Safe(e.path());const bool dir=e.is_directory();archive::Require(dir||e.is_regular_file(),"Unsupported source in managed layout");entries.push_back({e.path().lexically_relative(base),dir?0:e.file_size(),e.last_write_time(),dir});}
 for(auto&e:entries){auto to=stage/Destination(e.relative);if(e.directory){Safe(to);fs::create_directories(to);}else ExactCopy(base/e.relative,to);}
 size_t count=0;for(auto&e:fs::recursive_directory_iterator(base)){Safe(e.path());++count;}archive::Require(count==entries.size(),"Working tree changed during layout migration");for(auto&e:entries){auto p=base/e.relative;archive::Require(fs::exists(p)&&fs::is_directory(p)==e.directory&&(e.directory||(fs::file_size(p)==e.bytes&&fs::last_write_time(p)==e.modified)),"Working files changed during layout migration");}
 archive::Require(!fs::exists(stage/"Internal"/"managed-layout-v1.txt"),"Flat root collides with managed marker; originals retained");archive::Require(!fs::exists(stage/"Internal"/"layout-source-count.txt"),"Flat root collides with layout inventory; originals retained");Text(stage/"Internal"/"layout-source-count.txt",std::to_string(entries.size())+"\n");Text(stage/"Internal"/"managed-layout-v1.txt","NIKO_LAYOUT 1\n"+token+"\n");Text(stage/"README.txt","Datasets: named saves. Internal: app-managed active learning files; do not edit while running. Recovery: preserved history and pre-migration copies. Transfers: imports and exports. Data is compressed, not encrypted.\n");
 Text(Journal(base),"NIKO_LAYOUT_TRANSACTION 1\nPREPARED "+token+"\n");RecoverUpgrade(base);
}
}
