package gameanalytics;

/**
 * GameAnalytics event type definitions and shared data structures.
 * Uses the GameAnalytics REST API v2 format.
 */
class GACategory {
    public static inline var Design:String = "design";
    public static inline var Business:String = "business";
    public static inline var Resource:String = "resource";
    public static inline var Progression:String = "progression";
    public static inline var Error:String = "error";
}

class GASeverity {
    public static inline var Critical:String = "critical";
    public static inline var Error_:String = "error";
    public static inline var Warning:String = "warning";
    public static inline var Info:String = "info";
    public static inline var Debug:String = "debug";
}

class GAProgressionStatus {
    public static inline var Start:String = "Start";
    public static inline var Complete:String = "Complete";
    public static inline var Fail:String = "Fail";
}

class GAResourceFlowType {
    public static inline var Sink:String = "Sink";
    public static inline var Source:String = "Source";
}

typedef GACommonFields = {
    var user_id:String;
    var session_id:String;
    var build:String;
    @:optional var sdk_version:String;
    @:optional var os_version:String;
    @:optional var manufacturer:String;
    @:optional var device:String;
    @:optional var platform:String;
    var category:String;
    @:optional var client_ts:Int;
    @:optional var v:Int;
}

typedef GADesignEvent = {
    > GACommonFields,
    var event_id:String;
    @:optional var value:Float;
    @:optional var custom_fields:Dynamic;
}

typedef GABusinessEvent = {
    > GACommonFields,
    var event_id:String;
    var amount:Int;
    var currency:String;
    @:optional var transaction_num:Int;
    @:optional var cart_type:String;
    @:optional var receipt:String;
    @:optional var custom_fields:Dynamic;
}

typedef GAResourceEvent = {
    > GACommonFields,
    var event_id:String;
    var amount:Float;
}

typedef GAProgressionEvent = {
    > GACommonFields,
    var event_id:String;
    var attempt_num:Int;
    @:optional var score:Float;
}

typedef GAErrorEvent = {
    > GACommonFields,
    var severity:String;
    var message:String;
}
