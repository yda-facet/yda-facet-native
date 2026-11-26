// Yda Facet 01 — native shell
// The app window holds the Web screen (facet.html) in the system web view.
// 0.1.0  window + Web screen + native Save dialog for exports
// 0.2.0  the Web screen updates itself from the YDA site (no rebuild needed)
// Sound still runs inside the web view for now; later steps move it into a native engine.

#include <JuceHeader.h>

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

    void run() override
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
    static juce::String fetch (const juce::String& u)
    {
        int status = 0;
        auto in = juce::URL (u).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withConnectionTimeoutMs (15000)
                                                     .withStatusCode (&status));
        if (in == nullptr || status >= 400) return {};
        return in->readEntireStreamAsString();
    }

    juce::String current;
    std::function<void (juce::String)> ready;
};

//==============================================================================
// Runs before the page's own code:
//  1. reports what the web view supports (secure context, AudioWorklet, storage) for diagnosis
//  2. turns "download" links (how the app saves WAV / ZIP / .facet) into a native Save dialog,
//     because the system web view has no download manager. Big files go across in 2 MB pieces.
//  3. shows a small bar when a newer screen has been downloaded (switch now, or next launch)
static const char* userScript = R"JS(
(function(){
  var B=function(){return window.__JUCE__&&window.__JUCE__.backend};
  var send=function(id,o){var b=B();if(b)b.emitEvent(id,o)};
  window.__FACET_NATIVE__={shell:'0.2.0'};
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
                     .withEventListener ("facetReload", [this] (juce::var)   { switchToNext(); }))
    {
        addAndMakeVisible (browser);
        selfTest = juce::SystemStats::getEnvironmentVariable ("FACET_SELFTEST", {}).isNotEmpty();

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

    ~MainComponent() override { stopTimer(); updater = nullptr; }

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
        supportDir().getChildFile ("boot.txt")
            .replaceWithText (juce::Time::getCurrentTime().toISO8601 (true) + "  screen " + version + "\n" + juce::JSON::toString (v) + "\n");
        if (selfTest && switched)
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

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
