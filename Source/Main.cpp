// Yda Facet 01 — native shell
// The app window holds the Web screen (facet.html) in the system web view.
// 0.1.0  window + Web screen + native Save dialog for exports
// 0.2.0  the Web screen updates itself from the YDA site (no rebuild needed)
// 0.3.0  MIDI (Web MIDI for the screen, through the Mac's MIDI), alert / confirm / prompt as Mac dialogs,
//        new app versions are fetched from GitHub into the "native" folder and announced with the same bar,
//        app icon, and recovery: the song is copied to disk every 30 s; after an unexpected end the app offers it back
// Sound still runs inside the web view for now; later steps move it into a native engine.

#include <JuceHeader.h>

#if JUCE_MAC
bool facetInstallJsDialogs (void* peerView, bool autoOK);   // Source/MacBits.mm
#endif

namespace facet
{
//==============================================================================
// Where things live
static juce::File bundledPage()   // the copy inside the .app (macOS) or next to the program (elsewhere)
{
    auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
   #if JUCE_MAC
    auto f = exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/facet.html");
    if (f.existsAsFile()) return f;
   #endif
    return exe.getParentDirectory().getChildFile ("facet.html");
}

static juce::File supportDir()
{
   #if JUCE_MAC
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)   // ~/Library on macOS
               .getChildFile ("Application Support/Yda Facet");
   #else
    auto d = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("Yda Facet");
   #endif
    d.createDirectory();
    return d;
}

// The page is always opened from this one place, so saved songs and settings (browser storage) stay put across updates.
static juce::File livePage() { return supportDir().getChildFile ("web/facet.html"); }

//==============================================================================
// Versions: "2.77.0" style, compared number by number
static juce::String pageVersion (const juce::String& html)
{
    // <meta name="facet-version" content="2.77.0"> (written by the build and by the updater); older files: APP_VER='2.77.0'
    auto m = html.indexOf ("name=\"facet-version\" content=\"");
    if (m >= 0 && m < 4000) return html.substring (m + 30, html.indexOfChar (m + 30, '"')).trim();
    auto i = html.indexOf ("APP_VER='");
    if (i < 0) return {};
    return html.substring (i + 9, html.indexOfChar (i + 9, '\'')).trim();
}

