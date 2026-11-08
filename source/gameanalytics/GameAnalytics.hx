package gameanalytics;

import haxe.Http;
import haxe.Json;
import haxe.crypto.Base64;
import haxe.crypto.Hmac;
import haxe.io.Bytes;
import haxe.MainLoop;

#if (cpp || hl)
import lime.system.BackendThread;
#end

#if (sys || cpp || neko || hl || java)
import sys.io.File;
import sys.FileSystem;
#end

/**
 * GameAnalytics REST API v2 client.
 *
 * Direct HTTP-based integration — no outdated SDK required.
 * Events are queued locally and flushed in batches.
 *
 * Usage:
 *   GameAnalytics.init();
 *   GameAnalytics.instance.update(elapsedSec);
 *   GameAnalytics.instance.sendDesign("level:start", 1.0);
 */
class GameAnalytics {
    // ------------------------------------------------------------------
    // Constants
    // ------------------------------------------------------------------

    static var API_URL_TEMPLATE:String = "https://api.gameanalytics.com/v2/$game_key/events";
    static var MAX_QUEUE_SIZE:Int = 50;
    static var FLUSH_INTERVAL_SEC:Float = 10.0;
    static var MAX_RETRIES:Int = 3;
    static var SDK_VERSION:String = "rest api v2";
    static var diagnosticsEnabled:Bool = runtimeDiagnosticsEnabled();

    static function runtimeDiagnosticsEnabled():Bool {
        if (GameAnalyticsConfig.VERBOSE_LOGGING) return true;
        #if sys
        var value:String = Sys.getEnv("GAMEANALYTICS_DIAGNOSTICS");
        return value == "1" || value == "true";
        #else
        return false;
        #end
    }

    static inline function diagnostic(message:String):Void {
        if (!diagnosticsEnabled) return;
        #if sys
        Sys.println('[GameAnalytics] $message');
        #else
        trace('[GameAnalytics] $message');
        #end
    }

    // ------------------------------------------------------------------
    // Singleton
    // ------------------------------------------------------------------

    public static var instance(default, null):GameAnalytics;

    public static function init(?customUserId:String):Void {
        if (instance != null) {
            trace("[GameAnalytics] already initialized");
            return;
        }
        try {
            instance = new GameAnalytics(customUserId);
        } catch (_:Dynamic) {
            // Analytics must not stop game startup when private storage or OS entropy is unavailable.
            diagnostic("disabled: identity initialization unavailable");
            return;
        }
        instance._init();
    }

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    var gameKey:String;
    var secretKey:String;
    var userId:String;
    var identityStore:GAIdentityStore;
    var sessionId:String;
    var sessionNum:Int;
    var build:String;

    var eventQueue:Array<Dynamic> = [];
    var timeSinceLastFlush:Float = 0.0;
    var isSending:Bool = false;
    var retryCount:Int = 0;

    var platform:String;
    var osVersion:String;
    var manufacturer:String;
    var device:String;

    var initialized:Bool = false;

    // ------------------------------------------------------------------
    // Constructor
    // ------------------------------------------------------------------

    function new(?customUserId:String) {
        identityStore = new GAIdentityStore(GAIdentityStore.defaultDirectory());

        gameKey = GameAnalyticsConfig.GAME_KEY;
        secretKey = GameAnalyticsConfig.SECRET_KEY;
        build = GameAnalyticsConfig.BUILD_VERSION;
        sessionId = generateUUID();
        userId = (customUserId != null && customUserId.length > 0) ? customUserId : loadOrCreateUserId();
        sessionNum = loadAndIncrementSessionNum();
        detectPlatform();
    }

    function _init():Void {
        initialized = true;
        // Collection API requires a user/session-start event to be the first
        // event of the first batch for every launch.
        enqueue(makeCommonFields("user"));
        // Do not print user/session IDs or credentials, even in diagnostics.
        diagnostic('initialized | platform=$platform | build=$build');
    }

    // ------------------------------------------------------------------
    // Public API — lifecycle
    // ------------------------------------------------------------------

