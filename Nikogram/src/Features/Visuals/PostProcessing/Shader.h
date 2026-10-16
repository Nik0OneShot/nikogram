#pragma once
namespace PostFX
{
// Original shaders. All samples are clamped by the sampler. Input/output are
// display-referred, matching the native post-tonemap framebuffer.
inline constexpr char ShaderSource[] = R"hlsl(
sampler2D image : register(s0);
sampler2D auxiliary : register(s1);
sampler2D depthImage : register(s2);
float4 p[12] : register(c0);
float luminance(float3 c) { return dot(c,float3(.299,.587,.114)); }
float hash(float2 c) { return frac(sin(dot(c,float2(12.9898,78.233)))*43758.5453); }
float depthAt(float2 uv) {
    float d=tex2D(depthImage,uv).r;
    return p[7].x*p[7].y/max(.00001,p[7].y-d*(p[7].y-p[7].x));
}
float4 copy(float2 uv:TEXCOORD0):COLOR0 { return tex2D(image,uv); }
float4 threshold(float2 uv:TEXCOORD0):COLOR0 {
    float3 c=tex2D(image,uv).rgb; return float4(c*saturate((luminance(c)-p[1].z)/max(.01,1-p[1].z)),1);
}
float4 blur(float2 uv:TEXCOORD0):COLOR0 {
    float2 d=p[0].xy*p[1].xy;
    return tex2D(image,uv)*.227027+
      (tex2D(image,uv+d*1.384615)+tex2D(image,uv-d*1.384615))*.316216+
      (tex2D(image,uv+d*3.230769)+tex2D(image,uv-d*3.230769))*.070270;
}
float4 bloom(float2 uv:TEXCOORD0):COLOR0 {
    return float4(tex2D(image,uv).rgb+tex2D(auxiliary,uv).rgb*p[1].w,tex2D(image,uv).a);
}
float4 aa(float2 uv:TEXCOORD0):COLOR0 {
    float2 d=p[0].xy;
    float3 c=tex2D(image,uv).rgb, nw=tex2D(image,uv-d).rgb, ne=tex2D(image,uv+float2(d.x,-d.y)).rgb;
    float3 sw=tex2D(image,uv+float2(-d.x,d.y)).rgb, se=tex2D(image,uv+d).rgb;
    float l=luminance(c), a=luminance(nw), b=luminance(ne), e=luminance(sw), f=luminance(se);
    float lo=min(l,min(min(a,b),min(e,f))), hi=max(l,max(max(a,b),max(e,f)));
    if(hi-lo<max(.0312,hi*.125))return float4(c,tex2D(image,uv).a);
    float2 dir=float2(-((a+b)-(e+f)),((a+e)-(b+f)));
    float reduce=max((a+b+e+f)*(.25*.125),.0078125);
    dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),-8,8)*d;
    float3 x=.5*(tex2D(image,uv+dir*(-1.0/6)).rgb+tex2D(image,uv+dir*(1.0/6)).rgb);
    float3 y=x*.5+.25*(tex2D(image,uv-dir*.5).rgb+tex2D(image,uv+dir*.5).rgb);
    float ly=luminance(y);return float4(lerp(c,(ly<lo||ly>hi)?x:y,p[2].x),tex2D(image,uv).a);
}
float4 sharpen(float2 uv:TEXCOORD0):COLOR0 {
    float2 d=p[0].xy;
    float3 c=tex2D(image,uv).rgb;
    float3 b=(tex2D(image,uv+float2(d.x,0)).rgb+tex2D(image,uv-float2(d.x,0)).rgb+
      tex2D(image,uv+float2(0,d.y)).rgb+tex2D(image,uv-float2(0,d.y)).rgb)*.25;
    return float4(saturate(c+clamp(c-b,-.12,.12)*p[2].y),tex2D(image,uv).a);
}
float4 grade(float2 uv:TEXCOORD0):COLOR0 {
    float4 src=tex2D(image,uv);float3 c=src.rgb;
    c=lerp(luminance(c).xxx,c,p[3].x);c=(c-.5)*p[3].y+.5;
    c+=float3(p[3].z,-p[3].w,-p[3].z)*.15;
    float l=luminance(c);c+=p[4].x*(1-l)*(1-l)+p[4].y*l*l;
    if(p[4].w==1)c=lerp(c,float3(l*.95,l*.85,l*.68),p[4].z);
    if(p[4].w==2)c=lerp(c,c*float3(.92,1.02,1.12),p[4].z);
    if(p[4].w==3)c=lerp(c,c*float3(1.12,1.02,.9),p[4].z);
    return float4(saturate(lerp(src.rgb,c,p[5].x)),src.a);
}
float4 exposure(float2 uv:TEXCOORD0):COLOR0 {
    float4 c=tex2D(image,uv);c.rgb*=exp2(p[2].z);
    float l=max(.00001,luminance(c.rgb));
    float compressed=lerp(l,l/(1+l),p[2].w);
    c.rgb*=compressed/l;return float4(saturate(c.rgb),c.a);
}
float4 motion(float2 uv:TEXCOORD0):COLOR0 {
    float2 d=clamp(p[6].xy+(uv-.5)*p[6].z,-.08,.08);
    float4 c=0;
    [unroll]for(int i=0;i<9;i++) c+=tex2D(image,uv+d*(i/8.0-.5));
    return c/9;
}
float4 dof(float2 uv:TEXCOORD0):COLOR0 {
    float z=depthAt(uv), focus=p[7].w>.5?depthAt(float2(.5,.5)):p[7].z;
    float coc=saturate(abs(z-focus)/max(1,p[8].x))*p[8].y;
    float4 c=tex2D(image,uv);float weight=1;
    [unroll]for(int i=0;i<8;i++) {
      float a=i*.785398;float2 q=uv+float2(cos(a),sin(a))*p[0].xy*coc;
      // Do not bleed a distant background across a foreground silhouette.
      float w=abs(depthAt(q)-z)<=max(4,z*.02)?1:0;c+=tex2D(image,q)*w;weight+=w;
    }
    return c/weight;
}
float4 style(float2 uv:TEXCOORD0):COLOR0 {
    float4 c=tex2D(image,uv);
    if(p[9].y>.5)c.rgb=floor(saturate(c.rgb)*p[9].z+.5)/p[9].z;
    if(p[9].w>.5) {
      float2 pixel=uv/p[0].xy;float2 cell=frac(pixel/max(2,p[10].x))-.5;
      float dots=smoothstep(.1,.24,dot(cell,cell));
      c.rgb*=lerp(1,dots,p[10].y*(1-luminance(c.rgb)));
    }
    if(p[10].z>.5) {
      float l=luminance(c.rgb);
      float edge=abs(l-luminance(tex2D(image,uv+p[0].xy).rgb));
      c.rgb*=1-saturate(edge*8)*p[10].w;
    }
    return c;
}
float4 lens(float2 uv:TEXCOORD0):COLOR0 {
    float4 c=tex2D(image,uv);float2 radial=uv-.5;
    if(p[5].y>0) {
      float2 d=radial*p[5].y*.02;c.r=tex2D(image,uv+d).r;c.b=tex2D(image,uv-d).b;
    }
    c.rgb*=1-p[5].z*smoothstep(.15,.7,length(radial));
    c.rgb+=(hash(uv/p[0].xy+floor(p[0].z*24))-.5)*p[5].w;
    if(p[11].x>0) {
      float2 cell=floor(uv*float2(90,55));float2 local=frac(uv*float2(90,55))-.5;
      float dirt=step(.94,hash(cell))*smoothstep(.3,0,length(local));
      c.rgb+=dirt*p[11].x*luminance(tex2D(auxiliary,uv).rgb)*float3(.6,.48,.3);
    }
    if(uv.y<p[11].y||uv.y>1-p[11].y)c.rgb=0;
    return float4(saturate(c.rgb),c.a);
}
)hlsl";
}
