#include "ReferenceMix.hpp"
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
std::string sectionsJSON(const std::vector<Section>&ss){std::ostringstream j;j<<'[';for(size_t i=0;i<ss.size();++i){if(i)j<<',';auto s=ss[i];j<<"{\"start\":"<<s.start<<",\"seconds\":"<<s.seconds<<",\"rms\":"<<s.rms<<",\"crest\":"<<s.crest<<",\"stratum\":"<<s.stratum<<",\"attackProxy\":"<<s.attack<<",\"midPresenceFraction\":"<<s.midPresence<<",\"bands\":"<<arrayJSON(s.bands)<<'}';}j<<']';return j.str();}
std::string traceJSON(const std::vector<std::array<double,2>>&trace){std::ostringstream j;j<<'[';for(size_t i=0;i<trace.size();++i){if(i)j<<',';j<<arrayJSON(trace[i]);}j<<']';return j.str();}
std::string fingerprint(const fs::path&p){std::ifstream f(p,std::ios::binary);require(bool(f),"Quelle nicht lesbar.");uint64_t h=14695981039346656037ULL,n=0;std::array<char,65536>b{};while(f){f.read(b.data(),b.size());auto got=f.gcount();for(std::streamsize i=0;i<got;++i){h^=uint8_t(b[size_t(i)]);h*=1099511628211ULL;}n+=uint64_t(got);}require(f.eof(),"Quelle nicht vollstaendig lesbar.");return std::to_string(n)+":"+std::to_string(h);}
void stemMix(const fs::path&vocal,const fs::path&beat,const fs::path&reference,const fs::path&out,double strength,const std::string&flags){
 require(!fs::equivalent(vocal,beat)&&!fs::equivalent(vocal,reference)&&!fs::equivalent(beat,reference),"Dateien muessen unterschiedliche Rollen besitzen.");
 for(char c:flags)require(std::string("vdbult-").find(c)!=std::string::npos,"Unbekannte Bearbeitungsentscheidung.");
 Decoder vi(vocal),bi(beat),ri(reference);double seconds=std::max(vi.info.seconds,bi.info.seconds);require(seconds<=1800,"Maximal 30 Minuten.");newFolder(out,uint64_t((seconds*8+ri.info.seconds)*rate*8));
 auto refFingerprint=fingerprint(reference);
 auto vm=normalize(vocal,out/"Vocal-48k.wav",0,.12),bm=normalize(beat,out/"Beat-48k.wav",.12,.12),rm=normalize(reference,out/"Reference.wav",.24,.12);
 double vgDB=std::clamp(bm.active-6-vm.active,-3.,3.);mix(out/"Vocal-48k.wav",out/"Beat-48k.wav",out/"Original.wav",1,0);mix(out/"Vocal-48k.wav",out/"Beat-48k.wav",out/"Balance.wav",gain(vgDB),0);
 auto original=measure(out/"Original.wav"),balanced=measure(out/"Balance.wav");auto ownSections=sections(out/"Balance.wav"),refSections=sections(out/"Reference.wav");auto matched=matchSections(ownSections,refSections,balanced,rm);auto vp=vm;vp.active+=vgDB;
 auto plan=mixPlan(vp,bm,balanced,rm,matched,flags);double amount=plan.enabled('u')?strength:0;
 progress(.44,"Referenz vor dem Mix analysiert; begrenzte Stem-Bearbeitung und konfliktgesteuertes Unmasking");
 auto nonzero=[](const auto &a){return std::any_of(a.begin(),a.end(),[](double x){return x!=0;});};
 bool processing=amount>0||(plan.enabled('v')&&(plan.gainDB!=0||nonzero(plan.vocalEQ)))||(plan.enabled('b')&&nonzero(plan.beatEQ))||(plan.enabled('d')&&plan.ratio>1)||(plan.enabled('l')&&plan.bassDynamicDB>0)||(plan.enabled('t')&&plan.transientDB>0);
 WavReader bypass(out/"Balance.wav");
 WavReader v(out/"Vocal-48k.wav"),b(out/"Beat-48k.wav");WavWriter glue(out/"MixGlue.wav");StemProcessor3 stem(plan);Unmasker dsp(vm.active+vgDB+(plan.enabled('v')?plan.gainDB:0),amount,plan.centers);
 std::array<Frame,4096>va{},ba{},mixed{};size_t nv,nb;uint64_t frame=0;std::vector<std::array<double,4>> control;
 do{nv=v.read(va.data(),va.size());nb=b.read(ba.data(),ba.size());size_t n=std::max(nv,nb);for(size_t i=0;i<n;++i){auto vocalFrame=(i<nv?va[i]:Frame{})*gain(vgDB),beatFrame=i<nb?ba[i]:Frame{};auto pair=stem.tick(vocalFrame,beatFrame);mixed[i]=pair.first+dsp.tick(pair.first,pair.second);if(frame%4800==0)control.push_back({frame/double(rate),stem.control.vocalGR,stem.control.transient,stem.control.bassGR});++frame;}if(!processing)require(bypass.read(mixed.data(),n)==n,"Bypass-Laenge ungueltig.");glue.write(mixed.data(),n);}while(nv||nb);
 glue.close();dsp.finish();auto glued=measure(out/"MixGlue.wav");require(glued.frames==std::max(vm.frames,bm.frames)&&glued.lufs> -100,"Mix-Laenge oder Stille ungueltig.");
 std::vector<Metrics> ms{original,balanced,glued,rm};std::ostringstream j;j<<std::setprecision(10)<<"{\"schema\":3,\"version\":\"3.0.0-experimental\",\"vocalGainDB\":"<<vgDB<<",\"strength\":"<<strength<<",\"sourcePaths\":["<<quote(vocal.string())<<','<<quote(beat.string())<<','<<quote(reference.string())<<"],\"sourceInfos\":["<<infoJSON(vi.info)<<','<<infoJSON(bi.info)<<','<<infoJSON(ri.info)<<"],\"sourceMetrics\":["<<metricsJSON(vm)<<','<<metricsJSON(bm)<<','<<metricsJSON(rm)<<"],"<<playbackJSON({"Original.wav","Balance.wav","MixGlue.wav","Reference.wav"},ms);
 j<<",\"referenceMix\":{\"method\":"<<quote(matched.method)<<",\"confidence\":"<<matched.confidence<<",\"pairedWindows\":"<<matched.pairs<<",\"bandDeltaDB\":"<<arrayJSON(matched.delta)<<",\"crestDeltaDB\":"<<matched.crestDelta<<",\"attackProxyDelta\":"<<matched.attackDelta<<",\"ownSections\":"<<sectionsJSON(ownSections)<<",\"referenceSections\":"<<sectionsJSON(refSections)<<",\"vocalEQ\":"<<arrayJSON(plan.vocalEQ)<<",\"beatEQ\":"<<arrayJSON(plan.beatEQ)<<",\"vocalExtraGainDB\":"<<plan.gainDB<<",\"vocalCompressorMaxDB\":"<<stem.maxVocalGR<<",\"transientMaxDB\":"<<stem.maxTransient<<",\"bassDynamicMaxDB\":"<<stem.maxBassGR<<",\"controlTrace\":[";
 for(size_t i=0;i<control.size();++i){if(i)j<<',';j<<arrayJSON(control[i]);}j<<"],\"recommendations\":[";
 auto recommendation=[&](char id,const std::string&name,const std::string&evidence,const std::string&action,double magnitude,bool first){if(!first)j<<',';j<<"{\"id\":"<<quote(std::string(1,id))<<",\"name\":"<<quote(name)<<",\"evidence\":"<<quote(evidence)<<",\"action\":"<<quote(action)<<",\"magnitudeDB\":"<<magnitude<<",\"enabled\":"<<(plan.enabled(id)?"true":"false")<<'}';};
 auto number=[](double x){std::ostringstream o;o<<std::fixed<<std::setprecision(2)<<x;return o.str();};
 recommendation('v',"Vocal-Tonalitaet und Vordergrund", "Gemischte Referenz: Praesenzluecke "+number((matched.delta[7]+matched.delta[8]+matched.delta[9])/3)+" dB. Keine isolierte Referenz-Vocal; Zuordnung ueber bekannte eigene Stem-Energien.","Vocal EQ 180/2800/9000 Hz "+number(plan.vocalEQ[0])+" / "+number(plan.vocalEQ[1])+" / "+number(plan.vocalEQ[2])+" dB; Zusatzgain "+number(plan.gainDB)+" dB",plan.gainDB,true);
 recommendation('d',"Vocal-Dichte", "Eigene Vocal Crest "+number(vm.crest)+" dB, gesamte Referenz "+number(rm.crest)+" dB; nur vorsichtige Heuristik.","RMS-Kompressor Ratio "+number(plan.ratio)+", maximal 2 dB; tatsaechliches Maximum "+number(stem.maxVocalGR),stem.maxVocalGR,false);
 recommendation('b',"Stereo-Beat-Frequenzbalance",matched.method,"Beat EQ 85/350/2700/8500 Hz "+number(plan.beatEQ[0])+" / "+number(plan.beatEQ[1])+" / "+number(plan.beatEQ[2])+" / "+number(plan.beatEQ[3])+" dB; betrifft mehrere Instrumente gemeinsam",*std::max_element(plan.beatEQ.begin(),plan.beatEQ.end(),[](double a,double b){return std::abs(a)<std::abs(b);}),false);
 recommendation('l',"Dynamische Tiefbasskontrolle","Subbass-Energieabstand "+number(matched.delta[0])+" dB; eigener Beat-Anteil "+number(bm.bands[0]*100)+" %","Dynamischer Low-Shelf 100 Hz, Limit "+number(plan.bassDynamicDB)+" dB; keine isolierte Kick/Bass-Trennung",stem.maxBassGR,false);
 recommendation('t',"Stereo-Beat-Attack","Abschnitts-Onsetproxy-Abstand "+number(matched.attackDelta)+"; keine Kick-/Snare-Erkennung","Kurzzeitiger stereo-gekoppelter Attack-Gain bis "+number(plan.transientDB)+" dB; bei unklarer Zuordnung bypass",stem.maxTransient,false);
 recommendation('u',"Zeitabhaengige Vocal-Beat-Konflikte","Drei ausgewaehlte Konfliktzentren aus sieben Kandidaten; Aktivitaet und Energieueberlappung steuern die Eingriffe","Maximal 2.5 dB je Zone mal Staerke; Ruecknahme bei Vocal-Pausen",*std::max_element(dsp.stats.maximum.begin(),dsp.stats.maximum.end()),false);
 j<<"]},\"unmask\":{\"centers\":"<<arrayJSON(plan.centers)<<",\"maximum\":"<<arrayJSON(dsp.stats.maximum)<<",\"mean\":"<<arrayJSON(dsp.stats.mean)<<",\"activeFraction\":"<<dsp.stats.activeFrames/double(std::max<uint64_t>(1,dsp.stats.frames))<<",\"trace\":[";
 for(size_t i=0;i<dsp.stats.trace.size();++i){if(i)j<<',';auto p=dsp.stats.trace[i];j<<'['<<p.time<<','<<p.reduction[0]<<','<<p.reduction[1]<<','<<p.reduction[2]<<']';}
 j<<"]},\"warnings\":["<<quote(matched.method)<<",\"Zeitgleichheit erfordert gemeinsamen Exportstart. Unterschiedliche Laengen werden mit Stille ergaenzt. Keine automatische Reparatur.\",\"Referenz-Stimme, Kick und Snare werden NICHT isoliert. Regeln sind experimentell; Hoertest erforderlich.\"";
 if(vm.clipped||bm.clipped)j<<",\"Quelle clippt; bestehende Verzerrung wird nicht repariert.\"";
 j<<"]}";require(fingerprint(reference)==refFingerprint,"Referenz waehrend Mix-Analyse geaendert; bitte erneut berechnen.");save(out/"Mix.json",j.str());save(out/"Reference.identity",reference.string()+"\n"+refFingerprint);progress(1,"Reference Mix fertig: Entscheidungen pruefen, anhoeren und Mix bestaetigen");
}
struct Rendered {Metrics m;MasterPlan2 plan;LimitStats limiting;double drive=0,safety=0,compressor=0;std::string reason;std::vector<std::array<double,2>> compressorTrace;};
Rendered masterOne(const fs::path&input,const fs::path&out,const Metrics&own,const Metrics&ref,int profile){
 Rendered result;result.plan=plan2(own,ref,profile);auto p=result.plan;
 auto eqFile=out/(std::string(1,char('A'+profile))+"-EQ.wav"),limited=out/(std::string(1,char('A'+profile))+"-Limited.wav");
 WavReader r(input);WavWriter w(eqFile);MasterProcessor dsp(p);std::array<Frame,4096> buffer{};size_t n;
 uint64_t position=0;double windowMax=0;while((n=r.read(buffer.data(),buffer.size()))){for(size_t i=0;i<n;++i){buffer[i]=dsp.tick(buffer[i]);windowMax=std::max(windowMax,dsp.currentReduction);if(++position%4800==0){result.compressorTrace.push_back({position/double(rate),windowMax});windowMax=0;}}w.write(buffer.data(),n);}if(position%4800)result.compressorTrace.push_back({position/double(rate),windowMax});w.close();result.compressor=dsp.maximumReduction;
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
 if(result.m.lufs<p.target-.5)result.reason=constrained?"Ziel verfehlt: anhaltende Limiter-Reduktion begrenzt; musikalische Qualitaet muss gehoert werden.":"Ziel verfehlt: Peak-/Gain-Reduction-Budget begrenzt: benoetigter Restgain wird nach Messung ausgegeben; keine Lautheit um jeden Preis. Keine Garantie fuer Unhoerbarkeit.";
 if(result.m.lufs<p.target-.5)result.reason+=" Restabstand "+std::to_string(p.target-result.m.lufs)+" LU; Antrieb "+std::to_string(result.drive)+" dB; MaxGR "+std::to_string(result.limiting.maximum)+" dB; Anteil >3 dB "+std::to_string(result.limiting.busy()*100)+" Prozent; Sicherheitsabsenkung "+std::to_string(result.safety)+" dB.";
 else result.reason="Referenz-Lautheitsziel innerhalb 0.5 LU erreicht; Klangqualitaet noch im Hoertest pruefen.";
 if(result.m.correlation<.1)result.reason+=" Niedrige globale Stereokorrelation: Monosumme kontrollieren; keine automatische Phasenreparatur.";
 fs::remove(eqFile);fs::remove(limited);return result;
}
void mastering(const fs::path&project,const fs::path&reference,const std::string&selected,const fs::path&out){
 require(selected=="Original"||selected=="Balance"||selected=="MixGlue","Ungueltige Mix-Auswahl.");
 require(fs::exists(project/"Mix.json"),"Phase 1 muss zuerst erfolgreich abgeschlossen sein.");
 std::ifstream identity(project/"Reference.identity");std::string stored((std::istreambuf_iterator<char>(identity)),{});require(stored==reference.string()+"\n"+fingerprint(reference),"Referenz seit Mix-Analyse geaendert: Reference Mix erneut berechnen.");
 auto input=project/(selected+".wav");auto own=measure(input);Decoder ri(reference);
 newFolder(out,uint64_t(own.frames*55+ri.info.seconds*rate*8));auto rm=normalize(reference,out/"Reference.wav",0,.15);
 require(stored==reference.string()+"\n"+fingerprint(reference),"Referenz waehrend Master-Analyse geaendert.");
 auto sm=matchSections(sections(input),sections(out/"Reference.wav"),own,rm);auto guide=rm;
 if(sm.confidence>=.5){double sum=0;for(int k=0;k<13;++k){guide.bands[k]=own.bands[k]*std::pow(10.,sm.delta[k]/10);sum+=guide.bands[k];}for(auto &x:guide.bands)x/=std::max(sum,1e-30);guide.crest=own.crest+sm.crestDelta;}
 std::array<Rendered,3> render;for(int i=0;i<3;++i){render[i]=masterOne(input,out,own,guide,i);render[i].plan.reason=sm.method+" Bounded eight-zone EQ, crest-based compression, no extra saturation/clipping without proof.";}
 std::vector<std::string> files;std::vector<Metrics> ms;
 for(const char *name:{"Original.wav","Balance.wav","MixGlue.wav"}){files.push_back((project/name).string());ms.push_back(measure(project/name));}
 for(int i=0;i<3;++i){files.push_back((out/(std::string("Master-")+char('A'+i)+".wav")).string());ms.push_back(render[i].m);}
 files.push_back((out/"Reference.wav").string());ms.push_back(rm);
 std::ostringstream j;j<<std::setprecision(10)<<"{\"schema\":3,\"selectedMix\":"<<quote(selected)<<",\"referencePath\":"<<quote(reference.string())<<",\"referenceInfo\":"<<infoJSON(ri.info)<<','<<playbackJSON(files,ms)<<",\"masters\":[";
 for(int i=0;i<3;++i){if(i)j<<',';auto x=render[i];j<<"{\"file\":"<<quote(std::string("Master-")+char('A'+i)+".wav")<<",\"metrics\":"<<metricsJSON(x.m)<<",\"eqDB\":"<<arrayJSON(x.plan.eq)<<",\"sideEQ\":"<<arrayJSON(x.plan.sideEQ)<<",\"ratio\":"<<x.plan.ratio<<",\"threshold\":"<<x.plan.threshold<<",\"compressorMaxDB\":"<<x.compressor<<",\"targetLUFS\":"<<x.plan.target<<",\"driveDB\":"<<x.drive<<",\"safetyDB\":"<<x.safety<<",\"limiterMaxDB\":"<<x.limiting.maximum<<",\"limiterMeanDB\":"<<x.limiting.mean()<<",\"limiterBusyFraction\":"<<x.limiting.busy()<<",\"reason\":"<<quote(x.reason)<<",\"decision\":"<<quote(x.plan.reason)<<",\"compressorTrace\":"<<traceJSON(x.compressorTrace)<<",\"limiterTrace\":"<<traceJSON(x.limiting.trace)<<"}";}
 j<<"],\"similarities\":[";bool first=true;
 for(int a=0;a<3;++a)for(int b=a+1;b<3;++b){WavReader ar(out/(std::string("Master-")+char('A'+a)+".wav")),br(out/(std::string("Master-")+char('A'+b)+".wav"));std::array<Frame,4096> ab{},bb{};double energy=0,difference=0;size_t count;double g=gain(render[a].m.lufs-render[b].m.lufs);
  while((count=ar.read(ab.data(),ab.size()))){require(br.read(bb.data(),count)==count,"Similarity length mismatch");for(size_t i=0;i<count;++i){energy+=ab[i].l*ab[i].l+ab[i].r*ab[i].r;difference+=std::pow(ab[i].l-bb[i].l*g,2)+std::pow(ab[i].r-bb[i].r*g,2);}}
  if(!first)j<<',';
  first=false;double residual=10*std::log10(std::max(difference,1e-30)/std::max(energy,1e-30));j<<"{\"a\":"<<a<<",\"b\":"<<b<<",\"residualDB\":"<<residual<<",\"nearIdentical\":"<<(residual< -35?"true":"false")<<"}";
 }
 j<<"],\"warnings\":[\"Lautheitsziel ist die gemessene Referenz, keine feste -12-LUFS-Grenze. Begrenzte Budgets ersetzen keinen Hoertest.\",\"Energieklassen sind keine semantische Refrainerkennung; isolierte Referenzinstrumente nicht messbar. Seiten-EQ begrenzt.\",\"8x-r8brain-Peak-Rekonstruktion mit finalem PCM24-Test, keine allgemeine Zertifizierung.\"]}";
 save(out/"Master.json",j.str());progress(1,"Phase 2 fertig: drei Master exportiert und technisch geprueft");
}
int main(int argc,char**argv){try{
 if(argc==3&&std::string(argv[1])=="--inspect"){Decoder d(argv[2]);std::cout<<infoJSON(d.info)<<std::endl;return 0;}
 if(argc==3&&std::string(argv[1])=="--measure"){std::cout<<metricsJSON(measure(argv[2]))<<std::endl;return 0;}
 if(argc==8&&std::string(argv[1])=="--mix"){double strength=std::stod(argv[6]);require(std::isfinite(strength)&&strength>=0&&strength<=1,"Unmasking-Staerke muss 0..1 sein.");stemMix(fs::canonical(argv[2]),fs::canonical(argv[3]),fs::canonical(argv[4]),fs::absolute(argv[5]),strength,argv[7]);return 0;}
 if(argc==6&&std::string(argv[1])=="--master"){mastering(fs::canonical(argv[2]),fs::canonical(argv[3]),argv[4],fs::absolute(argv[5]));return 0;}
 throw std::runtime_error("V3: --mix VOCAL BEAT REFERENZ NEUER_ORDNER STAERKE FLAGS(vdbult) oder --master PROJEKT REFERENZ MIX NEUER_ORDNER");
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
