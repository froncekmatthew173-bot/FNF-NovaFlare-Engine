package gameanalytics;

/**
 * Public bridge to GameAnalytics. The implementation is versioned with the
 * engine; credentials are supplied through the build environment.
 * Without credentials, these methods compile as no-ops.
 */
class GABridge {

    public static function init(?userId:String):Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance == null)
            GameAnalytics.init(userId);
        #end
    }

    public static function update(elapsedSec:Float = 1.0 / 60.0):Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.update(elapsedSec);
        #end
    }

    public static function onPause():Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.onPause();
        #end
    }

    public static function flush():Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.flushNow();
        #end
    }

    public static function sendDesign(eventId:String, ?value:Float, ?customFields:Dynamic):Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.sendDesign(eventId, value, customFields);
        #end
    }

    public static function sendBusiness(eventId:String, amount:Int, currency:String,
        ?transactionNum:Int, ?cartType:String, ?receipt:String, ?customFields:Dynamic):Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.sendBusiness(eventId, amount, currency,
                transactionNum, cartType, receipt, customFields);
        #end
    }

    public static function sendResource(flowType:String, currency:String,
        itemType:String, itemId:String, amount:Float):Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.sendResource(flowType, currency, itemType, itemId, amount);
        #end
    }

    public static function sendProgression(status:String, progression01:String,
        ?progression02:String, ?progression03:String, ?score:Float, attemptNum:Int = 1):Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.sendProgression(status, progression01,
                progression02, progression03, score, attemptNum);
        #end
    }

    public static function sendError(severity:String, message:String):Void {
        #if GAMEANALYTICS_ENABLED
        if (GameAnalytics.instance != null)
            GameAnalytics.instance.sendError(severity, message);
        #end
    }
}