    public function update(elapsedSec:Float):Void {
        if (!initialized) return;
        timeSinceLastFlush += elapsedSec;
        var shouldFlush = (eventQueue.length >= MAX_QUEUE_SIZE)
            || (timeSinceLastFlush >= FLUSH_INTERVAL_SEC && eventQueue.length > 0);
        if (shouldFlush && !isSending) flush();
    }

    public function flushNow():Void {
        if (eventQueue.length > 0 && !isSending) flush();
    }

    public function onPause():Void {
        if (eventQueue.length > 0 && !isSending) flush();
    }

    public function getUserId():String return userId;
    public function getSessionId():String return sessionId;

    // ------------------------------------------------------------------
    // Public API — events
    // ------------------------------------------------------------------

    public function sendDesign(eventId:String, ?value:Float, ?customFields:Dynamic):Void {
        if (!initialized) return;
        var evt:Dynamic = makeCommonFields("design");
        evt.event_id = eventId;
        if (value != null) evt.value = value;
        if (customFields != null) evt.custom_fields = customFields;
        enqueue(evt);
    }

    public function sendBusiness(eventId:String, amount:Int, currency:String,
        ?transactionNum:Int, ?cartType:String, ?receipt:String, ?customFields:Dynamic):Void {
        if (!initialized) return;
        var evt:Dynamic = makeCommonFields("business");
        evt.event_id = eventId;
        evt.amount = amount;
        evt.currency = currency;
        if (transactionNum != null) evt.transaction_num = transactionNum;
        if (cartType != null) evt.cart_type = cartType;
        if (receipt != null) evt.receipt = receipt;
        if (customFields != null) evt.custom_fields = customFields;
        enqueue(evt);
    }

    public function sendResource(flowType:String, currency:String,
        itemType:String, itemId:String, amount:Float):Void {
        if (!initialized) return;
        var evt:Dynamic = makeCommonFields("resource");
        evt.event_id = flowType + ":" + currency + ":" + itemType + ":" + itemId;
        evt.amount = amount;
        enqueue(evt);
    }

    public function sendProgression(status:String, progression01:String,
        ?progression02:String, ?progression03:String, ?score:Float, attemptNum:Int = 1):Void {
        if (!initialized) return;
        var eventId:String = status + ":" + progression01;
        if (progression02 != null) eventId += ":" + progression02;
        if (progression03 != null) eventId += ":" + progression03;
        var evt:Dynamic = makeCommonFields("progression");
        evt.event_id = eventId;
        evt.attempt_num = attemptNum;
        if (score != null) evt.score = score;
        enqueue(evt);
    }

    public function sendError(severity:String, message:String):Void {
        if (!initialized) return;
        var evt:Dynamic = makeCommonFields("error");
        evt.severity = severity;
        evt.message = message;
        enqueue(evt);
    }

    // ------------------------------------------------------------------
    // Internal — event building
    // ------------------------------------------------------------------

    function makeCommonFields(category:String):Dynamic {
        return {
            user_id: userId,
            session_id: sessionId,
            session_num: sessionNum,
            build: build,
            sdk_version: SDK_VERSION,
            os_version: osVersion,
            manufacturer: manufacturer,
            device: device,
            platform: platform,
            category: category,
            client_ts: Std.int(Date.now().getTime() / 1000),
            v: 2,
        };
    }

    function enqueue(evt:Dynamic):Void {
        eventQueue.push(evt);
        if (evt.event_id != null)
            diagnostic('queued: ${evt.category}.${evt.event_id} (queue: ${eventQueue.length})');
    }

    // ------------------------------------------------------------------
    // Internal — network
    // ------------------------------------------------------------------

