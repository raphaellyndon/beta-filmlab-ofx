/*
 * BETA FilmLab OFX v1.0 — photochemical film emulation for DaVinci Resolve
 * BETA IMAGE LAB · 贝塔影像实验室
 *
 * Pipeline: 13 camera logs -> 5 film stocks -> 2383 print -> optical halation
 * -> animated grain -> gate weave -> dust/scratches -> vignette -> flicker.
 */
#include "filmlab_math.h"

#ifndef OFX_EXPORT
  #if defined(_WIN32)
    #define OFX_EXPORT __declspec(dllexport)
  #else
    #define OFX_EXPORT __attribute__((visibility("default")))
  #endif
#endif

// ---------------------------------------------------------------- suites
static OfxImageEffectSuiteV1* gEffectSuite = 0;
static OfxPropertySuiteV1*    gPropSuite   = 0;
static OfxParameterSuiteV1*   gParamSuite  = 0;

#define CHECK(s) do { OfxStatus _st = (s); \
    if (_st != kOfxStatOK && _st != kOfxStatReplyDefault) return _st; } while(0)

// ---------------------------------------------------------------- math (local)
static inline float sstep(float a,float b,float x){
    float t=(x-a)/(b-a); t=t<0.f?0.f:(t>1.f?1.f:t); return t*t*(3.f-2.f*t); }

// ---------------------------------------------------------------- params
struct Params {
    int camera, stock, gauge;
    double exposure, printCon, pr, pg, pb;
    double gAmt, gSize, gSh, gMid, gHi, gCol, gSeed;
    double halAmt, halRadius, weaveAmt, dustAmt, vignette, flicker;
};

static OfxStatus getParams(OfxImageEffectHandle h, Params& p) {
    // OFX: an image effect handle is also a param set handle (explicit cast)
    OfxParamSetHandle psh = (OfxParamSetHandle)h;
    OfxParamHandle ph; int iv; double dv;
    #define GETI(name,dst) CHECK(gParamSuite->paramGetHandle(psh,name,&ph,0)); \
        CHECK(gParamSuite->paramGetValue(ph,&iv)); dst=iv;
    #define GETD(name,dst) CHECK(gParamSuite->paramGetHandle(psh,name,&ph,0)); \
        CHECK(gParamSuite->paramGetValue(ph,&dv)); dst=dv;
    GETI("camera",p.camera); GETI("stock",p.stock); GETI("gauge",p.gauge);
    GETD("exposure",p.exposure); GETD("printCon",p.printCon);
    GETD("printerR",p.pr); GETD("printerG",p.pg); GETD("printerB",p.pb);
    GETD("grainAmt",p.gAmt); GETD("grainSize",p.gSize);
    GETD("grainSh",p.gSh); GETD("grainMid",p.gMid); GETD("grainHi",p.gHi);
    GETD("grainCol",p.gCol); GETD("grainSeed",p.gSeed);
    GETD("halAmt",p.halAmt); GETD("halRadius",p.halRadius);
    GETD("weaveAmt",p.weaveAmt); GETD("dustAmt",p.dustAmt);
    GETD("vignette",p.vignette); GETD("flicker",p.flicker);
    #undef GETI
    #undef GETD
    return kOfxStatOK;
}

