#include "Dapper.h"
#include "../../../SDK/SDK.h"
#include "../../../SDK/Definitions/Misc/VTF.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;
namespace Dapper
{
    namespace
    {
        constexpr int Size=512;
        constexpr const char* TextureNames[]{"nikogram/dapper_portrait_v054", "nikogram/dapper_money_v054", "nikogram/dapper_giga_v054"};
        struct Photo { const unsigned char* rgba=nullptr; ITexture* texture=nullptr; int surface=0; };
        Photo photos[3];
        class Regenerator final : public ITextureRegenerator
        {
            const unsigned char* pixels;
        public:
            explicit Regenerator(const unsigned char* data):pixels(data){}
            void RegenerateTextureBits(ITexture*,IVTFTexture* vtf,Rect_t*) override
            {
                if(!vtf)return;
                const auto format=vtf->Format();
                // Source may choose a native BGRA/BGRX destination even when
                // RGBA was requested. Always write the actual destination order.
                const bool blueFirst=format==IMAGE_FORMAT_BGRA8888||format==IMAGE_FORMAT_BGRX8888;
                if(format!=IMAGE_FORMAT_RGBA8888&&!blueFirst)return;
                // Fill every mip, including after device restoration. Box filtering
                // reduces flickering of small repeated faces at a distance.
                for(int frame=0;frame<vtf->FrameCount();++frame)
                for(int face=0;face<vtf->FaceCount();++face)
                for(int mip=0;mip<vtf->MipCount();++mip)
                {
                    int w,h,d;vtf->ComputeMipLevelDimensions(mip,&w,&h,&d);
                    auto out=vtf->ImageData(frame,face,mip);if(!out||w<=0||h<=0)continue;
                    const int stride=vtf->RowSizeInBytes(mip);
                    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
                    {
                        const int x0=x*Size/w,x1=std::max(x0+1,(x+1)*Size/w);
                        const int y0=y*Size/h,y1=std::max(y0+1,(y+1)*Size/h);
                        unsigned sums[4]{};
                        for(int sy=y0;sy<y1;++sy)for(int sx=x0;sx<x1;++sx)
                            for(int c=0;c<4;++c)sums[c]+=pixels[(sy*Size+sx)*4+c];
                        const unsigned count=(x1-x0)*(y1-y0);
                        auto pixel=out+y*stride+x*4;
                        pixel[0]=static_cast<unsigned char>(sums[blueFirst?2:0]/count);
                        pixel[1]=static_cast<unsigned char>(sums[1]/count);
                        pixel[2]=static_cast<unsigned char>(sums[blueFirst?0:2]/count);
                        pixel[3]=255; // Opaque photo, including the unused X channel.
                    }
                }
            }
            void Release() override {delete this;}
        };
    }
    void Load()
    {
        const auto module=reinterpret_cast<HMODULE>(&__ImageBase);
        for(int i=0;i<3;++i)
        {
            auto& photo=photos[i];if(photo.texture)continue;
            const auto resource=FindResourceW(module,MAKEINTRESOURCEW(301+i),MAKEINTRESOURCEW(10));
            if(!resource||SizeofResource(module,resource)!=Size*Size*4)continue;
            const auto memory=LoadResource(module,resource);
            photo.rgba=memory?static_cast<const unsigned char*>(LockResource(memory)):nullptr;
            if(!photo.rgba)continue;
            // No CLAMPS/CLAMPT: UVs repeat in both directions.
            auto texture=I::MaterialSystem->CreateProceduralTexture(TextureNames[i],TEXTURE_GROUP_MODEL,Size,Size,
                IMAGE_FORMAT_BGRX8888,TEXTUREFLAGS_TRILINEAR|TEXTUREFLAGS_NOLOD|TEXTUREFLAGS_PROCEDURAL);
            if(IsErrorTexture(texture))continue;
            texture->IncrementReferenceCount();
            texture->SetTextureRegenerator(new Regenerator(photo.rgba));
            texture->Download();photo.texture=texture;
        }
    }
    const char* TextureName(int index)
    {return index>=0&&index<3&&photos[index].texture?TextureNames[index]:nullptr;}
    void Draw(int selection,float x,float y,float width,float height)
    {
        if(selection<1||selection>3||width<=0||height<=0)return;
        auto& photo=photos[selection-1];if(!photo.rgba)return;
        if(!photo.surface||!I::MatSystemSurface->IsTextureIDValid(photo.surface))
            photo.surface=H::Draw.CreateTextureFromArray(photo.rgba,Size,Size);
        if(!photo.surface||!I::MatSystemSurface->IsTextureIDValid(photo.surface))return;
        I::MatSystemSurface->DrawSetColor(255,255,255,255);
        I::MatSystemSurface->DrawSetTexture(photo.surface);
        I::MatSystemSurface->DrawTexturedRect(int(x),int(y),int(x+width),int(y+height));
    }
    void Unload()
    {
        for(auto& photo:photos)
        {
            if(photo.surface){I::MatSystemSurface->DeleteTextureByID(photo.surface);I::MatSystemSurface->DestroyTextureID(photo.surface);}
            if(photo.texture){photo.texture->SetTextureRegenerator(nullptr);photo.texture->DecrementReferenceCount();}
            photo={};
        }
    }
}