    function flush():Void {
        if (eventQueue.length == 0) return;

        var batch:Array<Dynamic> = eventQueue.copy();
        eventQueue = [];
        timeSinceLastFlush = 0.0;

        var body:String = Json.stringify(batch);
        var url:String = StringTools.replace(API_URL_TEMPLATE, "$game_key", gameKey);

        diagnostic('flushing ${batch.length} events');
        isSending = true;

        #if (cpp || hl)
        // sys.Http is synchronous on native targets. Uploading on the update
        // thread caused a DNS/TLS/network stall every flush interval, which is
        // visible even when the rest of the menu renders above 1000 FPS.
        // Lime's persistent backend worker is already GC-aware and is shared
        // by the engine's other asynchronous I/O paths.
        var authorization:String = sign(body);
        BackendThread.run(function():Void {
            var response:String = null;
            var error:String = null;
            try {
                var http:Http = createRequest(url, body, authorization);
                http.onData = function(data:String):Void response = data;
                http.onError = function(message:String):Void error = message;
                http.request(true);
            } catch (e:Dynamic) {
                error = Std.string(e);
            }

            MainLoop.runInMainThread(function():Void {
                finishFlush(batch, response, error);
            });
        });
        #else
        var http:Http = createRequest(url, body, sign(body));

        http.onData = function(data:String):Void {
            finishFlush(batch, data, null);
        };

        http.onError = function(error:String):Void {
            finishFlush(batch, null, error);
        };

        http.request(true);
        #end
    }

    function createRequest(url:String, body:String, authorization:String):Http {
        var http:Http = new Http(url);
        http.setHeader("Content-Type", "application/json");
        http.setHeader("Authorization", authorization);
        http.setPostData(body);
        return http;
    }

    function finishFlush(batch:Array<Dynamic>, response:String, error:String):Void {
        isSending = false;
        if (error == null) {
            retryCount = 0;
            diagnostic('delivery accepted (${batch.length} events)');
            return;
        }

        // Authentication/schema failures are deterministic. GameAnalytics
        // explicitly requires these batches to be dropped rather than retried.
        if (error.indexOf("Http Error #400") >= 0
            || error.indexOf("Http Error #401") >= 0
            || error.indexOf("Http Error #403") >= 0) {
            retryCount = 0;
            diagnostic('delivery rejected permanently: $error');
            return;
        }

        diagnostic('delivery error (retry ${retryCount + 1}/$MAX_RETRIES): $error');
        if (retryCount < MAX_RETRIES) {
            retryCount++;
            // Preserve events queued while this request was in flight.
            eventQueue = batch.concat(eventQueue);
            timeSinceLastFlush = FLUSH_INTERVAL_SEC - 2;
        } else {
            retryCount = 0;
        }
    }

    function sign(body:String):String {
        var hmac:Hmac = new Hmac(SHA256);
        var sig:Bytes = hmac.make(Bytes.ofString(secretKey), Bytes.ofString(body));
        // Collection API v2 expects the raw HMAC digest encoded as Base64,
        // not the human-readable hexadecimal form.
        return Base64.encode(sig);
    }

    // ------------------------------------------------------------------
    // Internal — platform detection
    // ------------------------------------------------------------------

    function detectPlatform():Void {
        #if ios
        platform = "ios";
        #elseif android
        platform = "android";
        #elseif windows
        platform = "windows";
        #elseif mac
        platform = "mac_osx";
        #elseif linux
        platform = "linux";
        #elseif html5
        platform = "webgl";
        #elseif switch
        platform = "nintendo_switch";
        #else
        platform = "unknown";
        #end

        var rawVersion:String = null;
        #if lime
        try rawVersion = lime.system.System.platformVersion catch (_:Dynamic) {}
        #end
        // The collector schema requires '<platform> <numeric version>'. Lime
        // returns only the version on desktop and may append build text.
        var numericVersion:String = "0";
        if (rawVersion != null) {
            var versionPattern = ~/[0-9]+(?:\.[0-9]+){0,2}/;
            if (versionPattern.match(rawVersion)) numericVersion = versionPattern.matched(0);
        }
        osVersion = '$platform $numericVersion';

        #if (cpp || neko || hl)
        try { manufacturer = Sys.systemName(); device = "PC"; } catch (_:Dynamic) { manufacturer = "unknown"; device = "unknown"; }
        #else
        manufacturer = "unknown";
        device = "unknown";
        #end
    }

    // ------------------------------------------------------------------
    // Internal — persistent user ID
    // ------------------------------------------------------------------

    function loadOrCreateUserId():String {
        return identityStore.loadOrCreate();
    }

    function loadAndIncrementSessionNum():Int {
        return identityStore.nextSessionNum();
    }

    function generateUUID():String {
        return GAIdentityStore.generateUuid();
    }
}