static int compareVersions (const juce::String& a, const juce::String& b)
{
    juce::StringArray x, y;
    x.addTokens (a, ".", {});
    y.addTokens (b, ".", {});
    for (int i = 0; i < juce::jmax (x.size(), y.size()); ++i)
    {
        auto p = x[i].getIntValue(), q = y[i].getIntValue();
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

static bool writeAtomically (const juce::File& f, const juce::String& text)
{
    f.getParentDirectory().createDirectory();
    juce::TemporaryFile tmp (f);
    if (! tmp.getFile().replaceWithText (text, false, false, "\n")) return false;
    return tmp.overwriteTargetFileWithTemporary();
}

// Make sure livePage() holds the newest of: what came with the app, what was downloaded before.
static juce::String preparePage()
{
    auto live = livePage();
    auto liveVer = live.existsAsFile() ? pageVersion (live.loadFileAsString()) : juce::String();
    auto bundled = bundledPage();
    if (bundled.existsAsFile())
    {
        auto b = bundled.loadFileAsString();
        auto bVer = pageVersion (b);
        if (liveVer.isEmpty() || compareVersions (liveVer, bVer) < 0)
            if (writeAtomically (live, b)) liveVer = bVer;
    }
    return liveVer;
}

//==============================================================================
// App update (0.3.0): GitHub builds the app every time something changes (Releases → latest).
// When a newer app is out, it is downloaded into the "native" folder (Desktop/Yda facet/native, else Downloads)
// and the screen shows a bar with a button that opens that folder.
static const char* releasesApi   = "https://api.github.com/repos/yda-facet/yda-facet-native/releases/latest";
static const char* trustedAssets = "https://github.com/yda-facet/yda-facet-native/releases/download/";

static juce::File nativeFolder()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile ("Yda facet/native");
    if (d.isDirectory()) return d;
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads");
}

//==============================================================================
// Update check. The YDA page carries a <script src> to the Web screen on the store's file CDN:
//   https://cdn.shopify.com/s/files/1/0691/7098/7296/files/yda-facet-01-2.77.0.js?v=…
// That file is   (function(){var h="<html>…";document.write(h)})()   — the screen as one JSON string.
// Only that CDN folder is trusted, and the result must look like the Facet screen before it is used.
static const char* updatePage   = "https://ydajewelry.com/pages/yda-facet?preview_theme_id=193809809696";
static const char* trustedPrefix = "https://cdn.shopify.com/s/files/1/0691/7098/7296/files/yda-facet-01-";

class Updater final : public juce::Thread
{
public:
    Updater (juce::String currentVersion, std::function<void (juce::String)> onReady)
        : Thread ("facet-update"), current (std::move (currentVersion)), ready (std::move (onReady)) {}

    ~Updater() override { stopThread (20000); }

    std::function<void (juce::String, juce::File)> appReady;

    void run() override
    {
        checkScreen();
        if (! threadShouldExit()) checkApp();
    }

    // Is there a newer app on GitHub? Download it into the native folder (once) and say so.
    void checkApp()
    {
        auto api = juce::SystemStats::getEnvironmentVariable ("FACET_APP_API", releasesApi);
        auto assets = juce::SystemStats::getEnvironmentVariable ("FACET_APP_ASSETS", trustedAssets);
        if (assets != trustedAssets && ! assets.startsWith ("http://127.0.0.1:")) assets = trustedAssets;   // tests only

        auto json = juce::JSON::parse (fetch (api, "Accept: application/vnd.github+json"));
        auto tag = json["tag_name"].toString();                                   // app-0.3.0-screen-2.78.0
        auto ver = tag.fromFirstOccurrenceOf ("app-", false, false).upToFirstOccurrenceOf ("-screen", false, false);
        if (ver.isEmpty() || ! ver.containsOnly ("0123456789.")) { log ("app check: no release found"); return; }
        if (compareVersions (ProjectInfo::versionString, ver) >= 0) { log ("app up to date (" + ver + ")"); return; }

        juce::String url;
        if (auto* list = json["assets"].getArray())
            for (auto& a : *list)
                if (a["name"].toString() == "YdaFacet01-mac.zip") url = a["browser_download_url"].toString();
        if (! url.startsWith (assets)) { log ("app check: unexpected download " + url); return; }

        auto dest = nativeFolder().getChildFile ("v" + ver + "_YdaFacet01-mac.zip");
        if (! dest.existsAsFile())
        {
            juce::MemoryBlock mb;
            int status = 0;
            if (auto in = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                               .withConnectionTimeoutMs (30000).withStatusCode (&status)
                                                               .withExtraHeaders ("User-Agent: YdaFacet01")))
                in->readIntoMemoryBlock (mb);
            const auto* b = static_cast<const char*> (mb.getData());
            if (status >= 400 || mb.getSize() < 100000 || b[0] != 'P' || b[1] != 'K') { log ("app download failed (" + juce::String (status) + ")"); return; }
            juce::TemporaryFile tmp (dest);
            if (! tmp.getFile().replaceWithData (mb.getData(), mb.getSize()) || ! tmp.overwriteTargetFileWithTemporary())
            { log ("could not write " + dest.getFullPathName()); return; }
            log ("app " + ver + " saved to " + dest.getFullPathName());
        }
        if (appReady) appReady (ver, dest);
    }

    void checkScreen()
    {
        auto pageUrl = juce::SystemStats::getEnvironmentVariable ("FACET_UPDATE_URL", updatePage);
        auto prefix  = juce::SystemStats::getEnvironmentVariable ("FACET_UPDATE_PREFIX", trustedPrefix);
        if (prefix != trustedPrefix && ! prefix.startsWith ("http://127.0.0.1:")) prefix = trustedPrefix;   // tests only, on this machine
        log ("check " + pageUrl + " (have " + current + ")");

        auto page = fetch (pageUrl);
        if (page.isEmpty()) { log ("page not reachable"); return; }

        // find the newest  <prefix>X.Y.Z.js?v=N  on the page
        juce::String best, bestUrl;
        for (int i = page.indexOf (prefix); i >= 0; i = page.indexOf (i + 1, prefix))
        {
            auto end = i;
            while (end < page.length() && ! juce::String ("\"' <>").containsChar (page[end])) ++end;
            auto url = page.substring (i, end).replace ("&amp;", "&");
            auto ver = url.fromFirstOccurrenceOf (prefix, false, false).upToFirstOccurrenceOf (".js", false, false);
            if (ver.containsOnly ("0123456789.") && (best.isEmpty() || compareVersions (best, ver) < 0)) { best = ver; bestUrl = url; }
        }
        if (best.isEmpty()) { log ("no screen found on the page"); return; }
        if (compareVersions (current, best) >= 0) { log ("up to date (" + best + ")"); return; }
        if (threadShouldExit()) return;

        auto js = fetch (bestUrl);
        auto a = js.indexOf ("var h="), b = js.lastIndexOf (";document.write(h)");
        if (a < 0 || b <= a) { log ("unexpected file " + bestUrl); return; }

        auto arr = juce::JSON::parse ("[" + js.substring (a + 6, b) + "]");   // wrapped: the parser wants an array or object at the top
        auto parsed = arr.isArray() && arr.size() == 1 ? arr[0] : juce::var();
        auto html = parsed.toString();
        // the file names its version in its first line:  /* Yda Facet 01 v2.77.0 — … */
        auto head = js.substring (0, 200);
        if (! parsed.isString() || ! head.contains ("Yda Facet 01 v" + best + " ") || ! html.contains ("Yda Facet 01") || html.length() < 100000)
        { log ("check failed " + bestUrl + " (string " + juce::String ((int) parsed.isString()) + ", head " + juce::String ((int) head.contains ("Yda Facet 01 v" + best + " "))
                 + ", name " + juce::String ((int) html.contains ("Yda Facet 01")) + ", size " + juce::String (html.length()) + ")"); return; }

        if (pageVersion (html) != best)
        {
            auto h = html.indexOfIgnoreCase ("<head>");
            if (h < 0) { log ("no <head> in " + bestUrl); return; }
            html = html.substring (0, h + 6) + "\n<meta name=\"facet-version\" content=\"" + best + "\">" + html.substring (h + 6);
        }
        if (! html.trimStart().startsWithIgnoreCase ("<!doctype")) html = "<!doctype html>" + html;
        auto next = livePage().getSiblingFile ("facet-next.html");
        if (! writeAtomically (next, html)) { log ("could not write"); return; }
        log ("downloaded " + best);
        ready (best);
    }

    static void log (const juce::String& s)
    {
        supportDir().getChildFile ("update.txt")
            .appendText (juce::Time::getCurrentTime().toISO8601 (true) + "  " + s + "\n");
    }

private:
    static juce::String fetch (const juce::String& u, const juce::String& extraHeaders = {})
    {
        int status = 0;
        auto in = juce::URL (u).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withConnectionTimeoutMs (15000)
                                                     .withExtraHeaders ("User-Agent: YdaFacet01" + (extraHeaders.isNotEmpty() ? "\r\n" + extraHeaders : juce::String()))
                                                     .withStatusCode (&status));
        if (in == nullptr || status >= 400) return {};
        return in->readEntireStreamAsString();
    }

    juce::String current;
    std::function<void (juce::String)> ready;
};

