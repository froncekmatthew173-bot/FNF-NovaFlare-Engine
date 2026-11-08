package gameanalytics;

/** Keys are injected at compile time from GitHub Actions Secrets or local environment variables. */
@:build(gameanalytics.GACompileConfig.buildConfig())
class GameAnalyticsConfig
{
    public static var GAME_KEY:String = '';
    public static var SECRET_KEY:String = '';
    public static var BUILD_VERSION:String = '1.2.1-HF-2';
    public static var VERBOSE_LOGGING:Bool = #if debug true #else false #end;
    public static var SANDBOX_MODE:Bool = #if debug true #else false #end;
}
