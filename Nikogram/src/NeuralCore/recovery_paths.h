#pragma once
#include "layout.h"
namespace movement::recovery {
// Preserve a prior rollback before advancing the latest rollback pointer.
// Standalone callers without a managed layout retain their existing paths.
inline void RetainPrevious(const std::filesystem::path&primary,const std::filesystem::path&previous){
 if(layout::Managed(primary.parent_path())&&std::filesystem::exists(previous)){
  auto folder=layout::Recovery(primary.parent_path(),"PreviousHistory");std::filesystem::create_directories(folder);
  auto saved=layout::Unique(folder/(previous.filename().string()+"-"+layout::Token()));
  layout::ExactCopy(previous,saved);
 }
}
inline std::filesystem::path Backup(const std::filesystem::path&working,const char*category,const std::filesystem::path&filename){
 auto folder=layout::Managed(working)?layout::Recovery(working,category):working;
 std::filesystem::create_directories(folder);return layout::Unique(folder/filename);
}
}
