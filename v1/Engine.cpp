#include "Audio.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <chrono>

using namespace diliyan;
std::string quote(const std::string &s) {
    std::ostringstream o;o<<'"';
    for(unsigned char c:s) {
        if(c=='"'||c=='\\')o<<'\\'<<c;
        else if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;
        else o<<c;
    }
    o<<'"';return o.str();
}
std::string metricsJSON(const Metrics &m) {
    std::ostringstream s;s<<std::setprecision(10);
    s<<"{\"frames\":"<<m.frames<<",\"seconds\":"<<m.frames/double(rate)
     <<",\"lufs\":"<<m.lufs<<",\"shortMax\":"<<m.shortMax<<",\"samplePeak\":"<<m.peak<<",\"truePeakEstimate\":"<<m.tp
     <<",\"rms\":"<<m.rms<<",\"activeRMS\":"<<m.active<<",\"bassFraction\":"<<m.bass
     <<",\"presenceFraction\":"<<m.presence<<",\"airFraction\":"<<m.air<<",\"correlation\":"<<m.correlation
     <<",\"width\":"<<m.width<<",\"crestDB\":"<<m.crest<<",\"onsetProxy\":"<<m.onset
     <<",\"leadingSilenceSeconds\":"<<m.leading<<",\"clippedFrames\":"<<m.clipped<<"}";return s.str();
}
std::string infoJSON(const SourceInfo &i) {
    std::ostringstream s;s<<"{\"sampleRate\":"<<i.sampleRate<<",\"channels\":"<<i.channels<<",\"seconds\":"<<i.seconds<<",\"bits\":"<<i.bits<<"}";return s.str();
}
void progress(double p,const std::string &text) {std::cout<<"{\"progress\":"<<p<<",\"message\":"<<quote(text)<<"}"<<std::endl;}
Metrics normalize(const fs::path &in,const fs::path &out,double base,double span) {
    Decoder d(in);WavWriter w(out);Meter m;std::array<Frame,blockSize> b{};size_t n;uint64_t done=0,next=0;
    while((n=d.read(b.data(),b.size()))) {
        for(size_t i=0;i<n;++i)m.add(b[i]);
        w.write(b.data(),n);done+=n;
        require(done<=uint64_t(rate)*1801,"Dekodierte Datei überschreitet 30-Minuten-Grenze.");
        if(done>=next) {progress(base+span*std::min(1.0,done/(std::max(1.0,d.info.seconds)*rate)),"Analysiere "+in.filename().string());next=done+rate*2;}
    }
    w.close();auto result=m.finish();
    require(result.frames>=19200,"Zu wenig dekodierte Audiodaten.");
    require(std::abs(result.frames/double(rate)-d.info.seconds)<0.1,"Dekodierte Länge weicht unerwartet ab; kein gekürzter Export.");
    require(result.lufs>-100,"Datei ist still oder unterhalb des Lautheits-Gates.");
    return result;
}
void mix(const fs::path &vocal,const fs::path &beat,const fs::path &dest,double vg,double beatPresence) {
    WavReader v(vocal),b(beat);WavWriter w(dest);StereoFilter eq(Biquad::peak(2700,0.9,beatPresence));
    std::array<Frame,blockSize> vb{},bb{},out{};size_t nv,nb;
    do {
        nv=v.read(vb.data(),vb.size());nb=b.read(bb.data(),bb.size());size_t n=std::max(nv,nb);
        for(size_t i=0;i<n;++i)out[i]=(i<nv?vb[i]*vg:Frame{})+eq.tick(i<nb?bb[i]:Frame{});
        w.write(out.data(),n);
    }while(nv||nb);
    w.close();
}
void processEQ(const fs::path &in,const fs::path &out,Plan p) {
    WavReader r(in);WavWriter w(out);MasterDSP dsp(p);std::array<Frame,blockSize>b{};size_t n;
    while((n=r.read(b.data(),b.size()))) {for(size_t i=0;i<n;++i)b[i]=dsp.tick(b[i]);w.write(b.data(),n);}w.close();
}
double limit(const fs::path &in,const fs::path &out,double level) {
    WavReader r(in);WavWriter w(out);Limiter limiter;std::array<Frame,blockSize>b{},buffer{};size_t n,used=0;uint64_t written=0;
    auto add=[&](Frame f) {
        Frame output;
        if(limiter.add(f,output)&&written<r.frames) {
            buffer[used++]=output;++written;
            if(used==buffer.size()) {w.write(buffer.data(),used);used=0;}
        }
    };
    while((n=r.read(b.data(),b.size())))for(size_t i=0;i<n;++i)add(b[i]*level);
    for(size_t i=0;i<Limiter::latency();++i)add({});
    if(used)w.write(buffer.data(),used);
    w.close();require(written==r.frames,"Limiter-Längenfehler.");return limiter.reduction;
}
void monoCopy(const fs::path &in,const fs::path &out) {
    WavReader r(in);WavWriter w(out);std::array<Frame,blockSize>b{};size_t n;
    while((n=r.read(b.data(),b.size()))) {
        for(size_t i=0;i<n;++i) {double mono=(b[i].l+b[i].r)*0.5;b[i]={mono,mono};}
        w.write(b.data(),n);
    }
    w.close();
}