//==============================================================================
// MIDI bridge (0.3.0). The Mac's web view has no Web MIDI, so the screen gets a stand-in (see userScript) and the
// app does the real work: lists the Mac's MIDI ports, forwards incoming messages, and sends outgoing ones on time
// (each output has its own timing thread, so MIDI clock and notes scheduled ahead leave exactly when asked).
class MidiBridge final : private juce::MidiInputCallback,
                         private juce::AsyncUpdater,
                         private juce::Timer
{
public:
    std::function<void (const juce::Identifier&, juce::var)> emit;   // called on the message thread

    ~MidiBridge() override
    {
        stopTimer();
        cancelPendingUpdate();
        for (auto& i : ins) if (i.dev) i.dev->stop();
        ins.clear();
        outs.clear();
    }

    void start()                { refresh (true); startTimer (1500); }   // device list is checked every 1.5 s (plug / unplug)
    void hello()                { refresh (true); }

    // [[id, delayMs, byte, byte, …], …]
    void send (const juce::var& v)
    {
        auto* list = v["m"].getArray();
        if (list == nullptr) return;
        for (auto& item : *list)
        {
            auto* a = item.getArray();
            if (a == nullptr || a->size() < 3) continue;
            auto* out = outputFor ((*a)[0].toString());
            if (out == nullptr) continue;
            juce::HeapBlock<juce::uint8> bytes ((size_t) (a->size() - 2));
            for (int k = 2; k < a->size(); ++k) bytes[k - 2] = (juce::uint8) (int) (*a)[k];
            juce::MidiMessage msg (bytes.get(), a->size() - 2);
            const double delay = (*a)[1];
            if (delay < 1.0)
                out->sendMessageNow (msg);
            else
            {
                juce::MidiBuffer b;
                b.addEvent (msg, 0);
                out->sendBlockOfMessages (b, juce::Time::getMillisecondCounterHiRes() + delay, 1000.0);
            }
        }
    }

private:
    struct In  { juce::String id, name; std::unique_ptr<juce::MidiInput> dev; };
    struct Out { juce::String id, name; std::unique_ptr<juce::MidiOutput> dev; };
    struct Rx  { juce::String id; juce::MemoryBlock data; double t; };

    std::vector<In> ins;
    std::vector<Out> outs;
    juce::String lastSig;
    juce::CriticalSection lock;
    std::vector<Rx> rx;

    void timerCallback() override { refresh (false); }

    void refresh (bool force)
    {
        auto ia = juce::MidiInput::getAvailableDevices();
        auto oa = juce::MidiOutput::getAvailableDevices();
        juce::String sig;
        for (auto& d : ia) sig << "i" << d.identifier << "|";
        for (auto& d : oa) sig << "o" << d.identifier << "|";
        if (! force && sig == lastSig) return;
        lastSig = sig;

        // inputs: keep the open ones that are still there, open the new ones
        std::vector<In> keep;
        for (auto& d : ia)
        {
            auto it = std::find_if (ins.begin(), ins.end(), [&] (const In& x) { return x.id == d.identifier; });
            if (it != ins.end()) { keep.push_back (std::move (*it)); continue; }
            In in { d.identifier, d.name, juce::MidiInput::openDevice (d.identifier, this) };
            if (in.dev) in.dev->start();
            keep.push_back (std::move (in));
        }
        for (auto& i : ins) if (i.dev) i.dev->stop();
        ins = std::move (keep);

        // outputs are opened when first used; forget the ones that went away
        std::vector<Out> keepOut;
        for (auto& d : oa)
        {
            auto it = std::find_if (outs.begin(), outs.end(), [&] (const Out& x) { return x.id == d.identifier; });
            if (it != outs.end()) keepOut.push_back (std::move (*it));
            else keepOut.push_back ({ d.identifier, d.name, nullptr });
        }
        outs = std::move (keepOut);

        auto* o = new juce::DynamicObject();
        juce::Array<juce::var> vi, vo;
        for (auto& i : ins)  { auto* p = new juce::DynamicObject(); p->setProperty ("id", i.id); p->setProperty ("name", i.name); vi.add (juce::var (p)); }
        for (auto& x : outs) { auto* p = new juce::DynamicObject(); p->setProperty ("id", x.id); p->setProperty ("name", x.name); vo.add (juce::var (p)); }
        o->setProperty ("ins", vi);
        o->setProperty ("outs", vo);
        if (emit) emit ("facetMidiPorts", juce::var (o));
    }

    juce::MidiOutput* outputFor (const juce::String& id)
    {
        auto it = std::find_if (outs.begin(), outs.end(), [&] (const Out& x) { return x.id == id; });
        if (it == outs.end()) return nullptr;
        if (it->dev == nullptr)
        {
            it->dev = juce::MidiOutput::openDevice (id);
            if (it->dev) it->dev->startBackgroundThread();
        }
        return it->dev.get();
    }

    // MIDI thread: keep it short, hand over to the message thread
    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& m) override
    {
        {
            const juce::ScopedLock sl (lock);
            rx.push_back ({ source->getIdentifier(), juce::MemoryBlock (m.getRawData(), (size_t) m.getRawDataSize()),
                            juce::Time::getMillisecondCounterHiRes() });
        }
        triggerAsyncUpdate();
    }

    void handleAsyncUpdate() override
    {
        std::vector<Rx> batch;
        {
            const juce::ScopedLock sl (lock);
            batch.swap (rx);
        }
        if (! emit || batch.empty()) return;
        const auto now = juce::Time::getMillisecondCounterHiRes();
        juce::Array<juce::var> list;
        for (auto& r : batch)
        {
            juce::Array<juce::var> bytes;
            for (size_t k = 0; k < r.data.getSize(); ++k) bytes.add ((int) static_cast<const juce::uint8*> (r.data.getData())[k]);
            auto* p = new juce::DynamicObject();
            p->setProperty ("id", r.id);
            p->setProperty ("data", bytes);
            p->setProperty ("age", now - r.t);
            list.add (juce::var (p));
        }
        auto* o = new juce::DynamicObject();
        o->setProperty ("m", list);
        emit ("facetMidiIn", juce::var (o));
    }
};