static OfxStatus defineParams(OfxImageEffectHandle h) {
    OfxParamSetHandle psh = (OfxParamSetHandle)h;
    OfxParamHandle ph; OfxPropertySetHandle ps;
    #define CHOICE(name,label,def,opts,no) \
        CHECK(gParamSuite->paramDefine(psh,kOfxParamTypeChoice,name,&ps)); \
        CHECK(gPropSuite->propSetString(ps,kOfxPropLabel,0,label)); \
        for(int _i=0;_i<no;++_i) CHECK(gPropSuite->propSetString(ps,kOfxParamPropChoiceOption,_i,opts[_i])); \
        CHECK(gPropSuite->propSetInt(ps,kOfxParamPropDefault,0,def));
    #define DBL(name,label,def,mn,mx) \
        CHECK(gParamSuite->paramDefine(psh,kOfxParamTypeDouble,name,&ps)); \
        CHECK(gPropSuite->propSetString(ps,kOfxPropLabel,0,label)); \
        CHECK(gPropSuite->propSetDouble(ps,kOfxParamPropDefault,0,def)); \
        CHECK(gPropSuite->propSetDouble(ps,kOfxParamPropMin,0,mn)); \
        CHECK(gPropSuite->propSetDouble(ps,kOfxParamPropMax,0,mx));
    const char* cams[]={"Sony S-Log3","Sony S-Log2","Panasonic V-Log","Canon C-Log2",
        "Canon C-Log3","Nikon N-Log","DJI D-Log","DJI D-Log2*","BMD Film Gen5",
        "RED Log3G10","ARRI LogC3","ARRI LogC4","Apple Log"};
    const char* stocks[]={"5219 500T","5207 250D","5203 50D","5213 200T","5222 Double-X BW"};
    const char* gauges[]={"Super 8","16mm","35mm","65mm"};
    CHOICE("camera","Camera",0,cams,13);
    CHOICE("stock","Film Stock",0,stocks,5);
    DBL("exposure","Exposure",1.0,0.25,4.0);
    DBL("printCon","Print Contrast",1.0,0.7,1.3);
    DBL("printerR","Printer Red",1.0,0.5,2.0);
    DBL("printerG","Printer Green",1.0,0.5,2.0);
    DBL("printerB","Printer Blue",1.0,0.5,2.0);
    DBL("grainAmt","Grain Amount",0.35,0.0,1.0);
    DBL("grainSize","Grain Size px",1.6,0.3,6.0);
    CHOICE("gauge","Film Gauge",2,gauges,4);
    DBL("grainSh","Grain Shadows",1.0,0.0,2.0);
    DBL("grainMid","Grain Mids",1.0,0.0,2.0);
    DBL("grainHi","Grain Highlights",0.55,0.0,2.0);
    DBL("grainCol","Grain Colour",0.3,0.0,1.0);
    DBL("grainSeed","Grain Seed",0.0,0.0,4096.0);
    DBL("halAmt","Halation",0.5,0.0,1.0);
    DBL("halRadius","Halation Radius",24.0,4.0,96.0);
    DBL("weaveAmt","Gate Weave",0.0,0.0,4.0);
    DBL("dustAmt","Dust & Scratches",0.0,0.0,1.0);
    DBL("vignette","Vignette",0.0,0.0,1.0);
    DBL("flicker","Film Flicker",0.0,0.0,1.0);
    #undef CHOICE
    #undef DBL
    return kOfxStatOK;
}

// ---------------------------------------------------------------- images
struct Img { float* data; int w,h,rowPx; OfxPropertySetHandle ps; };
static OfxStatus fetchImg(OfxImageEffectHandle h,const char* clip,double t,Img& im){
    OfxImageClipHandle ch;
    CHECK(gEffectSuite->clipGetHandle(h,clip,&ch,0));
    CHECK(gEffectSuite->clipGetImage(ch,t,0,&im.ps));
    int b[4]; CHECK(gPropSuite->propGetIntN(im.ps,kOfxImagePropBounds,4,b));
    im.w=b[2]-b[0]; im.h=b[3]-b[1];
    int rb; CHECK(gPropSuite->propGetInt(im.ps,kOfxImagePropRowBytes,0,&rb));
    im.rowPx=rb/(4*(int)sizeof(float));
    void* p; CHECK(gPropSuite->propGetPointer(im.ps,kOfxImagePropData,0,&p));
    im.data=(float*)p; return kOfxStatOK;
}
static inline void getPx(const Img& im,int x,int y,float&r,float&g,float&b){
    x=x<0?0:(x>=im.w?im.w-1:x); y=y<0?0:(y>=im.h?im.h-1:y);
    float* p=im.data+(size_t)y*im.rowPx+x*4; r=p[0];g=p[1];b=p[2];
}
static inline void getBilinear(const Img& im,float x,float y,float&r,float&g,float&b){
    int x0=(int)floorf(x),y0=(int)floorf(y); float fx=x-x0,fy=y-y0;
    float r0,g0,b0,r1,g1,b1,r2,g2,b2,r3,g3,b3;
    getPx(im,x0,y0,r0,g0,b0); getPx(im,x0+1,y0,r1,g1,b1);
    getPx(im,x0,y0+1,r2,g2,b2); getPx(im,x0+1,y0+1,r3,g3,b3);
    float t0x=r0+(r1-r0)*fx, t1x=r2+(r3-r2)*fx; r=t0x+(t1x-t0x)*fy;
    t0x=g0+(g1-g0)*fx; t1x=g2+(g3-g2)*fx; g=t0x+(t1x-t0x)*fy;
    t0x=b0+(b1-b0)*fx; t1x=b2+(b3-b2)*fx; b=t0x+(t1x-t0x)*fy;
}

