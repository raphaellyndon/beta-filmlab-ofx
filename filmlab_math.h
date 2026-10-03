/*
 * BETA FilmLab OFX v1.0 — photochemical film emulation for DaVinci Resolve
 * BETA IMAGE LAB · 贝塔影像实验室
 *
 * True spatial effects (impossible in DCTL):
 *   - Optical halation: bright-pass + multi-scale separable Gaussian + amber composite
 *   - Gate weave: per-frame geometric displacement (bilinear resample)
 *   - Dust & scratches: temporally-coherent particles
 * Plus the full photochemical pipeline: 13 camera logs -> 5 film stocks ->
 * 2383 print -> animated procedural grain -> vignette -> flicker.
 *
 * Grain algorithm adapted from "RCS Film Grain" by Red Coral Studios (MIT).
 * OFX API: raw C, no Support-library dependency. Builds on Linux/macOS/Windows.
 */

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"

// ---------------------------------------------------------------- math
// (suites/CHECK live in BETA_FilmLab.cpp; this header is pure math)
static inline float clampf(float x, float a, float b){ return x<a?a:(x>b?b:x); }
static inline float satf(float x){ return clampf(x,0.f,1.f); }
static inline float hd(float LE,float dmin,float dmax,float gamma,float pivot){
    float k = 4.f*gamma/(dmax-dmin);
    return dmin + (dmax-dmin)/(1.f+expf(-k*(LE-pivot)));
}
static inline float l10(float x){ return logf(x>1e-10f?x:1e-10f)/logf(10.f); }
static inline float oetf(float L){
    return (L<0.018f)?(4.5f*L):(1.099f*powf(L>1e-10f?L:1e-10f,0.45f)-0.099f);
}
// integer hash -> [0,1)
static inline float hashI(float x,float y,float seed){
    unsigned int h = (unsigned int)((int)floorf(x))*0x9E3779B1u
                   ^ (unsigned int)((int)floorf(y))*0x85EBCA77u
                   ^ (unsigned int)((int)floorf(seed))*0xC2B2AE3Du;
    h^=h>>16; h*=0x7FEB352Du; h^=h>>15; h*=0x846CA68Bu; h^=h>>16;
    return (float)(h&0x00FFFFFFu)*(1.f/16777216.f);
}

// ---- camera log decodes (verified vs colour-science, see DCTL v2.0) ----
static inline float d_slog3(float v){
    if(v>=0.1673601f) return powf(10.f,(v*1023.f-420.f)/261.5f)*0.19f-0.01f;
    return (v*1023.f-95.f)*0.01125f/76.2102946929f; }
static inline float d_slog2(float v){
    float x=(v*1023.f-64.f)/876.f;
    float l=(v>=0.08825129f)?(powf(10.f,(x-0.646596f)/0.432699f)-0.037584f):((x-0.03000122f)/5.f);
    return 1.4129032f*l*0.9f; }
static inline float d_vlog(float v){
    if(v<0.181f) return (v-0.125f)/5.6f;
    return powf(10.f,(v-0.598206f)/0.241514f)-0.00873f; }
static inline float d_clog2(float v){
    float l=(v<0.092864125f)?(-(powf(10.f,(0.092864125f-v)/0.24136077f)-1.f)/87.09937546f)
                           :((powf(10.f,(v-0.092864125f)/0.24136077f)-1.f)/87.09937546f);
    return l*0.9f; }
static inline float d_clog3(float v){
    float l;
    if(v<0.097465473f) l=(-(powf(10.f,(0.12783901f-v)/0.36726845f)-1.f)/14.98325f);
    else if(v<=0.15277891f) l=((v-0.12512219f)/1.9754798f);
    else l=((powf(10.f,(v-0.12240537f)/0.36726845f)-1.f)/14.98325f);
    return l*0.9f; }
static inline float d_nlog(float v){
    if(v<0.44183773f){ float t=v/0.63538612f; return t*t*t-0.0075f; }
    return expf((v-0.60508309f)/0.14662757f); }
static inline float d_dlog(float v){
    if(v<=0.14f) return (v-0.0929f)/6.025f;
    return (powf(10.f,3.89616f*v-2.27752f)-0.0108f)/0.9892f; }
static inline float d_dlog2(float v){ // APPROX
    float toe=(v-0.0929f)/6.025f;
    float mn=(powf(10.f,3.89616f*v-2.27752f)-0.0108f)/0.9892f;
    float w=1.f/(1.f+expf(-40.f*(v-0.14f)));
    return toe*(1.f-w)+mn*w; }
static inline float d_bmdf5(float v){
    if(v<0.13388378f) return (v-0.09246575f)/8.28360593f;
    return expf((v-0.53001334f)/0.08692876f)-0.00549407f; }
static inline float d_rlog310(float v){
    if(v<0.f) return v/15.1927f-0.01f;
    return (powf(10.f,v/0.224282f)-1.f)/155.975327f-0.01f; }
static inline float d_logc3(float v){
    if(v>0.149658f) return (powf(10.f,(v-0.385537f)/0.24719f)-0.052272f)/5.555556f;
    return (v-0.092809f)/5.367655f; }