//==============================================================================
// Runs before the page's own code:
//  1. reports what the web view supports (secure context, AudioWorklet, storage) for diagnosis
//  2. turns "download" links (how the app saves WAV / ZIP / .facet) into a native Save dialog,
//     because the system web view has no download manager. Big files go across in 2 MB pieces.
//  3. shows a small bar when a newer screen has been downloaded (switch now, or next launch),
//     or when a newer app has been put into the native folder (open the folder)
//  4. gives the screen Web MIDI (navigator.requestMIDIAccess) backed by the app's MIDI bridge
static const char* userScript = R"JS(
(function(){
  var B=function(){return window.__JUCE__&&window.__JUCE__.backend};
  var send=function(id,o){var b=B();if(b)b.emitEvent(id,o)};
  window.__FACET_NATIVE__={shell:'0.3.0'};
  var on=function(id,f){var b=B();if(b)b.addEventListener(id,f)};
  /* ---- Web MIDI stand-in ---- */
  if(!navigator.requestMIDIAccess){
    var MI=new Map(),MO=new Map(),acc=null,ready=null,res=null,q=[],qt=false;
    var flush=function(){qt=false;if(q.length){send('facetMidiOut',{m:q});q=[]}};
    var port=function(p,type){var o={id:p.id,name:p.name,manufacturer:'',version:'',type:type,state:'connected',connection:'open',onstatechange:null,_l:[],
      addEventListener:function(t,f){if(t==='midimessage')this._l.push(f)},removeEventListener:function(t,f){this._l=this._l.filter(function(x){return x!==f})},
      open:function(){return Promise.resolve(this)},close:function(){return Promise.resolve(this)}};
      if(type==='output'){o.send=function(data,ts){var d=ts?Math.max(0,ts-performance.now()):0;q.push([this.id,d].concat(Array.prototype.slice.call(data)));if(!qt){qt=true;Promise.resolve().then(flush)}};o.clear=function(){}}
      return o};
    var sync=function(map,list,type){var seen={},ch=[];list.forEach(function(p){seen[p.id]=1;if(!map.has(p.id)){var o=port(p,type);map.set(p.id,o);ch.push(o)}});
      Array.from(map.keys()).forEach(function(k){if(!seen[k]){var o=map.get(k);o.state='disconnected';map.delete(k);ch.push(o)}});return ch};
    on('facetMidiPorts',function(o){var ch=sync(MI,o.ins||[],'input').concat(sync(MO,o.outs||[],'output'));
      if(acc&&acc.onstatechange)ch.forEach(function(p){try{acc.onstatechange({port:p})}catch(e){console.error(e)}});if(res){res();res=null}});
    on('facetMidiIn',function(o){(o.m||[]).forEach(function(r){var p=MI.get(r.id);if(!p)return;
      var ev={type:'midimessage',data:new Uint8Array(r.data),timeStamp:performance.now()-(r.age||0),target:p,currentTarget:p};
      try{if(p.onmidimessage)p.onmidimessage(ev);p._l.forEach(function(f){f(ev)})}catch(e){console.error(e)}})});
    navigator.requestMIDIAccess=function(){
      if(!ready)ready=new Promise(function(r){res=r;send('facetMidiHello',{});setTimeout(function(){if(res){res();res=null}},2500)})
        .then(function(){acc={inputs:MI,outputs:MO,sysexEnabled:true,onstatechange:null,addEventListener:function(){},removeEventListener:function(){}};return acc});
      return ready};
  }
  /* ---- after an unexpected end: offer the last copy on disk ---- */
  on('facetCrash',function(o){var go=function(){
    var en=/^en/i.test(document.documentElement.getAttribute('lang')||'')||document.body.classList.contains('en');
    var d=document.getElementById('facet-crash')||document.createElement('div');d.id='facet-crash';
    d.style.cssText='position:fixed;left:50%;top:14px;transform:translateX(-50%);z-index:2147483646;display:flex;gap:10px;align-items:center;padding:8px 10px 8px 14px;border-radius:18px;background:#1c1b19;color:#f6f4ee;font:12px/1.4 ui-monospace,Menlo,monospace;box-shadow:0 4px 16px rgba(0,0,0,.3)';
    var bt='border:0;border-radius:12px;padding:5px 10px;font:inherit;cursor:pointer;';
    d.innerHTML='<span>'+(en?'Facet ended unexpectedly last time. Restore the song saved at '+o.time+'?':'前回は途中で終了しました。'+o.time+' の自動保存から曲を戻しますか？')+'</span>'+
      '<button id="facet-crash-go" style="'+bt+'background:#e2401a;color:#fff">'+(en?'Restore':'戻す')+'</button>'+
      '<button id="facet-crash-no" style="'+bt+'background:#3a3833;color:#f6f4ee">'+(en?'Keep as is':'今のまま')+'</button>';
    document.body.appendChild(d);
    document.getElementById('facet-crash-go').onclick=function(){d.remove();send('facetRestore',{})};
    document.getElementById('facet-crash-no').onclick=function(){d.remove()};
  };if(document.body)go();else addEventListener('DOMContentLoaded',go)});
  /* ---- new app in the native folder ---- */
  on('facetAppUpdate',function(o){var go=function(){
    var en=/^en/i.test(document.documentElement.getAttribute('lang')||'')||document.body.classList.contains('en');
    var d=document.getElementById('facet-app')||document.createElement('div');d.id='facet-app';
    d.style.cssText='position:fixed;left:50%;bottom:58px;transform:translateX(-50%);z-index:2147483646;display:flex;gap:10px;align-items:center;padding:8px 10px 8px 14px;border-radius:18px;background:#1c1b19;color:#f6f4ee;font:12px/1.4 ui-monospace,Menlo,monospace;box-shadow:0 4px 16px rgba(0,0,0,.3)';
    var bt='border:0;border-radius:12px;padding:5px 10px;font:inherit;cursor:pointer;';
    d.innerHTML='<span>'+(en?'New app v'+o.ver+' is in the '+o.folder+' folder':'新しいアプリ v'+o.ver+' があります（'+o.folder+' フォルダに置きました）')+'</span>'+
      '<button id="facet-app-go" style="'+bt+'background:#e2401a;color:#fff">'+(en?'Open folder':'フォルダを開く')+'</button>'+
      '<button id="facet-app-later" style="'+bt+'background:#3a3833;color:#f6f4ee">'+(en?'Later':'あとで')+'</button>';
    document.body.appendChild(d);
    document.getElementById('facet-app-go').onclick=function(){d.remove();send('facetOpenFolder',{})};
    document.getElementById('facet-app-later').onclick=function(){d.remove()};
  };if(document.body)go();else addEventListener('DOMContentLoaded',go)});
  addEventListener('DOMContentLoaded',function(){
    var ls=false,idb=!!window.indexedDB;
    try{localStorage.setItem('__t','1');ls=localStorage.getItem('__t')==='1';localStorage.removeItem('__t')}catch(e){}
    send('facetBoot',{secure:!!window.isSecureContext,worklet:!!window.AudioWorkletNode,
      localStorage:ls,indexedDB:idb,ua:navigator.userAgent,href:location.href});
    var b=B();if(b)b.addEventListener('facetUpdate',function(o){
      var en=/^en/i.test(document.documentElement.getAttribute('lang')||'')||document.body.classList.contains('en');
      var d=document.getElementById('facet-upd')||document.createElement('div');d.id='facet-upd';
      d.style.cssText='position:fixed;left:50%;bottom:16px;transform:translateX(-50%);z-index:2147483646;display:flex;gap:10px;align-items:center;padding:8px 10px 8px 14px;border-radius:18px;background:#1c1b19;color:#f6f4ee;font:12px/1.4 ui-monospace,Menlo,monospace;box-shadow:0 4px 16px rgba(0,0,0,.3)';
      var bt='border:0;border-radius:12px;padding:5px 10px;font:inherit;cursor:pointer;';
      d.innerHTML='<span>'+(en?'New screen v'+o.ver+' is ready':'新しい画面 v'+o.ver+' があります')+'</span>'+
        '<button id="facet-upd-go" style="'+bt+'background:#e2401a;color:#fff">'+(en?'Switch now':'いま切り替える')+'</button>'+
        '<button id="facet-upd-later" style="'+bt+'background:#3a3833;color:#f6f4ee">'+(en?'Next launch':'次の起動で')+'</button>';
      document.body.appendChild(d);
      document.getElementById('facet-upd-go').onclick=function(){d.remove();send('facetReload',{})};
      document.getElementById('facet-upd-later').onclick=function(){d.remove()};
    });
  });
  var seq=0;
  var b64=function(u8){var s='',k=0x8000;for(var i=0;i<u8.length;i+=k)s+=String.fromCharCode.apply(null,u8.subarray(i,i+k));return btoa(s)};
  var save=function(href,name){
    fetch(href).then(function(r){return r.arrayBuffer()}).then(function(ab){
      var u8=new Uint8Array(ab),id=++seq,P=2*1024*1024,n=Math.max(1,Math.ceil(u8.length/P));
      for(var i=0;i<n;i++)send('facetSave',{id:id,name:name,i:i,n:n,data:b64(u8.subarray(i*P,Math.min(u8.length,(i+1)*P)))});
    }).catch(function(e){console.warn('facet save failed',e)});
  };
  var click=HTMLAnchorElement.prototype.click;
  HTMLAnchorElement.prototype.click=function(){
    if(B()&&this.hasAttribute('download')&&/^(blob:|data:)/.test(this.href)){save(this.href,this.getAttribute('download')||'facet-export');return}
    return click.apply(this,arguments);
  };
})();
)JS";