int main(int argc,char **argv) {
    try {
        if(argc==3&&std::string(argv[1])=="--inspect") {
            Decoder d(argv[2]);std::cout<<infoJSON(d.info)<<std::endl;return 0;
        }
        require(argc==6&&std::string(argv[1])=="--render","Aufruf: --render VOCAL BEAT REFERENCE NEUER_PROJEKTORDNER");
        fs::path vocal=fs::canonical(argv[2]),beat=fs::canonical(argv[3]),ref=fs::canonical(argv[4]),out=fs::absolute(argv[5]);
        require(!fs::equivalent(vocal,beat)&&!fs::equivalent(vocal,ref)&&!fs::equivalent(beat,ref),"Dieselbe Datei wurde mehreren Rollen zugeordnet.");
        require(!fs::exists(out),"Projektordner existiert bereits; Überschreiben wird verhindert.");
        // Inspect all sources before allocating render files.
        Decoder vi(vocal),bi(beat),ri(ref);
        require(vi.info.seconds<=1800&&bi.info.seconds<=1800&&ri.info.seconds<=1800,"Maximal 30 Minuten pro Datei.");
        auto available=fs::space(out.parent_path()).available;
        double duration=std::max(vi.info.seconds,bi.info.seconds);
        uint64_t needed=uint64_t((vi.info.seconds+bi.info.seconds+ri.info.seconds+duration*7)*rate*8+duration*3*rate*6)+512*1024*1024;
        require(available>needed,"Nicht genügend freier Speicher für nichtdestruktive Zwischenstände.");
        fs::create_directory(out);
        progress(0,"Import und Analyse – lokale Verarbeitung");
        auto vm=normalize(vocal,out/"vocal-48k.wav",0,0.12);
        auto bm=normalize(beat,out/"beat-48k.wav",0.12,0.12);
        auto rm=normalize(ref,out/"reference-48k.wav",0.24,0.12);
        double vocalDB=std::clamp(bm.active-6-vm.active,-3.0,3.0);
        double masking=bm.presence>0.12&&vm.presence>0.10?-0.6:0;
        progress(0.37,"Originalmix und konservative Stem-Balance");
        mix(out/"vocal-48k.wav",out/"beat-48k.wav",out/"Original.wav",1,0);
        mix(out/"vocal-48k.wav",out/"beat-48k.wav",out/"Balanced.wav",gain(vocalDB),masking);
        auto original=measure(out/"Original.wav"),balanced=measure(out/"Balanced.wav");
        require(balanced.lufs>-100,"Vocal und Beat löschen sich aus oder ergeben keine messbare Lautheit.");
        std::array<Metrics,3> rendered;std::array<Plan,3> plans;std::array<double,3> limiterDB{},driveDB{},safetyDB{};
        for(int version=0;version<3;++version) {
            std::string label(1,char('A'+version));
            progress(0.40+version*0.18,"Master "+label+": adaptiver EQ und Dynamik");
            auto p=plan(balanced,rm,version);plans[version]=p;
            auto processed=out/(label+"-processed.wav"),limited=out/(label+"-limited.wav"),output=out/("Master-"+label+".wav");
            processEQ(out/"Balanced.wav",processed,p);auto eq=measure(processed);
            // Do not force the reference's loudness at the expense of >3dB
            // peak pressure or add more than 6dB to a quiet mix.
            double drive=std::min({p.target-eq.lufs,6.0,-1.3-eq.tp+3.0});
            driveDB[version]=drive;
            limiterDB[version]=limit(processed,limited,gain(drive));auto lm=measure(limited);
            double correction=std::min(0.0,-1.1-lm.tp);safetyDB[version]=correction;
            scaleFile(limited,output,gain(correction),24);rendered[version]=measure(output);
            // Verify the exported, dithered PCM rather than an upstream buffer.
            require(rendered[version].frames==balanced.frames,"Master-Länge stimmt nicht.");
            require(rendered[version].clipped==0&&rendered[version].tp<=-1.0,"Export überschreitet True-Peak-Schätzgrenze.");
            fs::remove(processed);fs::remove(limited);
        }
        // Conservative common playback level, peak-safe for every version.
        double compare=-18;
        if(original.lufs>-100)compare=std::min(compare,original.lufs-original.tp-2);
        for(auto &m:rendered)compare=std::min(compare,m.lufs-m.tp-2);
        double referenceCompare=std::min(compare,rm.lufs-rm.tp-2);
        progress(0.96,"Monosummen für kontrolliertes Abhören");
        monoCopy(out/"Original.wav",out/"Mono-Original.wav");
        for(char label='A';label<='C';++label) {
            auto name=std::string("Master-")+label+".wav";
            monoCopy(out/name,out/("Mono-"+name));
        }
        std::ostringstream report;report<<std::setprecision(10);
        report<<"{\"schema\":1,\"version\":\"0.1.0-experimental\",\"sampleRate\":48000,\"exportBits\":24,\"comparisonLUFS\":"<<compare
            <<",\"referenceComparisonLUFS\":"<<referenceCompare<<",\"vocalGainDB\":"<<vocalDB<<",\"beatPresenceDB\":"<<masking
            <<",\"sources\":[";
        std::array<fs::path,3> paths{vocal,beat,ref};std::array<SourceInfo,3> infos{vi.info,bi.info,ri.info};std::array<Metrics,3> ms{vm,bm,rm};
        for(int i=0;i<3;++i) {if(i)report<<',';report<<"{\"path\":"<<quote(paths[i].string())<<",\"info\":"<<infoJSON(infos[i])<<",\"metrics\":"<<metricsJSON(ms[i])<<"}";}
        report<<"],\"original\":"<<metricsJSON(original)<<",\"balanced\":"<<metricsJSON(balanced)<<",\"masters\":[";
        for(int i=0;i<3;++i) {
            if(i)report<<',';
            auto p=plans[i];
            report<<"{\"file\":"<<quote(std::string("Master-")+char('A'+i)+".wav")<<",\"metrics\":"<<metricsJSON(rendered[i])
                <<",\"lowDB\":"<<p.low<<",\"presenceDB\":"<<p.presence<<",\"highDB\":"<<p.high<<",\"compressionRatio\":"<<p.compression
                <<",\"targetLUFS\":"<<p.target<<",\"driveDB\":"<<driveDB[i]<<",\"maxLimiterReductionDB\":"<<limiterDB[i]<<",\"safetyGainDB\":"<<safetyDB[i]<<"}";
        }
        report<<"],\"warnings\":[\"Experimentelle Heuristiken; keine nachgewiesene professionelle Mastering-Qualität.\","
            <<"\"LUFS-Messung noch nicht extern zertifiziert; True Peak ist eine 8x-FIR-Schätzung.\","
            <<"\"Gleicher Stem-Start wurde vom Benutzer bestätigt; keine verlässliche automatische Synchronitätsprüfung.\","
            <<"\"Frequenzmaskierung nur anhand breiter Bandenergie; keine Sprachverständlichkeitserkennung oder Resonanzdiagnose.\","
            <<"\"Ziel-Lautheit wird zugunsten von Headroom begrenzt. Varianten können sich bei geeigneten Eingängen nur gering unterscheiden.\"";
        if(std::abs(vm.frames/double(rate)-bm.frames/double(rate))>0.2)report<<",\"Stem-Längen unterscheiden sich; der kürzere Stem wurde bis zum längeren mit Stille ergänzt.\"";
        if(std::abs(vm.leading-bm.leading)>0.15)report<<",\"Erste Pegelereignisse der Stems unterscheiden sich. Das kann musikalisch beabsichtigt sein und beweist keinen Zeitversatz; Startpositionen kontrollieren.\"";
        if(balanced.correlation<0.1)report<<",\"Niedrige globale Stereokorrelation: Monosumme vor Export anhören. Keine automatische Phasenkorrektur.\"";
        if(vm.clipped||bm.clipped||rm.clipped)report<<",\"Mindestens eine Quelle enthält Samples ab 0 dBFS; bestehende Verzerrungen sind nicht repariert.\"";
        report<<"]}";
        std::ofstream json(out/"Session.json");json<<report.str();json.close();require(bool(json),"Projektbericht nicht schreibbar.");
        fs::remove(out/"vocal-48k.wav");fs::remove(out/"beat-48k.wav");
        progress(1,"Fertig: drei geprüfte PCM24-WAV-Render");
        std::cout<<"{\"report\":"<<quote((out/"Session.json").string())<<"}"<<std::endl;
        return 0;
    } catch(const std::exception &e) {std::cerr<<e.what()<<std::endl;return 1;}
}
