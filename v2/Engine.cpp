#include "Processing.hpp"
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
     <<",\"spectralWindows\":"<<m.spectralWindows<<",\"bands\":[";for(size_t i=0;i<m.bands.size();++i){if(i)s<<',';s<<m.bands[i];}s<<"],\"sideBands\":[";for(size_t i=0;i<m.sideBands.size();++i){if(i)s<<',';s<<m.sideBands[i];}s<<"],\"leadingSilenceSeconds\":"<<m.leading<<",\"clippedFrames\":"<<m.clipped<<"}";return s.str();
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
void save(const fs::path&p,const std::string&data){std::ofstream f(p);f<<data;f.close();require(bool(f),"Projektbericht konnte nicht gespeichert werden.");}
template<class T,size_t N> std::string arrayJSON(const std::array<T,N>&v){std::ostringstream s;s<<'[';for(size_t i=0;i<N;++i){if(i)s<<',';s<<v[i];}s<<']';return s.str();}
void newFolder(const fs::path&p,uint64_t needed){require(!fs::exists(p),"Projekt existiert bereits; kein Überschreiben.");require(fs::space(p.parent_path()).available>needed+512*1024*1024,"Zu wenig freier Speicher.");fs::create_directory(p);}
std::string playbackJSON(const std::vector<std::string>&files,const std::vector<Metrics>&m){double matched=-18,trim=0;for(auto x:m){if(x.lufs>-100)matched=std::min(matched,x.lufs-x.tp-2);trim=std::min(trim,-1.5-x.tp);}std::ostringstream s;
 s<<"\"comparisonLUFS\":"<<matched<<",\"originalMonitorTrimDB\":"<<trim<<",\"files\":[";
 for(size_t i=0;i<files.size();++i){if(i)s<<',';s<<"{\"file\":"<<quote(files[i])<<",\"metrics\":"<<metricsJSON(m[i])<<",\"matchedGainDB\":"<<(m[i].lufs>-100?matched-m[i].lufs:-120)<<"}";}s<<']';return s.str();}
