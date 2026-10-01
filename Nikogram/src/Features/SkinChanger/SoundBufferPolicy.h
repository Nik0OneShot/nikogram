#pragma once
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <new>

namespace SkinSound
{
    // x64 CUtlVector ABI, without a CRT-owning destructor. Native sound code
    // may grow this buffer; both that code and our cleanup must use MemAlloc.
    template<class T> struct Origins
    {
        T* memory=nullptr;
        int capacity=0;
        int growSize=0;
        int count=0;
        T* elements=nullptr;
        Origins()=default;
        Origins(const Origins&)=delete;
        Origins& operator=(const Origins&)=delete;
        int Count()const{return count;}
        bool Valid()const
        {return count>=0&&count<=128&&capacity>=count&&capacity<=4096&&(!capacity||memory);}
        template<class Allocator> bool CopyFrom(const Origins& source,Allocator& allocator)
        {
            static_assert(std::is_trivially_destructible_v<T>);
            if(!source.Valid()||!Valid())return false;
            if(this==&source)return true;
            if(source.count>capacity)
            {
                if(growSize<0)return false; // externally owned destination
                auto next=static_cast<T*>(allocator.Alloc(sizeof(T)*source.count));
                if(!next)return false;
                for(int i=0;i<source.count;++i)new(next+i) T(source.memory[i]);
                if(memory)allocator.Free(memory);
                memory=next;capacity=source.count;
            }
            else for(int i=0;i<source.count;++i)new(memory+i) T(source.memory[i]);
            count=source.count;elements=memory;return true;
        }
        template<class Allocator> void Release(Allocator& allocator)
        {
            if(memory&&growSize>=0)allocator.Free(memory);
            memory=elements=nullptr;capacity=count=growSize=0;
        }
    };
    static_assert(sizeof(void*)!=8||sizeof(Origins<float>)==32);
    static_assert(sizeof(void*)!=8||offsetof(Origins<float>,count)==16);
    static_assert(sizeof(void*)!=8||offsetof(Origins<float>,elements)==24);
    static_assert(std::is_standard_layout_v<Origins<float>>);
    static_assert(std::is_trivially_destructible_v<Origins<float>>);
}
