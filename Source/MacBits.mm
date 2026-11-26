// Yda Facet 01 — macOS-only helpers
// JavaScript alert / confirm / prompt inside the app's web view.
// The system web view (WKWebView) shows nothing for these unless the app answers them itself, so confirm() would
// always come back "cancel" (that is why loading a .facet file did nothing: it asks "replace the current song?" first).
// Here the web view's existing UI delegate gets the three answers added, shown as normal Mac dialogs.

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <objc/runtime.h>

static bool facetAutoOK = false;   // self-test: answer "OK" without showing anything

static WKWebView* facetFindWebView (NSView* v)
{
    if (v == nil) return nil;
    if ([v isKindOfClass: [WKWebView class]]) return (WKWebView*) v;
    for (NSView* s in [v subviews])
        if (WKWebView* w = facetFindWebView (s)) return w;
    return nil;
}

static NSAlert* facetMakeAlert (NSString* message)
{
    NSAlert* a = [[[NSAlert alloc] init] autorelease];
    [a setMessageText: @"Yda Facet 01"];
    [a setInformativeText: message != nil ? message : @""];
    return a;
}

static void facetAlert (id, SEL, WKWebView*, NSString* message, WKFrameInfo*, void (^done)(void))
{
    if (! facetAutoOK)
    {
        NSAlert* a = facetMakeAlert (message);
        [a addButtonWithTitle: @"OK"];
        [a runModal];
    }
    done();
}

static void facetConfirm (id, SEL, WKWebView*, NSString* message, WKFrameInfo*, void (^done)(BOOL))
{
    if (facetAutoOK) { done (YES); return; }
    NSAlert* a = facetMakeAlert (message);
    [a addButtonWithTitle: @"OK"];
    [a addButtonWithTitle: @"キャンセル"];
    done ([a runModal] == NSAlertFirstButtonReturn);
}

static void facetPrompt (id, SEL, WKWebView*, NSString* prompt, NSString* defaultText, WKFrameInfo*, void (^done)(NSString*))
{
    if (facetAutoOK) { done (defaultText != nil ? defaultText : @""); return; }
    NSAlert* a = facetMakeAlert (prompt);
    [a addButtonWithTitle: @"OK"];
    [a addButtonWithTitle: @"キャンセル"];
    NSTextField* field = [[[NSTextField alloc] initWithFrame: NSMakeRect (0, 0, 280, 24)] autorelease];
    [field setStringValue: defaultText != nil ? defaultText : @""];
    [a setAccessoryView: field];
    [[a window] setInitialFirstResponder: field];
    done ([a runModal] == NSAlertFirstButtonReturn ? [field stringValue] : nil);
}

// Returns true once the web view answers the three dialogs. Safe to call more than once.
bool facetInstallJsDialogs (void* peerView, bool autoOK)
{
    facetAutoOK = autoOK;
    WKWebView* wk = facetFindWebView ((NSView*) peerView);
    if (wk == nil) return false;

    id delegate = [wk UIDelegate];
    if (delegate == nil) return false;

    Class c = object_getClass (delegate);
    class_addMethod (c, @selector (webView:runJavaScriptAlertPanelWithMessage:initiatedByFrame:completionHandler:),
                     (IMP) facetAlert, "v@:@@@@?");
    class_addMethod (c, @selector (webView:runJavaScriptConfirmPanelWithMessage:initiatedByFrame:completionHandler:),
                     (IMP) facetConfirm, "v@:@@@@?");
    class_addMethod (c, @selector (webView:runJavaScriptTextInputPanelWithPrompt:defaultText:initiatedByFrame:completionHandler:),
                     (IMP) facetPrompt, "v@:@@@@@?");

    // WebKit remembers which answers a delegate has when it is set, so set it again
    [delegate retain];
    [wk setUIDelegate: nil];
    [wk setUIDelegate: delegate];
    [delegate release];
    return [delegate respondsToSelector: @selector (webView:runJavaScriptConfirmPanelWithMessage:initiatedByFrame:completionHandler:)];
}