void stemMix(const fs::path&vocal,const fs::path&beat,const fs::path&out,double strength){
 require(!fs::equivalent(vocal,beat),"Vocal und Beat dürfen nicht dieselbe Datei sein.");Decoder vInfo(vocal),bInfo(beat);double seconds=std::max(vInfo.info.seconds,bInfo.info.seconds);
 require(seconds<=1800,"Maximal 30 Minuten pro Datei.");newFolder(out,uint64_t(seconds*rate*8*6));
 auto vm=normalize(vocal,out/"Vocal-48k.wav",0,.20),bm=normalize(beat,out/"Beat-48k.wav",.20,.20);
 double vgDB=std::clamp(bm.active-6-vm.active,-3.,3.);double vg=gain(vgDB);
 progress(.42,"Phase 1: Original, Pegelbalance und dynamisches Unmasking");
 WavReader v(out/"Vocal-48k.wav"),b(out/"Beat-48k.wav");
 WavWriter original(out/"Original.wav"),balance(out/"Balance.wav"),glue(out/"MixGlue.wav");
 Unmasker dsp(vm.active+vgDB,strength);std::array<Frame,4096>va{},ba{},orig{},bal{},mixout{};size_t nv,nb;
 do{nv=v.read(va.data(),va.size());nb=b.read(ba.data(),ba.size());size_t n=std::max(nv,nb);
  for(size_t i=0;i<n;++i){Frame vocalFrame=i<nv?va[i]:Frame{},beatFrame=i<nb?ba[i]:Frame{};orig[i]=vocalFrame+beatFrame;vocalFrame=vocalFrame*vg;bal[i]=vocalFrame+beatFrame;mixout[i]=vocalFrame+dsp.tick(vocalFrame,beatFrame);}
  original.write(orig.data(),n);balance.write(bal.data(),n);glue.write(mixout.data(),n);
 }while(nv||nb);
 original.close();balance.close();glue.close();dsp.finish();
 std::vector<Metrics> m;for(const char *file:{"Original.wav","Balance.wav","MixGlue.wav"})m.push_back(measure(out/file));
 require(m[0].frames==std::max(vm.frames,bm.frames)&&m[1].frames==m[0].frames&&m[2].frames==m[0].frames,"Mix-Längenprüfung fehlgeschlagen.");
 require(m[2].lufs>-100,"Mix ist still oder löscht sich aus.");
 std::ostringstream j;j<<std::setprecision(10)<<"{\"schema\":2,\"version\":\"2.0.0-experimental\",\"vocalGainDB\":"<<vgDB<<",\"strength\":"<<strength<<",\"sourcePaths\":["<<quote(vocal.string())<<','<<quote(beat.string())<<"],\"sourceInfos\":["<<infoJSON(vInfo.info)<<','<<infoJSON(bInfo.info)<<"],\"sourceMetrics\":["<<metricsJSON(vm)<<','<<metricsJSON(bm)<<"],"<<playbackJSON({"Original.wav","Balance.wav","MixGlue.wav"},m)
 <<",\"unmask\":{\"centers\":[800,2000,4000],\"maximum\":"<<arrayJSON(dsp.stats.maximum)<<",\"mean\":"<<arrayJSON(dsp.stats.mean)<<",\"activeFraction\":"<<dsp.stats.activeFrames/double(std::max<uint64_t>(1,dsp.stats.frames))<<",\"trace\":[";
 for(size_t i=0;i<dsp.stats.trace.size();++i){if(i)j<<',';auto point=dsp.stats.trace[i];j<<"["<<point.time<<','<<point.reduction[0]<<','<<point.reduction[1]<<','<<point.reduction[2]<<']';}
 j<<"]},\"warnings\":[\"Zeitgleichheit basiert auf gemeinsamem Exportstart, nicht auf einer automatischen Reparatur.\",\"Die drei Reglerkurven dokumentieren die EQ-Steuerhuellkurven; keine erfundene Spracherkennung.\"";
 if(vm.frames!=bm.frames)j<<",\"Unterschiedliche Stem-Laengen: kuerzerer Stem wird mit Stille ergaenzt; Ende kontrollieren.\"";
 if(vm.clipped||bm.clipped)j<<",\"Quelle erreicht/ueberschreitet 0 dBFS. Bestehende Verzerrungen werden nicht repariert.\"";
 if(*std::max_element(dsp.stats.maximum.begin(),dsp.stats.maximum.end())<.05)j<<",\"Keine relevante Maskierung nach den begrenzten Bandkriterien erkannt; praktisch kein Eingriff.\"";
 j<<"]}";save(out/"Mix.json",j.str());progress(1,"Phase 1 fertig: Mix vergleichen und bestaetigen");
}
struct Rendered {Metrics m;MasterPlan2 plan;LimitStats limiting;double drive=0,safety=0,compressor=0;std::string reason;};
Rendered masterOne(const fs::path&input,const fs::path&out,const Metrics&own,const Metrics&ref,int profile){
 Rendered result;result.plan=plan2(own,ref,profile);auto p=result.plan;
 auto eqFile=out/(std::string(1,char('A'+profile))+"-EQ.wav"),limited=out/(std::string(1,char('A'+profile))+"-Limited.wav");
 WavReader r(input);WavWriter w(eqFile);MasterProcessor dsp(p);std::array<Frame,4096> buffer{};size_t n;
 while((n=r.read(buffer.data(),buffer.size()))){for(size_t i=0;i<n;++i)buffer[i]=dsp.tick(buffer[i]);w.write(buffer.data(),n);}w.close();result.compressor=dsp.maximumReduction;
 auto eq=measure(eqFile);double maxDrive=std::min(24.,-1.3-eq.tp+p.budget);double drive=std::min(p.target-eq.lufs,maxDrive);
 bool constrained=false;
 for(int attempt=0;attempt<3;++attempt){
  progress(.22+profile*.23+attempt*.025,"Phase 2: Master "+std::string(1,char('A'+profile))+" · 8x-Limiting und Lautheitspruefung");
  auto ls=oversampledLimit(eqFile,limited,gain(drive));auto lm=measure(limited);double safety=std::min(0.,-1.3-lm.tp);
  double achieved=lm.lufs+safety;
  if((ls.mean()>2.5||ls.busy()>.35)&&attempt<2){drive-=1.5;constrained=true;continue;}
  result.limiting=ls;result.drive=drive;result.safety=safety;
  double add=p.target-achieved;
  if(add>.3&&drive<maxDrive-.05&&!constrained&&attempt<2){drive=std::min(maxDrive,drive+add);continue;}
  break;
 }
 auto file=out/(std::string("Master-")+char('A'+profile)+".wav");scaleFile(limited,file,gain(result.safety),24);result.m=measure(file);
 require(result.m.frames==own.frames&&result.m.clipped==0&&result.m.tp<=-1.15,"Finale PCM24-/Peak-Pruefung fehlgeschlagen.");
 if(result.m.lufs<p.target-.5)result.reason=constrained?"Ziel verfehlt: anhaltende Limiter-Reduktion begrenzt; musikalische Qualitaet muss gehoert werden.":"Ziel verfehlt: Peak-/Gain-Reduction-Budget oder Sicherheitsabsenkung begrenzt. Keine Garantie fuer Unhoerbarkeit.";
 else result.reason="Referenz-Lautheitsziel innerhalb 0.5 LU erreicht; Klangqualitaet noch im Hoertest pruefen.";
 if(result.m.correlation<.1)result.reason+=" Niedrige globale Stereokorrelation: Monosumme kontrollieren; keine automatische Phasenreparatur.";
 fs::remove(eqFile);fs::remove(limited);return result;
}
void mastering(const fs::path&project,const fs::path&reference,const std::string&selected,const fs::path&out){
 require(selected=="Original"||selected=="Balance"||selected=="MixGlue","Ungueltige Mix-Auswahl.");
 require(fs::exists(project/"Mix.json"),"Phase 1 muss zuerst erfolgreich abgeschlossen sein.");
 auto input=project/(selected+".wav");auto own=measure(input);Decoder ri(reference);
 newFolder(out,uint64_t(own.frames*55+ri.info.seconds*rate*8));auto rm=normalize(reference,out/"Reference.wav",0,.15);
 std::array<Rendered,3> render;for(int i=0;i<3;++i)render[i]=masterOne(input,out,own,rm,i);
 std::vector<std::string> files;std::vector<Metrics> ms;
 for(const char *name:{"Original.wav","Balance.wav","MixGlue.wav"}){files.push_back((project/name).string());ms.push_back(measure(project/name));}
 for(int i=0;i<3;++i){files.push_back((out/(std::string("Master-")+char('A'+i)+".wav")).string());ms.push_back(render[i].m);}
 files.push_back((out/"Reference.wav").string());ms.push_back(rm);
 std::ostringstream j;j<<std::setprecision(10)<<"{\"schema\":2,\"selectedMix\":"<<quote(selected)<<",\"referencePath\":"<<quote(reference.string())<<",\"referenceInfo\":"<<infoJSON(ri.info)<<','<<playbackJSON(files,ms)<<",\"masters\":[";
 for(int i=0;i<3;++i){if(i)j<<',';auto x=render[i];j<<"{\"file\":"<<quote(std::string("Master-")+char('A'+i)+".wav")<<",\"metrics\":"<<metricsJSON(x.m)<<",\"eqDB\":"<<arrayJSON(x.plan.eq)<<",\"sideEQ\":"<<arrayJSON(x.plan.sideEQ)<<",\"ratio\":"<<x.plan.ratio<<",\"threshold\":"<<x.plan.threshold<<",\"compressorMaxDB\":"<<x.compressor<<",\"targetLUFS\":"<<x.plan.target<<",\"driveDB\":"<<x.drive<<",\"safetyDB\":"<<x.safety<<",\"limiterMaxDB\":"<<x.limiting.maximum<<",\"limiterMeanDB\":"<<x.limiting.mean()<<",\"limiterBusyFraction\":"<<x.limiting.busy()<<",\"reason\":"<<quote(x.reason)<<",\"decision\":"<<quote(x.plan.reason)<<"}";}
 j<<"],\"similarities\":[";bool first=true;
 for(int a=0;a<3;++a)for(int b=a+1;b<3;++b){WavReader ar(out/(std::string("Master-")+char('A'+a)+".wav")),br(out/(std::string("Master-")+char('A'+b)+".wav"));std::array<Frame,4096> ab{},bb{};double energy=0,difference=0;size_t count;double g=gain(render[a].m.lufs-render[b].m.lufs);
  while((count=ar.read(ab.data(),ab.size()))){require(br.read(bb.data(),count)==count,"Similarity length mismatch");for(size_t i=0;i<count;++i){energy+=ab[i].l*ab[i].l+ab[i].r*ab[i].r;difference+=std::pow(ab[i].l-bb[i].l*g,2)+std::pow(ab[i].r-bb[i].r*g,2);}}
  if(!first)j<<',';
  first=false;double residual=10*std::log10(std::max(difference,1e-30)/std::max(energy,1e-30));j<<"{\"a\":"<<a<<",\"b\":"<<b<<",\"residualDB\":"<<residual<<",\"nearIdentical\":"<<(residual< -35?"true":"false")<<"}";
 }
 j<<"],\"warnings\":[\"Lautheitsziel ist die gemessene Referenz, keine feste -12-LUFS-Grenze. Begrenzte Budgets ersetzen keinen Hoertest.\",\"13 FFT-Baender aus aktiven Fenstern, keine semantische Abschnittserkennung; Seiten-EQ nur begrenzt und ohne neue Stereo-Inhalte.\",\"8x-r8brain-Peak-Rekonstruktion mit finalem PCM24-Test, keine allgemeine Zertifizierung.\"]}";
 save(out/"Master.json",j.str());progress(1,"Phase 2 fertig: drei Master exportiert und technisch geprueft");
}
int main(int argc,char**argv){try{
 if(argc==3&&std::string(argv[1])=="--inspect"){Decoder d(argv[2]);std::cout<<infoJSON(d.info)<<std::endl;return 0;}
 if(argc==3&&std::string(argv[1])=="--measure"){std::cout<<metricsJSON(measure(argv[2]))<<std::endl;return 0;}
 if(argc==6&&std::string(argv[1])=="--mix"){double strength=std::stod(argv[5]);require(std::isfinite(strength)&&strength>=0&&strength<=1,"Unmasking-Staerke muss 0..1 sein.");stemMix(fs::canonical(argv[2]),fs::canonical(argv[3]),fs::absolute(argv[4]),strength);return 0;}
 if(argc==6&&std::string(argv[1])=="--master"){mastering(fs::canonical(argv[2]),fs::canonical(argv[3]),argv[4],fs::absolute(argv[5]));return 0;}
 throw std::runtime_error("V2: --mix VOCAL BEAT NEUER_ORDNER STAERKE oder --master PROJEKT REFERENZ MIX NEUER_ORDNER");
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