// ---------------------------------------------------------------- render
static OfxStatus doRender(OfxImageEffectHandle h,double time,const OfxRectI& win){
    Params P; CHECK(getParams(h,P));
    Img src,dst;
    CHECK(fetchImg(h,"Source",time,src));
    CHECK(fetchImg(h,"Output",time,dst));
    int W=dst.w,H=dst.h;
    int ci=P.camera<0?0:(P.camera>12?12:P.camera);
    int si=P.stock<0?0:(P.stock>4?4:P.stock);
    const Stock& S=STOCKS[si]; DecFn dec=DECODES[ci]; const float* GM=GAMUT[ci];

    float wx=0,wy=0;
    if(P.weaveAmt>0.0001){
        wx=(hashI((float)time,1.f,11.f)-0.5f)*2.f*(float)P.weaveAmt;
        wy=(hashI((float)time,2.f,11.f)-0.5f)*2.f*(float)P.weaveAmt;
    }
    float flick=1.f;
    if(P.flicker>0.0001)
        flick=1.f+(float)P.flicker*0.025f*(hashI((float)time,7.f,3.f)*2.f-1.f);

    std::vector<float> bufA((size_t)W*H*4);
    for(int y=win.y1;y<win.y2;++y) for(int x=win.x1;x<win.x2;++x){
        float sr,sg,sb;
        if(wx!=0||wy!=0) getBilinear(src,(float)x-wx,(float)y-wy,sr,sg,sb);
        else getPx(src,x,y,sr,sg,sb);
        float lr=dec(sr),lg=dec(sg),lb=dec(sb);
        float R=GM[0]*lr+GM[1]*lg+GM[2]*lb;
        float G=GM[3]*lr+GM[4]*lg+GM[5]*lb;
        float B=GM[6]*lr+GM[7]*lg+GM[8]*lb;
        R=R>0?R:0;G=G>0?G:0;B=B>0?B:0;
        if(S.bw){float l=R*0.2126f+G*0.7152f+B*0.0722f;R=G=B=l;}
        R=fmaxf(R*(float)P.exposure*S.bal[0],1e-6f);
        G=fmaxf(G*(float)P.exposure*S.bal[1],1e-6f);
        B=fmaxf(B*(float)P.exposure*S.bal[2],1e-6f);
        float dr=hd(l10(R),S.dmin,S.dmax,S.g,-0.7447275f);
        float dg=hd(l10(G),S.dmin,S.dmax,S.g,-0.7447275f);
        float db=hd(l10(B),S.dmin,S.dmax,S.g,-0.7447275f);
        float er=S.li[0]*(float)P.pr*powf(10.f,-dr);
        float eg=S.li[1]*(float)P.pg*powf(10.f,-dg);
        float eb=S.li[2]*(float)P.pb*powf(10.f,-db);
        float gp=2.00f*(float)P.printCon;
        float pr_=hd(l10(er),0.08f,3.80f,gp,S.piv[0]);
        float pg_=hd(l10(eg),0.08f,3.80f,gp,S.piv[1]);
        float pb_=hd(l10(eb),0.08f,3.80f,gp,S.piv[2]);
        float* d=&bufA[((size_t)y*W+x)*4];
        d[0]=oetf(fmaxf(powf(10.f,-pr_)/S.nrm[0],0.f));
        d[1]=oetf(fmaxf(powf(10.f,-pg_)/S.nrm[1],0.f));
        d[2]=oetf(fmaxf(powf(10.f,-pb_)/S.nrm[2],0.f));
        d[3]=1.f;
    }

    // halation: bright-pass half-res -> separable gaussian -> amber screen
    std::vector<float> bufH; int hW=0,hH=0;
    if(P.halAmt>0.0001){
        hW=(W+1)/2; hH=(H+1)/2;
        std::vector<float> hp((size_t)hW*hH*3,0.f);
        for(int y=0;y<hH;++y)for(int x=0;x<hW;++x){
            float r=0,g=0,b=0;
            for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
                int sx=x*2+dx,sy=y*2+dy;
                if(sx<W&&sy<H){float*p=&bufA[((size_t)sy*W+sx)*4];r+=p[0];g+=p[1];b+=p[2];}}
            r*=0.25f;g*=0.25f;b*=0.25f;
            float m=sstep(0.55f,0.95f,r*0.2126f+g*0.7152f+b*0.0722f);
            float*d=&hp[((size_t)y*hW+x)*3]; d[0]=r*m;d[1]=g*m;d[2]=b*m;
        }
        float sigma=(float)P.halRadius/6.f; if(sigma<0.75f)sigma=0.75f;
        int rad=(int)ceilf(3.f*sigma); if(rad>32)rad=32; if(rad<1)rad=1;
        std::vector<float> kern(2*rad+1); float ks=0;
        for(int i=-rad;i<=rad;++i){kern[i+rad]=expf(-0.5f*i*i/(sigma*sigma));ks+=kern[i+rad];}
        for(float&k:kern)k/=ks;
        std::vector<float> tmp((size_t)hW*hH*3);
        for(int y=0;y<hH;++y)for(int x=0;x<hW;++x)for(int c=0;c<3;++c){
            float s=0; for(int i=-rad;i<=rad;++i){
                int sx=x+i; sx=sx<0?0:(sx>=hW?hW-1:sx);
                s+=hp[((size_t)y*hW+sx)*3+c]*kern[i+rad];}
            tmp[((size_t)y*hW+x)*3+c]=s;}
        bufH.assign((size_t)hW*hH*3,0.f);
        for(int y=0;y<hH;++y)for(int x=0;x<hW;++x)for(int c=0;c<3;++c){
            float s=0; for(int i=-rad;i<=rad;++i){
                int sy=y+i; sy=sy<0?0:(sy>=hH?hH-1:sy);
                s+=tmp[((size_t)sy*hW+x)*3+c]*kern[i+rad];}
            bufH[((size_t)y*hW+x)*3+c]=s;}
    }

    float sizeMul=1.f,intMul=1.f;
    if(P.gauge==0){sizeMul=3.8f;intMul=3.2f;}
    else if(P.gauge==1){sizeMul=2.15f;intMul=2.f;}
    else if(P.gauge==3){sizeMul=0.5f;intMul=0.45f;}
    if(si==0)intMul*=1.25f; else if(si==2)intMul*=0.55f; else if(si==4)intMul*=0.9f;
    float gsz=fmaxf(0.30f,(float)P.gSize*sizeMul*((float)H/1080.f));
    float seed=(float)P.gSeed+(float)time*17.f;

    for(int y=win.y1;y<win.y2;++y) for(int x=win.x1;x<win.x2;++x){
        float*a=&bufA[((size_t)y*W+x)*4];
        float r=a[0],g=a[1],b=a[2];
        if(!bufH.empty()){
            float hx=(float)x*0.5f,hy=(float)y*0.5f;
            int x0=(int)hx,y0=(int)hy; float fx=hx-x0,fy=hy-y0;
            float hr=0,hg=0,hb=0;
            for(int dy=0;dy<=1;++dy)for(int dx=0;dx<=1;++dx){
                int sx=x0+dx; sx=sx<0?0:(sx>=hW?hW-1:sx);
                int sy=y0+dy; sy=sy<0?0:(sy>=hH?hH-1:sy);
                float*p=&bufH[((size_t)sy*hW+sx)*3];
                float w=(dx?fx:1.f-fx)*(dy?fy:1.f-fy);
                hr+=p[0]*w;hg+=p[1]*w;hb+=p[2]*w;
            }
            float amt=(float)P.halAmt;
            r=1.f-(1.f-r)*(1.f-hr*amt);
            g=1.f-(1.f-g)*(1.f-hg*amt*0.82f);
            b=1.f-(1.f-b)*(1.f-hb*amt*0.60f);
        }
        if(P.gAmt>0.0001){
            float lum=r*0.2126f+g*0.7152f+b*0.0722f, Lc=lum<0?0:(lum>1?1:lum);
            float wS=sstep(0.f,0.5f,1.f-Lc*2.f), wH=sstep(0.5f,1.f,Lc);
            float wM=sstep(0.f,1.f,1.f-fabsf(Lc-0.45f)*2.2f);
            float gate=powf((4.f*Lc*(1.f-Lc))<0?0:((4.f*Lc*(1.f-Lc))>1?1:(4.f*Lc*(1.f-Lc))),0.35f);
            float amt=(float)P.gAmt*intMul*((float)P.gSh*wS+(float)P.gMid*wM+(float)P.gHi*wH)*gate*0.15f;
            float col=(float)P.gCol;
            float gM=hashI((float)x/gsz*3.1f,(float)y/gsz*3.1f,seed)*2.f-1.f;
            float gR=hashI((float)x/gsz*3.1f,(float)y/gsz*3.1f,seed+101.f)*2.f-1.f;
            float gG=hashI((float)x/gsz*3.1f,(float)y/gsz*3.1f,seed+227.f)*2.f-1.f;
            float gB=hashI((float)x/gsz*3.1f,(float)y/gsz*3.1f,seed+353.f)*2.f-1.f;
            r+=(gM+(gR-gM)*col)*amt; g+=(gM+(gG-gM)*col)*amt; b+=(gM+(gB-gM)*col)*amt;
        }
        if(P.dustAmt>0.0001){
            float ds=(float)time*7.f;
            if(hashI((float)x,(float)y,ds)<0.0004f*(float)P.dustAmt){
                float v=hashI((float)x,(float)y,ds+5.f)>0.5f?1.f:-1.f;
                float d2=v*0.35f*(float)P.dustAmt;
                r=r+d2>1?1:(r+d2<0?0:r+d2); g=g+d2>1?1:(g+d2<0?0:g+d2); b=b+d2>1?1:(b+d2<0?0:b+d2);
            }
            if(hashI(floorf((float)x/3.f),ds,9.f)<0.00008f*(float)P.dustAmt){
                float d2=0.25f*(float)P.dustAmt;
                r=r-d2<0?0:r-d2; g=g-d2<0?0:g-d2; b=b-d2<0?0:b-d2;
            }
        }
        if(P.vignette>0.0001){
            float nx=((float)x/(float)W-0.5f)*2.f, ny=((float)y/(float)H-0.5f)*2.f;
            float vv=1.f-(float)P.vignette*0.45f*sstep(0.35f,1.6f,nx*nx+ny*ny);
            r*=vv;g*=vv;b*=vv;
        }
        r*=flick;g*=flick;b*=flick;
        r=r/(1.f+powf(fmaxf(r-1.f,0.f)*4.f,2.f));
        g=g/(1.f+powf(fmaxf(g-1.f,0.f)*4.f,2.f));
        b=b/(1.f+powf(fmaxf(b-1.f,0.f)*4.f,2.f));
        float*d=dst.data+(size_t)y*dst.rowPx+x*4;
        d[0]=r;d[1]=g;d[2]=b;d[3]=1.f;
    }
    gEffectSuite->clipReleaseImage(src.ps);
    gEffectSuite->clipReleaseImage(dst.ps);
    return kOfxStatOK;
}