//==============================================================================
class MainComponent final : public juce::Component,
                            private juce::Timer
{
public:
    MainComponent()
        : browser (juce::WebBrowserComponent::Options{}
                     .withNativeIntegrationEnabled()
                     .withKeepPageLoadedWhenBrowserIsHidden()
                     .withUserScript (userScript)
                     .withEventListener ("facetBoot",   [this] (juce::var v) { onBoot (v); })
                     .withEventListener ("facetSave",   [this] (juce::var v) { onSave (v); })
                     .withEventListener ("facetReload", [this] (juce::var)   { switchToNext(); })
                     .withEventListener ("facetMidiHello", [this] (juce::var)  { midi.hello(); })
                     .withEventListener ("facetMidiOut", [this] (juce::var v)  { midi.send (v); })
                     .withEventListener ("facetOpenFolder", [this] (juce::var) { if (appFile.exists()) appFile.revealToUser(); })
                     .withEventListener ("facetRestore", [this] (juce::var) { restoreSnapshot(); })
                     .withEventListener ("facetTest", [] (juce::var v)
                     {
                         supportDir().getChildFile ("boot.txt").appendText ("test " + juce::JSON::toString (v, true) + "\n");
                     }))
    {
        addAndMakeVisible (browser);
        midi.emit = [this] (const juce::Identifier& id, juce::var v) { browser.emitEventIfBrowserIsVisible (id, v); };
        midi.start();
        snapTimer.fn = [this] { takeSnapshot(); };
        selfTest = juce::SystemStats::getEnvironmentVariable ("FACET_SELFTEST", {}).isNotEmpty();

        // a "running" mark that a normal quit removes: still there at start = the last run ended unexpectedly
        crashed = runningMark().existsAsFile() || juce::SystemStats::getEnvironmentVariable ("FACET_SIMULATE_CRASH", {}).isNotEmpty();
        runningMark().replaceWithText (juce::Time::getCurrentTime().toISO8601 (true));

        switchToNext (false);              // a screen downloaded last time is used from this launch
        version = preparePage();
        openPage();
        setSize (1440, 900);

        // check for a newer screen shortly after start, then every 6 hours
        if (juce::SystemStats::getEnvironmentVariable ("FACET_NO_UPDATE", {}).isEmpty())
            startTimer (selfTest ? 3000 : 8000);

        // FACET_SELFTEST=1: saves a 5 MB test file through the bridge into the support folder, then quits
        // FACET_SELFTEST=update: waits for the update bar, presses "switch now", quits once the new screen has started
        if (selfTest && juce::SystemStats::getEnvironmentVariable ("FACET_SELFTEST", {}) != "update")
        {
            juce::Timer::callAfterDelay (6000, [this]
            {
                browser.evaluateJavascript ("(function(){var u=new Uint8Array(5*1024*1024+7);for(var i=0;i<u.length;i++)u[i]=i*31&255;"
                                            "var a=document.createElement('a');a.href=URL.createObjectURL(new Blob([u]));a.download='selftest.bin';a.click();})()");
            });
        }
    }

    ~MainComponent() override
    {
        stopTimer();
        snapTimer.stopTimer();
        updater = nullptr;
        runningMark().deleteFile();   // normal quit
    }

    void resized() override { browser.setBounds (getLocalBounds()); }

private:
    void openPage()
    {
        auto page = livePage();
        if (page.existsAsFile())
            browser.goToURL (juce::URL (page).toString (false));
        else
            browser.goToURL ("data:text/html,<p style='font:14px sans-serif'>facet.html not found</p>");
    }

    void timerCallback() override
    {
        startTimer (6 * 60 * 60 * 1000);
        if (updater != nullptr && updater->isThreadRunning()) return;
        juce::Component::SafePointer<MainComponent> safe (this);
        updater = std::make_unique<Updater> (version, [safe] (juce::String ver)
        {
            juce::MessageManager::callAsync ([safe, ver]
            {
                if (safe == nullptr) return;
                safe->pendingVersion = ver;
                auto* o = new juce::DynamicObject();
                o->setProperty ("ver", ver);
                safe->browser.emitEventIfBrowserIsVisible ("facetUpdate", juce::var (o));
                if (safe->selfTest)
                    juce::Timer::callAfterDelay (1500, [safe]
                    {
                        if (safe != nullptr)
                            safe->browser.evaluateJavascript ("var b=document.getElementById('facet-upd-go');if(b)b.click();");
                    });
            });
        });
        updater->appReady = [safe] (juce::String ver, juce::File f)
        {
            juce::MessageManager::callAsync ([safe, ver, f]
            {
                if (safe == nullptr) return;
                safe->appFile = f;
                auto* o = new juce::DynamicObject();
                o->setProperty ("ver", ver);
                o->setProperty ("folder", f.getParentDirectory().getFileName());
                safe->browser.emitEventIfBrowserIsVisible ("facetAppUpdate", juce::var (o));
            });
        };
        updater->startThread();
    }

    // Use a downloaded screen. With reload=true the page is opened again right away.
    void switchToNext (bool reload = true)
    {
        auto next = livePage().getSiblingFile ("facet-next.html");
        if (! next.existsAsFile()) return;
        auto ver = pageVersion (next.loadFileAsString());
        if (ver.isNotEmpty() && next.moveFileTo (livePage()))
        {
            Updater::log ("switched to " + ver);
            version = ver;
            pendingVersion = {};
            if (reload) { switched = true; openPage(); }
        }
    }

    // The first run writes what the web view can do to "boot.txt" — handy when something is off.
    void onBoot (const juce::var& v)
    {
        if (! v["href"].toString().startsWith ("file:")) return;   // the blank page shown before ours
        if (crashed)
        {
            crashed = false;   // offer once
            auto snap = latestSnapshot();
            if (snap.existsAsFile())
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("time", snap.getLastModificationTime().toString (false, true, false, true));
                browser.emitEventIfBrowserIsVisible ("facetCrash", juce::var (o));
                if (selfTest)   // self-test: press "restore" once the bar is up
                    juce::Timer::callAfterDelay (1200, [this] { browser.evaluateJavascript ("var b=document.getElementById('facet-crash-go');if(b)b.click();"); });
            }
        }
        if (! snapTimer.isTimerRunning()) snapTimer.startTimer (selfTest ? 3000 : 30000);
        supportDir().getChildFile ("boot.txt")
            .replaceWithText (juce::Time::getCurrentTime().toISO8601 (true) + "  screen " + version + "\n" + juce::JSON::toString (v) + "\n");
       #if JUCE_MAC
        // alert / confirm / prompt as Mac dialogs (needs the web view, which exists once the page has started)
        if (auto* peer = getPeer())
            supportDir().getChildFile ("boot.txt").appendText (juce::String ("dialogs ")
                + (facetInstallJsDialogs (peer->getNativeHandle(), selfTest) ? "on" : "off") + "\n");
       #endif
        if (selfTest)
        {
            // checks for the automatic test: confirm() answers, and the MIDI stand-in resolves
           #if JUCE_MAC
            const char* c = "var c=confirm('selftest');";   // answered "OK" by the self-test dialogs
           #else
            const char* c = "var c='n/a';";                  // Linux shows its own dialog; nothing to answer it in a test
           #endif
            browser.evaluateJavascript (juce::String ("(function(){") + c + "var b=window.__JUCE__.backend;"
                                        "navigator.requestMIDIAccess().then(function(a){b.emitEvent('facetTest',{confirm:c,midiIn:a.inputs.size,midiOut:a.outputs.size})})})()");
        }
        if (selfTest && switched)
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    //==========================================================================
    // Recovery copies of the song (what the screen keeps in its own storage under "touchdeck"):
    // every 30 s, only when it changed, into snapshots/ (the newest 12 are kept).
    static juce::File runningMark()  { return supportDir().getChildFile ("running.mark"); }
    static juce::File snapshotDir()  { auto d = supportDir().getChildFile ("snapshots"); d.createDirectory(); return d; }

    static juce::File latestSnapshot()
    {
        auto files = snapshotDir().findChildFiles (juce::File::findFiles, false, "song-*.json");
        juce::File best;
        for (auto& f : files) if (best == juce::File{} || f.getFileName() > best.getFileName()) best = f;
        return best;
    }

    void takeSnapshot()
    {
        juce::Component::SafePointer<MainComponent> safe (this);
        browser.evaluateJavascript ("localStorage.getItem('touchdeck')", [safe] (juce::WebBrowserComponent::EvaluationResult r)
        {
            if (safe == nullptr || r.getResult() == nullptr || ! r.getResult()->isString()) return;
            auto text = r.getResult()->toString();
            if (text.length() < 20) return;
            auto h = text.hashCode64();
            if (h == safe->lastSnapHash) return;
            safe->lastSnapHash = h;
            auto f = snapshotDir().getChildFile ("song-" + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S") + ".json");
            writeAtomically (f, text);
            auto files = snapshotDir().findChildFiles (juce::File::findFiles, false, "song-*.json");
            std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b) { return a.getFileName() > b.getFileName(); });
            for (int i = 12; i < files.size(); ++i) files.getReference (i).deleteFile();
        });
    }

    // Put the copy back: the screen's own "save on leave" is switched off for that key first, then the page reloads.
    void restoreSnapshot()
    {
        auto snap = latestSnapshot();
        if (! snap.existsAsFile()) return;
        auto text = snap.loadFileAsString();
        if (! juce::JSON::parse (text).isObject()) return;
        lastSnapHash = text.hashCode64();
        browser.evaluateJavascript ("(function(){var t=" + juce::JSON::toString (juce::var (text)) + ";"
                                    "var set=Storage.prototype.setItem;Storage.prototype.setItem=function(k,v){if(k==='touchdeck')return;return set.call(this,k,v)};"
                                    "set.call(localStorage,'touchdeck',t);location.reload();})()");
        Updater::log ("restored " + snap.getFileName());
    }

    struct SnapTimer final : public juce::Timer
    {
        std::function<void()> fn;
        void timerCallback() override { if (fn) fn(); }
    };

    struct Pending { juce::String name; int n = 0, got = 0; std::unique_ptr<juce::TemporaryFile> tmp; std::unique_ptr<juce::FileOutputStream> out; };

    void onSave (const juce::var& v)
    {
        const int id = v["id"], n = v["n"];
        auto& p = pending[id];
        if (p.tmp == nullptr)
        {
            p.name = v["name"].toString();
            p.n = n;
            p.tmp = std::make_unique<juce::TemporaryFile> (".part");
            p.out = std::make_unique<juce::FileOutputStream> (p.tmp->getFile());
        }
        juce::MemoryOutputStream mo;
        juce::Base64::convertFromBase64 (mo, v["data"].toString());   // pieces arrive in order
        p.out->write (mo.getData(), mo.getDataSize());
        if (++p.got < p.n) return;

        p.out->flush();
        p.out.reset();
        auto tmp = std::shared_ptr<juce::TemporaryFile> (p.tmp.release());
        auto name = p.name;
        pending.erase (id);

        if (selfTest)
        {
            tmp->getFile().copyFileTo (supportDir().getChildFile (name));
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            return;
        }

        auto start = juce::File::getSpecialLocation (juce::File::userDesktopDirectory).getChildFile (name);
        chooser = std::make_unique<juce::FileChooser> ("Save", start);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                | juce::FileBrowserComponent::warnAboutOverwriting,
                              [tmp] (const juce::FileChooser& fc)
                              {
                                  auto dest = fc.getResult();
                                  if (dest != juce::File{})
                                      tmp->getFile().copyFileTo (dest);
                              });
    }

    juce::WebBrowserComponent browser;
    std::map<int, Pending> pending;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<Updater> updater;
    MidiBridge midi;
    juce::File appFile;
    SnapTimer snapTimer;
    juce::int64 lastSnapHash = 0;
    bool crashed = false;
    juce::String version, pendingVersion;
    bool selfTest = false, switched = false;
};

//==============================================================================
class App final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Yda Facet 01"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override { window = std::make_unique<Window>(); }
    void shutdown() override                       { window = nullptr; }
    void systemRequestedQuit() override            { quit(); }

private:
    struct Window final : public juce::DocumentWindow
    {
        Window() : DocumentWindow ("Yda Facet 01", juce::Colour (0xff1c1b19), allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (900, 560, 10000, 10000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<Window> window;
};
} // namespace facet

START_JUCE_APPLICATION (facet::App)
