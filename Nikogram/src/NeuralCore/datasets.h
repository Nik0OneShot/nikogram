#pragma once
#include "storage.h"
#include "neural_input.h"
#include "layout.h"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <cctype>
#include <iomanip>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace movement::datasets {
namespace fs=std::filesystem;
enum class Action{Save,Load,Delete};
struct State {bool busy=true,ready=false;std::vector<std::string>names;std::string active,storage,message="Restoring selected database",error;};
inline bool ValidName(const std::string&name){
 if(name.empty()||name.size()>48||name.front()==' '||name.back()==' '||name.back()=='.')return false;
 for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' '||c=='_'||c=='-'))return false;
 std::string upper=name;for(auto&c:upper)c=char(std::toupper(static_cast<unsigned char>(c)));
 if(upper=="CON"||upper=="PRN"||upper=="AUX"||upper=="NUL")return false;
 if(upper.size()==4&&(upper.starts_with("COM")||upper.starts_with("LPT"))&&upper.back()>='0'&&upper.back()<='9')return false;
 return true;
}
// Immutable generations and atomic manifests protect named snapshots. The active
// root remains an append-only working copy; restarting never reloads an older save.
class Store {
public:
 static constexpr int Files=4; // legacy, sequence v2, v3 (frozen), v4
 using Snapshots=std::array<std::shared_ptr<storage::Snapshot>,Files>;
 using Capture=std::function<Snapshots()>;
private:
 fs::path base,root,saved,backups,migrateFrom;std::function<void()>pause,resume;std::function<void(const fs::path&,bool,const storage::Cancel&)>validate;Capture capture;
 struct Command{Action action;std::string name;bool overwrite;};
 struct Identity{std::string name,token;std::array<bool,Files>present{};};
 struct Manifest{fs::path folder;std::array<bool,Files>present{};std::string generation;bool packed=false;};
 mutable std::mutex mutex;std::condition_variable wake;std::optional<Command>pending;State state;bool recoveryRequired=false;std::atomic<bool>stop=false,ready=false,migrationBlocked=false;
 Identity active;std::array<uint64_t,Files>lastSaved{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};std::array<fs::file_time_type,Files>lastTimes{};std::chrono::milliseconds interval;std::thread worker;
 static constexpr std::array<const char*,Files>files{"observations.txt","sequence-observations-v2.txt","sequence-observations-v3.txt","sequence-observations-v4.txt"};
 std::string Stamp()const{return std::to_string(std::chrono::system_clock::now().time_since_epoch().count());}
 void SafePath(const fs::path&p,const fs::path&boundary)const{
  layout::Safe(p);
  auto relative=fs::weakly_canonical(p).lexically_relative(fs::weakly_canonical(boundary));
  if(relative.empty()||relative.is_absolute()||*relative.begin()==".."||fs::is_symlink(p))throw std::runtime_error("Dataset path escapes its storage folder: "+p.string());
 }
 static void FlushFile(const fs::path&p){
#ifdef _WIN32
  auto h=CreateFileW(p.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot open durable dataset file");
  const bool ok=FlushFileBuffers(h)!=0;CloseHandle(h);if(!ok)throw std::runtime_error("Cannot flush durable dataset file");
#else
  (void)p;
#endif
 }
 static void AtomicReplace(const fs::path&from,const fs::path&to){
#ifdef _WIN32
  if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot commit dataset manifest");
#else
  fs::rename(from,to);
#endif
 }
 void AtomicText(const fs::path&path,const std::string&text,bool preserve=true){
  SafePath(path,base);auto temp=path;temp+=".tmp-"+Stamp();
  {std::ofstream out(temp,std::ios::binary);if(!out||!(out<<text)||!out.flush())storage::Fail(temp,"Cannot write dataset metadata");}FlushFile(temp);
  if(preserve&&fs::exists(path)){auto prior=layout::Previous(path);fs::create_directories(prior.parent_path());if(fs::exists(prior))fs::rename(prior,layout::Unique(fs::path(prior.string()+"-"+Stamp())));auto staged=prior;staged+=".tmp-"+Stamp();fs::copy_file(path,staged);FlushFile(staged);AtomicReplace(staged,prior);}
  AtomicReplace(temp,path);
 }
 // Older formats are written while no v4 corpus exists, so 0.7.0.24 can still read them.
 std::string IdentityText(const Identity&id)const{std::ostringstream out;const bool four=id.present[3];out<<(four?"MOVEMENT_ACTIVE 3\n":"MOVEMENT_ACTIVE 2\n")<<std::quoted(id.name)<<' '<<std::quoted(id.token);for(int i=0;i<(four?Files:3);++i)out<<' '<<id.present[i];out<<'\n';return out.str();}
 Identity ReadIdentity(){
  auto path=root/"active-dataset.txt";SafePath(path,root);std::ifstream in(path);std::string magic;int version=0,a=-1,b=-1;Identity id;
  if(!(in>>magic>>version>>std::quoted(id.name)>>std::quoted(id.token)>>a>>b)||magic!="MOVEMENT_ACTIVE"||version<1||version>3||!ValidName(id.name)||id.token.empty()||a<0||a>1||b<0||b>1)throw std::runtime_error("Selected database record is invalid; automatic saving is blocked");
  int c=0,d=0;if(version>=2&&(!(in>>c)||c<0||c>1))throw std::runtime_error("Invalid selected cue corpus flag");if(version>=3&&(!(in>>d)||d<0||d>1))throw std::runtime_error("Invalid selected frame corpus flag");in>>std::ws;if(!in.eof())throw std::runtime_error("Unexpected selected database metadata");id.present={a!=0,b!=0,c!=0,d!=0};return id;
 }
 Manifest ReadManifest(const std::string&name){
  return ReadManifestAt(saved/name);
 }
 Manifest ReadManifestAt(const fs::path&folder){SafePath(folder,folder.parent_path());SafePath(folder/"dataset.txt",folder);std::ifstream in(folder/"dataset.txt");std::string magic;int version=0;Manifest m;m.folder=folder;
  if(!(in>>magic>>version)||magic!="MOVEMENT_DATASET"||version<1||version>5)throw std::runtime_error("Unsupported dataset manifest: "+folder.string());
  m.packed=version>=3;if(version>=2){if(!(in>>m.generation)||!m.generation.starts_with("generation-")||!std::all_of(m.generation.begin()+11,m.generation.end(),[](char c){return c>='0'&&c<='9';})||m.generation.size()<=11)throw std::runtime_error("Invalid dataset generation");m.folder=folder/m.generation;SafePath(m.folder,folder);}
  for(int i=0;i<(version>=5?4:version>=4?3:2);++i){int value=-1;if(!(in>>value)||value<0||value>1)throw std::runtime_error("Invalid dataset manifest");m.present[i]=value!=0;if(m.present[i]){SafePath(ManifestFile(m,i),m.folder);if(!fs::is_regular_file(ManifestFile(m,i)))throw std::runtime_error("Dataset file missing: "+std::string(files[i]));if(m.packed&&!archive::IsPacked(ManifestFile(m,i)))throw std::runtime_error("Version3 dataset requires a valid compressed archive");}}
  in>>std::ws;if(!in.eof())throw std::runtime_error("Unexpected dataset manifest content");return m;
 }
 fs::path ManifestFile(const Manifest&m,int i)const{return m.folder/(std::string(files[i])+(m.packed?".ncz":""));}
 std::array<bool,Files> ValidateRoot(){std::array<bool,Files>present{};for(int i=0;i<Files;++i){SafePath(root/files[i],root);present[i]=fs::exists(root/files[i]);if(present[i])validate(root/files[i],i!=0,{});}return present;}
 Snapshots CaptureRoot(){if(capture)return capture();Snapshots result;for(int i=0;i<Files;++i)result[i]=std::make_shared<storage::Snapshot>(root/files[i]);return result;}
 void Refresh(){std::vector<std::string>names;for(const auto&e:fs::directory_iterator(saved))if(e.is_directory()&&ValidName(e.path().filename().string())&&fs::exists(e.path()/"dataset.txt"))names.push_back(e.path().filename().string());std::sort(names.begin(),names.end());std::lock_guard lock(mutex);state.names=std::move(names);}
 void Ready(const Identity&id){active=id;ready=true;std::lock_guard lock(mutex);state.ready=true;state.active=id.name;state.error.clear();state.message="Active database: "+id.name+". Changes save automatically.";}
 void Fail(const std::exception&e){std::lock_guard lock(mutex);state.error=e.what();state.message=ready?"Automatic save failed; working observations are retained":"Database unavailable; learning is paused and existing files are preserved";}
 bool Prefix(const fs::path&prefixPath,const fs::path&workingPath){
  if(!fs::exists(workingPath))return false;neuralio::Input a(prefixPath),b(workingPath);std::array<char,65536>x{},y{};
  while(a){a.read(x.data(),x.size());auto n=a.gcount();if(!n)break;if(!b.read(y.data(),n)||!std::equal(x.begin(),x.begin()+n,y.begin()))return false;}return a.eof();
 }
 void Save(const std::string&name,bool overwrite,bool automatic){
  if(!ready&&!automatic)throw std::runtime_error("Load a valid database before saving; failed loads cannot overwrite saved data");
  auto destination=saved/name;SafePath(destination,saved);if(fs::exists(destination)&&!overwrite)throw std::runtime_error("Name already exists; confirm overwrite or choose another name");
  auto snapshots=CaptureRoot();std::array<bool,Files>present{};std::array<uint64_t,Files>sizes{};std::array<fs::file_time_type,Files>times{};
  for(int i=0;i<Files;++i){present[i]=snapshots[i]&&bool(snapshots[i]->input);sizes[i]=present[i]?snapshots[i]->bytes:0;times[i]=present[i]?snapshots[i]->modified:fs::file_time_type{};if(active.present[i]&&!present[i])throw std::runtime_error("Active observation file disappeared; refusing empty overwrite");}
  if(automatic&&name==active.name&&sizes==lastSaved&&times==lastTimes)return;
  fs::create_directories(destination);const auto generation="generation-"+Stamp();auto stage=destination/generation;if(!fs::create_directory(stage))throw std::runtime_error("Dataset generation already exists");SafePath(stage,destination);
  storage::Scratch scratch;for(int i=0;i<Files;++i)if(present[i]){auto raw=scratch.Next();snapshots[i]->Copy(raw,{});auto target=stage/(std::string(files[i])+".ncz");auto logical=neuralio::LogicalSize(raw);neuralio::Input input(raw);archive::Pack(input,logical,target);validate(target,i!=0,{});FlushFile(target);}
  snapshots={};std::string previous;
  if(fs::exists(destination/"dataset.txt")){auto m=ReadManifest(name);previous=m.generation;}
  std::ostringstream manifest;manifest<<(present[3]?"MOVEMENT_DATASET 5\n":"MOVEMENT_DATASET 4\n")<<generation<<'\n'<<present[0]<<' '<<present[1]<<' '<<present[2];if(present[3])manifest<<' '<<present[3];manifest<<'\n';
  AtomicText(destination/"dataset.txt",manifest.str());
  Identity selected{name,Stamp(),present};AtomicText(root/"active-dataset.txt",IdentityText(selected));Ready(selected);lastSaved=sizes;lastTimes=times;
  // Retain every prior generation in Recovery; no observation history is deleted.
  for(const auto&e:fs::directory_iterator(destination))if(e.is_directory()){
   const auto n=e.path().filename().string();if(n==generation||n==previous||!n.starts_with("generation-")||n.size()<=11||!std::all_of(n.begin()+11,n.end(),[](char c){return c>='0'&&c<='9';}))continue;
   SafePath(e.path(),destination);auto history=layout::Recovery(root,"Generations")/name;fs::create_directories(history);fs::rename(e.path(),layout::Unique(history/n));
  }
 }
 // Load journal is durable before either active file moves. An uncommitted
 // transaction always rolls back, including a process interruption between files.
 void Recover(){
  auto path=root/"dataset-load-journal.txt";if(!fs::exists(path))return;SafePath(path,root);std::ifstream in(path);std::string magic,token;int version=0,a=-1,b=-1;
  if(!(in>>magic>>version>>token>>a>>b)||magic!="MOVEMENT_LOAD"||version<1||version>3||token.empty()||!std::all_of(token.begin(),token.end(),[](char c){return c>='0'&&c<='9';})||a<0||a>1||b<0||b>1)throw std::runtime_error("Load recovery journal is invalid; recover original observations from "+backups.string());
  int c=0,d=0;if(version>=2&&(!(in>>c)||c<0||c>1))throw std::runtime_error("Invalid cue corpus recovery flag");if(version>=3&&(!(in>>d)||d<0||d>1))throw std::runtime_error("Invalid frame corpus recovery flag");in>>std::ws;if(!in.eof())throw std::runtime_error("Unexpected load journal content");in.close();
  auto backup=backups/("before-load-"+token);SafePath(backup,backups);
  bool committed=false;if(fs::exists(root/"active-dataset.txt"))try{committed=ReadIdentity().token==token;}catch(...){}
  if(!committed){const std::array<bool,Files>prior{a!=0,b!=0,c!=0,d!=0};for(int i=0;i<(version==1?2:version==2?3:Files);++i){auto source=backup/files[i];auto target=root/files[i];SafePath(target,root);if(prior[i]){if(fs::exists(source)){if(fs::exists(target)){auto displaced=layout::Unique(backup/(std::string(files[i])+".uncommitted"));fs::rename(target,displaced);}fs::rename(source,target);}else if(!fs::exists(target))throw std::runtime_error("Load recovery is missing original observations");}else if(fs::exists(target)){fs::rename(target,backup/(std::string(files[i])+".uncommitted-"+Stamp()));}}}
  fs::remove(path);
 }
 void Load(const std::string&name){
  if(recoveryRequired)throw std::runtime_error("Recover the interrupted load before continuing");auto m=ReadManifest(name);
  // Validate and stage before pausing any working service.
  const auto token=Stamp();auto transactions=root/"Transactions";fs::create_directories(transactions);auto stage=transactions/("load-"+token);if(!fs::create_directory(stage))throw std::runtime_error("Load stage already exists");SafePath(stage,root);
  for(int i=0;i<Files;++i)if(m.present[i]){auto source=ManifestFile(m,i);auto logical=neuralio::LogicalSize(source);neuralio::Input input(source);appendlog::CreateFrom(input,logical,stage/files[i]);validate(stage/files[i],i!=0,{});FlushFile(stage/files[i]);}
  try{pause();}catch(...){recoveryRequired=true;ready=false;std::lock_guard lock(mutex);state.ready=false;throw;}ready=false;{std::lock_guard lock(mutex);state.ready=false;}
  bool paused=true;try{
   auto backup=backups/("before-load-"+token);fs::create_directory(backup);std::array<bool,Files>prior{};for(int i=0;i<Files;++i)prior[i]=fs::exists(root/files[i]);
   std::ostringstream journal;journal<<(prior[3]?"MOVEMENT_LOAD 3\n":"MOVEMENT_LOAD 2\n")<<token<<' '<<prior[0]<<' '<<prior[1]<<' '<<prior[2];if(prior[3])journal<<' '<<prior[3];journal<<'\n';AtomicText(root/"dataset-load-journal.txt",journal.str(),false);
   for(int i=0;i<Files;++i)if(prior[i]){SafePath(root/files[i],root);fs::rename(root/files[i],backup/files[i]);}
   for(int i=0;i<Files;++i)if(m.present[i])fs::rename(stage/files[i],root/files[i]);
   Identity id{name,token,m.present};AtomicText(root/"active-dataset.txt",IdentityText(id));Ready(id);fs::remove(root/"dataset-load-journal.txt");lastSaved={UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX};if(fs::is_empty(stage))fs::remove(stage);resume();paused=false;
  }catch(...){try{Recover();if(!active.name.empty()){ValidateRoot();Ready(active);resume();paused=false;}}catch(...){recoveryRequired=true;}if(paused)recoveryRequired=true;if(recoveryRequired){ready=false;std::lock_guard lock(mutex);state.ready=false;}throw;}
 }
 uint64_t LogicalSize(const fs::path&p){neuralio::Input in(p);std::array<char,65536>buf{};uint64_t n=0;while(in){in.read(buf.data(),buf.size());n+=in.gcount();}if(!in.eof())throw std::runtime_error("Cannot inspect dataset size");return n;}
 void WorkingCompression(){std::lock_guard lock(mutex);state.storage="Active observations: native Zstandard blocks, flushed within100ms while the IO worker is available, on snapshots and shutdown. Named saves/models: Zstandard. Unencrypted; tiny journals remain text. Originals stay in Learning.";auto repairs=layout::Recovery(root,"AppendRepair");if(fs::is_directory(repairs)&&!fs::is_empty(repairs))state.storage+=" Append recovery files are retained in Recovery/AppendRepair.";else if(fs::exists(root/"append-recovery.txt"))state.storage+=" An earlier recovery notice remains in Internal/append-recovery.txt.";}
 void Migrate(){
  if(migrateFrom.empty())return;
  const auto parent=root.parent_path();fs::create_directories(parent);SafePath(root,parent);SafePath(migrateFrom,parent);
  if(fs::weakly_canonical(root)==fs::weakly_canonical(migrateFrom))throw std::runtime_error("Compressed working root must differ from original Learning");
  auto marker=parent/(root.filename().string()+".migration.txt");SafePath(marker,parent);
  auto markerText=[](const std::string&phase,const std::string&token){return "NIKO_MIGRATION 1\n"+phase+" "+token+"\n";};
  auto write=[&](const fs::path&p,const std::string&text){SafePath(p,parent);auto temp=p;temp+=".tmp-"+Stamp();{std::ofstream out(temp,std::ios::binary);if(!out||!(out<<text)||!out.flush())throw std::runtime_error("Cannot write migration metadata");}FlushFile(temp);AtomicReplace(temp,p);};
  auto verifyRoot=[&](const std::string&token){if(!fs::is_directory(root))throw std::runtime_error("Committed LearningCompressed is missing; refusing fallback to older Learning");auto identity=(layout::Managed(root/"Internal")?root/"Internal":root)/"compressed-working-v1.txt";SafePath(identity,root);std::ifstream in(identity);std::string magic,t;int version=0;if(!(in>>magic>>version>>t)||magic!="NIKO_WORKING"||version!=1||t!=token)throw std::runtime_error("Compressed working identity is invalid; original Learning remains untouched");in>>std::ws;if(!in.eof())throw std::runtime_error("Unexpected compressed working metadata");};
  if(fs::exists(marker)){
   std::ifstream in(marker);std::string magic,phase,token;int version=0;if(!(in>>magic>>version>>phase>>token)||magic!="NIKO_MIGRATION"||version!=1||(phase!="PREPARED"&&phase!="COMMITTED")||token.empty()||!std::all_of(token.begin(),token.end(),[](char c){return c>='0'&&c<='9';}))throw std::runtime_error("Invalid migration journal; automatic fallback blocked");in>>std::ws;if(!in.eof())throw std::runtime_error("Unexpected migration journal content");in.close();
   auto stage=parent/(root.filename().string()+".stage-"+token);SafePath(stage,parent);
   if(phase=="PREPARED"){if(fs::exists(root)&&fs::exists(stage))throw std::runtime_error("Ambiguous staged migration; preserve both folders for recovery");if(!fs::exists(root)){if(!fs::is_directory(stage))throw std::runtime_error("Prepared migration files missing; originals retained");AtomicReplace(stage,root);}verifyRoot(token);write(marker,markerText("COMMITTED",token));}else verifyRoot(token);
   return;
  }
  if(fs::exists(root))throw std::runtime_error("LearningCompressed exists without its migration marker; refusing to overwrite or create an empty database");
  if(fs::exists(migrateFrom/"dataset-load-journal.txt"))throw std::runtime_error("Original Learning has an unfinished load; resolve it before compression migration");
  const auto token=Stamp();auto stage=parent/(root.filename().string()+".stage-"+token);SafePath(stage,parent);if(!fs::create_directory(stage))throw std::runtime_error("Migration stage already exists");
  struct SourceStamp{fs::path path;uint64_t size;fs::file_time_type time;};std::vector<SourceStamp>sourceStamps;
  auto remember=[&](const fs::path&p){if(sourceStamps.size()>=4096)throw std::runtime_error("Migration exceeds4096 source files; originals retained");sourceStamps.push_back({p,fs::file_size(p),fs::last_write_time(p)});};
  auto copy=[&](const fs::path&from,const fs::path&to,bool corpus){
   SafePath(from,migrateFrom);SafePath(to,stage);if(!fs::is_regular_file(from))throw std::runtime_error("Migration source is not a regular file");auto size=fs::file_size(from);auto time=fs::last_write_time(from);if(size>archive::DefaultLimit)throw std::runtime_error("Migration file exceeds supported bound; originals retained");
   remember(from);fs::create_directories(to.parent_path());
   const auto name=from.filename().string();const bool outcomes=name=="chosen-outcomes-v2.txt"||name=="chosen-outcomes-v3.txt";
   const bool tiny=name=="README.txt"||name=="active-dataset.txt"||name=="active-dataset.txt.previous"||name=="import-provenance.txt"||name=="import-provenance.txt.previous";
   if(corpus||outcomes){neuralio::Input input(from);appendlog::CreateFrom(input,neuralio::LogicalSize(from),to);}
   else if(!tiny){neuralio::Input input(from);archive::Pack(input,neuralio::LogicalSize(from,archive::DefaultLimit),to);}
   else{storage::Snapshot snapshot(from);snapshot.Copy(to,{});FlushFile(to);}
   neuralio::Same(from,to);if(fs::file_size(from)!=size||fs::last_write_time(from)!=time)throw std::runtime_error("Original Learning changed during migration; retry after stopping the old client");

  };
  if(fs::exists(migrateFrom)){
   if(!fs::is_directory(migrateFrom))throw std::runtime_error("Original Learning is not a directory");
   std::ostringstream inventory;inventory<<"Original Learning retained unchanged. Copied active observations, model/policy metadata and SequenceImport; original backups and unknown files remain at the original path; transfer folders are also copied.\n";
   auto required=[](std::string name){if(name.ends_with(".previous"))name.resize(name.size()-9);for(auto known:{"models.txt","experimental-candidates.txt","last-evaluation.txt","sequence-players-v2.txt","chosen-outcomes-v2.txt","chosen-outcomes-v3.txt","imported-candidates.txt","import-provenance.txt","active-dataset.txt"})if(name==known)return true;for(auto prefix:{"sequence-model-","sequence-evaluation-","sequence-v3-model-","sequence-v3-evaluation-"})if(name.starts_with(prefix)&&name.ends_with(".txt")){auto id=name.substr(std::char_traits<char>::length(prefix),name.size()-std::char_traits<char>::length(prefix)-4);if(!id.empty()&&std::all_of(id.begin(),id.end(),[](char c){return c>='0'&&c<='9';}))return true;}return false;};
   for(const auto&e:fs::directory_iterator(migrateFrom))if(e.is_regular_file()){auto name=e.path().filename().string();bool corpus=std::find(files.begin(),files.end(),name)!=files.end();if(corpus||required(name)){copy(e.path(),stage/name,corpus);if(corpus)validate(stage/name,name!=files[0],{});inventory<<"copied ";}else inventory<<"retained in original folder only ";inventory<<std::quoted(name)<<'\n';}write(stage/"migration-inventory.txt",inventory.str());
   for(auto transfer:{"SequenceImport","Import","Exports"}){auto imports=migrateFrom/transfer;if(fs::exists(imports)){SafePath(imports,migrateFrom);for(const auto&e:fs::recursive_directory_iterator(imports)){SafePath(e.path(),migrateFrom);if(e.is_regular_file())copy(e.path(),stage/transfer/e.path().lexically_relative(imports),false);}}}
   auto originalSaved=migrateFrom/"Datasets";if(fs::exists(originalSaved)){SafePath(originalSaved,migrateFrom);for(const auto&e:fs::directory_iterator(originalSaved)){
    auto name=e.path().filename().string();if(!e.is_directory()||!ValidName(name)||!fs::exists(e.path()/"dataset.txt"))continue;SafePath(e.path(),migrateFrom);remember(e.path()/"dataset.txt");auto m=ReadManifestAt(e.path());auto dest=stage/"Datasets"/name;auto generation="generation-"+token;auto target=dest/generation;fs::create_directories(target);
    for(int i=0;i<Files;++i)if(m.present[i]){auto source=ManifestFile(m,i);remember(source);validate(source,i!=0,{});auto file=target/(std::string(files[i])+".ncz");if(m.packed){fs::copy_file(source,file);FlushFile(file);}else{storage::Snapshot snapshot(source);archive::Pack(*snapshot.input,snapshot.bytes,file);}validate(file,i!=0,{});}
    std::ostringstream text;text<<(m.present[3]?"MOVEMENT_DATASET 5\n":"MOVEMENT_DATASET 4\n")<<generation<<'\n'<<m.present[0]<<' '<<m.present[1]<<' '<<m.present[2];if(m.present[3])text<<' '<<m.present[3];text<<'\n';write(dest/"dataset.txt",text.str());
   }}
  }
  for(const auto&source:sourceStamps)if(!fs::is_regular_file(source.path)||fs::file_size(source.path)!=source.size||fs::last_write_time(source.path)!=source.time)throw std::runtime_error("Original Learning changed during migration; stop the old client before retrying");
  write(stage/"compressed-working-v1.txt","NIKO_WORKING 1\n"+token+"\n");
  write(marker,markerText("PREPARED",token));AtomicReplace(stage,root);verifyRoot(token);write(marker,markerText("COMMITTED",token));
 }
 void Initialize(){
  SafePath(saved,base);SafePath(backups,base);fs::create_directories(saved);fs::create_directories(backups);Recover();for(auto file:files){SafePath(root/file,root);if(std::string(file)!="sequence-observations-v2.txt")neuralio::EnsureAppend(root/file);}for(auto file:{"chosen-outcomes-v2.txt","chosen-outcomes-v3.txt"}){SafePath(root/file,root);neuralio::EnsureAppend(root/file);}Refresh();
  if(fs::exists(root/"active-dataset.txt")){
   auto id=ReadIdentity();auto present=ValidateRoot();
   for(int i=0;i<Files;++i)if(id.present[i]&&!present[i])throw std::runtime_error("Selected working database is incomplete; load its saved snapshot to recover");
   ReadManifest(id.name);id.present=present;Ready(id);resume();return;
  }
  auto present=ValidateRoot();std::vector<std::string>names;{std::lock_guard lock(mutex);names=state.names;}
  struct Match{std::string name;std::array<uint64_t,Files>bytes;};std::vector<Match>matches;
  for(const auto&name:names){auto m=ReadManifest(name);bool fits=true;std::array<uint64_t,Files>bytes{};for(int i=0;i<Files;++i){if(m.present[i]){fits&=present[i]&&Prefix(ManifestFile(m,i),root/files[i]);bytes[i]=LogicalSize(ManifestFile(m,i));}else fits&=!present[i];}if(fits){for(int i=0;i<Files;++i)if(m.present[i])validate(ManifestFile(m,i),i!=0,{});matches.push_back({name,bytes});}}
  std::string match;
  for(const auto&candidate:matches){bool uniqueLargest=true;for(const auto&other:matches)if(other.name!=candidate.name){if(candidate.bytes[0]<other.bytes[0]||candidate.bytes[1]<other.bytes[1]||candidate.bytes[2]<other.bytes[2]||candidate.bytes[3]<other.bytes[3]||candidate.bytes==other.bytes){uniqueLargest=false;break;}}if(uniqueLargest){if(!match.empty())throw std::runtime_error("Ambiguous saved database identity; choose Load explicitly");match=candidate.name;}}
  if(!match.empty()){Identity id{match,Stamp(),present};AtomicText(root/"active-dataset.txt",IdentityText(id));Ready(id);resume();return;}
  if(!names.empty()){
   if(!present[0]&&!present[1]&&!present[2]&&!present[3]&&names.size()==1){Load(names.front());return;}
   throw std::runtime_error("No saved database matches the existing working observations; select Load to choose without overwriting either copy");
  }
  active={"default",Stamp(),present};Save("default",false,true);resume();
 }
 void Execute(const Command&command){
  if(!ValidName(command.name))throw std::runtime_error("Use 1-48 letters, numbers, spaces, _ or -; device names are reserved");
  if(command.action==Action::Load){Load(command.name);return;}
  if(recoveryRequired)throw std::runtime_error("Dataset recovery is required before saving or deleting");
  if(command.action==Action::Save){Save(command.name,command.overwrite,false);return;}
  if(!active.name.empty()&&fs::exists(saved/command.name)&&fs::exists(saved/active.name)&&fs::equivalent(saved/command.name,saved/active.name))throw std::runtime_error("Load another database before deleting the active database");auto path=saved/command.name;SafePath(path,saved);fs::rename(path,backups/("deleted-"+command.name+"-"+Stamp()));
 }
 void Run()noexcept{
  try{migrationBlocked=true;layout::RecoverUpgrade(base);Migrate();if(!layout::Managed(base/"Internal")){fs::create_directories(base);fs::create_directories(backups);Recover();layout::Upgrade(base);}root=base/"Internal";saved=base/"Datasets";backups=base/"Recovery"/"Datasets";migrationBlocked=false;Initialize();WorkingCompression();}catch(const std::exception&e){ready=false;{std::lock_guard lock(mutex);state.ready=false;}Fail(e);} {std::lock_guard lock(mutex);state.busy=false;}
  auto next=std::chrono::steady_clock::now()+interval;
  while(!stop){Command command;bool requested=false;{std::unique_lock lock(mutex);wake.wait_until(lock,next,[&]{return stop||pending.has_value();});if(stop)break;if(pending){command=std::move(*pending);pending.reset();requested=true;}state.busy=true;}
   try{if(requested)Execute(command);else if(ready&&!recoveryRequired)Save(active.name,true,true);Refresh();}catch(const std::exception&e){Fail(e);}
   {std::lock_guard lock(mutex);state.busy=false;}next=std::chrono::steady_clock::now()+interval;
  }
  if(ready&&!recoveryRequired)try{pause();Save(active.name,true,true);}catch(const std::exception&e){Fail(e);}
 }
public:
 Store(fs::path folder,std::function<void()>pauseWork,std::function<void()>resumeWork,std::function<void(const fs::path&,bool,const storage::Cancel&)>validator,Capture snapshot={},std::chrono::milliseconds autosave=std::chrono::minutes(5),fs::path original={}):base(std::move(folder)),root(base),saved(root/"Datasets"),backups(root/"DatasetBackups"),migrateFrom(std::move(original)),pause(std::move(pauseWork)),resume(std::move(resumeWork)),validate(std::move(validator)),capture(std::move(snapshot)),interval(autosave),worker([this]{Run();}){}
 ~Store(){Stop();}
 bool Request(Action action,std::string name,bool overwrite=false){std::lock_guard lock(mutex);if(stop||state.busy||migrationBlocked)return false;if(!ValidName(name)){state.error="Use 1-48 letters, numbers, spaces, _ or -; device names are reserved";return false;}pending=Command{action,std::move(name),overwrite};state.busy=true;state.error.clear();state.message=action==Action::Load?"Loading selected database":"Saving database in the background";wake.notify_one();return true;}
 bool Ready()const noexcept{return ready.load();}
 State GetState()const{std::lock_guard lock(mutex);return state;}
 void Stop()noexcept{stop=true;wake.notify_all();if(worker.joinable())worker.join();}
};
}