// ---------------------------------------------------------------- actions
static OfxStatus describeAction(OfxImageEffectHandle h){
    OfxPropertySetHandle ps;
    CHECK(gEffectSuite->getPropertySet(h,&ps));
    CHECK(gPropSuite->propSetString(ps,kOfxPropLabel,0,"BETA FilmLab"));
    CHECK(gPropSuite->propSetString(ps,kOfxImageEffectPluginPropGrouping,0,"BETA IMAGE LAB"));
    CHECK(gPropSuite->propSetString(ps,kOfxPropVersionLabel,0,"v1.0 OFX"));
    CHECK(gPropSuite->propSetString(ps,kOfxImageEffectPropSupportedContexts,0,kOfxImageEffectContextFilter));
    CHECK(gPropSuite->propSetString(ps,kOfxImageEffectPropSupportedPixelDepths,0,kOfxBitDepthFloat));
    CHECK(defineParams(h));
    // declare clips (clipDefine returns the property set directly)
    OfxPropertySetHandle cps;
    CHECK(gEffectSuite->clipDefine(h,"Source",&cps));
    CHECK(gPropSuite->propSetString(cps,kOfxImageEffectPropSupportedComponents,0,kOfxImageComponentRGBA));
    CHECK(gEffectSuite->clipDefine(h,"Output",&cps));
    CHECK(gPropSuite->propSetString(cps,kOfxImageEffectPropSupportedComponents,0,kOfxImageComponentRGBA));
    return kOfxStatOK;
}