static inline float d_logc4(float v){
    if(v>=0.f) return (powf(2.f,14.f*((v-0.09286413f)/0.90713587f)+6.f)-64.f)/2231.82630907f;
    return v*0.11359721f-0.018057f; }
static inline float d_alog(float v){
    if(v>=0.2085608f) return powf(2.f,(v-0.69336945f)/0.08550479f)-0.00964052f;
    if(v<0.f) return -0.05641088f;
    return sqrtf((v>0.f?v:0.f)/47.28711236f)-0.05641088f; }

typedef float(*DecFn)(float);
static DecFn DECODES[13] = { d_slog3,d_slog2,d_vlog,d_clog2,d_clog3,d_nlog,d_dlog,
    d_dlog2,d_bmdf5,d_rlog310,d_logc3,d_logc4,d_alog };

// gamut matrices: camera-linear -> Rec.709 linear (CAT02, baked from colour-science)
static const float GAMUT[13][9] = {
 {+1.626947f,-0.540139f,-0.086809f, -0.178516f,+1.417941f,-0.239425f, -0.044436f,-0.195920f,+1.240356f}, // S-Log3
 {+1.877915f,-0.794169f,-0.083746f, -0.176807f,+1.351000f,-0.174193f, -0.026201f,-0.148422f,+1.174623f}, // S-Log2
 {+1.806574f,-0.695698f,-0.110879f, -0.170089f,+1.305955f,-0.135865f, -0.025206f,-0.154468f,+1.179674f}, // V-Log
 {+1.923861f,-0.798761f,-0.125101f, -0.204311f,+1.495899f,-0.291588f, -0.023685f,-0.420127f,+1.443812f}, // C-Log2
 {+1.923861f,-0.798761f,-0.125101f, -0.204311f,+1.495899f,-0.291588f, -0.023685f,-0.420127f,+1.443812f}, // C-Log3
 {+1.660491f,-0.587641f,-0.072850f, -0.124550f,+1.132900f,-0.008349f, -0.018151f,-0.100579f,+1.118730f}, // N-Log
 {+1.674842f,-0.579967f,-0.094927f, -0.098125f,+1.334046f,-0.235877f, -0.041009f,-0.243022f,+1.283755f}, // D-Log
 {+1.674842f,-0.579967f,-0.094927f, -0.098125f,+1.334046f,-0.235877f, -0.041009f,-0.243022f,+1.283755f}, // D-Log2
 {+1.568405f,-0.522674f,-0.045730f, -0.086359f,+1.344913f,-0.258554f, -0.052047f,-0.249155f,+1.301202f}, // BMD5
 {+1.981975f,-0.900433f,-0.081546f, -0.178143f,+1.500469f,-0.322324f, -0.101796f,-0.535263f,+1.637060f}, // R3G10
 {+1.617524f,-0.537286f,-0.080238f, -0.070573f,+1.334613f,-0.264040f, -0.021102f,-0.226954f,+1.248056f}, // LogC3
 {+1.893123f,-0.780882f,-0.112242f, -0.205700f,+1.340257f,-0.134557f, -0.012706f,-0.152185f,+1.164891f}, // LogC4
 {+1.660491f,-0.587641f,-0.072850f, -0.124550f,+1.132900f,-0.008349f, -0.018151f,-0.100579f,+1.118730f}, // ALog
};

// film stocks: balance(3), lights(3), piv2383(3), norms(3), g_neg, dmin, dmax, is_bw
// Constants baked from validated Python reference model (filmlab_model.py).
struct Stock { float bal[3], li[3], piv[3], nrm[3], g, dmin, dmax; int bw; };
static const Stock STOCKS[5] = {
 // 5219 Vision3 500T
 {{1.0000f,1.0000f,1.5500f},{0.9169f,0.9169f,1.1662f},{-1.9966f,-2.0158f,-2.0353f},{0.009010f,0.008409f,0.007847f},0.55f,0.20f,2.80f,0},
 // 5207 Vision3 250D
 {{1.0000f,1.0000f,1.0000f},{1.0000f,1.0000f,1.0000f},{-1.9590f,-1.9781f,-1.9976f},{0.009010f,0.008409f,0.007847f},0.55f,0.20f,2.80f,0},
 // 5203 Vision3 50D
 {{1.0000f,1.0000f,1.0000f},{1.0000f,1.0000f,1.0000f},{-2.0190f,-2.0381f,-2.0576f},{0.009010f,0.008409f,0.007847f},0.58f,0.22f,2.90f,0},
 // 5213 Vision3 200T
 {{1.0000f,1.0000f,1.4500f},{0.9298f,0.9298f,1.1403f},{-1.9905f,-2.0097f,-2.0292f},{0.009010f,0.008409f,0.007847f},0.55f,0.20f,2.80f,0},
 // 5222 Double-X B&W (neutral print)
 {{1.0000f,1.0000f,1.0000f},{1.0000f,1.0000f,1.0000f},{-2.0532f,-2.0532f,-2.0532f},{0.008422f,0.008422f,0.008422f},0.60f,0.25f,2.90f,1},
};