static OfxStatus mainEntry(const char* action,const void* handle,
                           OfxPropertySetHandle inArgs,OfxPropertySetHandle outArgs){
    if(strcmp(action,kOfxActionDescribe)==0) return kOfxStatOK;
    if(strcmp(action,kOfxImageEffectActionDescribeInContext)==0)
        return describeAction((OfxImageEffectHandle)handle);
    if(strcmp(action,kOfxImageEffectActionGetClipPreferences)==0){
        OfxPropertySetHandle ps;
        CHECK(gEffectSuite->getPropertySet((OfxImageEffectHandle)handle,&ps));
        // float RGBA already declared; nothing more needed
        return kOfxStatOK;
    }
    if(strcmp(action,kOfxImageEffectActionRender)==0){
        double time; OfxRectI win; OfxPropertySetHandle ps;
        CHECK(gPropSuite->propGetDouble(inArgs,kOfxPropTime,0,&time));
        CHECK(gPropSuite->propGetIntN(inArgs,kOfxImageEffectPropRenderWindow,4,&win.x1));
        return doRender((OfxImageEffectHandle)handle,time,win);
    }
    return kOfxStatReplyDefault;
}

// ---------------------------------------------------------------- plugin struct
static void setHost(OfxHost* h){
    gEffectSuite=(OfxImageEffectSuiteV1*)h->fetchSuite(h->host,kOfxImageEffectSuite,1);
    gPropSuite=(OfxPropertySuiteV1*)h->fetchSuite(h->host,kOfxPropertySuite,1);
    gParamSuite=(OfxParameterSuiteV1*)h->fetchSuite(h->host,kOfxParameterSuite,1);
}

static OfxPlugin gPlugin = {
    kOfxImageEffectPluginApi, 1,
    "com.betaimagelab.filmlab", 1, 0,
    setHost, mainEntry
};

extern "C" {
OFX_EXPORT int OfxGetNumberOfPlugins(void){ return 1; }
OFX_EXPORT OfxPlugin* OfxGetPlugin(int nth){
    if(nth==0) return &gPlugin;
    return 0;
}
}
