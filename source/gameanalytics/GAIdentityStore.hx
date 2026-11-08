package gameanalytics;

import haxe.io.Bytes;
import haxe.io.Path;
#if sys
import sys.FileSystem;
import sys.io.File;
#end

#if (windows && cpp)
@:cppFileCode('#include <windows.h>\n#include <objbase.h>\n#include <cstdio>')
@:buildXml('<target id="haxe"><lib name="ole32.lib" if="windows" /></target>')
#end
/** Identity belongs to private app storage, never a distributable game/resource folder. */
class GAIdentityStore {
    final directory:Null<String>;
    final makeId:Void->String;
    var currentId:Null<String>;
    var sessionNum:Int = 0;

    public function new(directory:Null<String>, ?makeId:Void->String) {
        this.directory = directory;
        this.makeId = makeId == null ? generateUuid : makeId;
    }

    public static function isValidUuid(value:String):Bool {
        return value != null && ~/^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/i.match(value);
    }

    public function loadOrCreate():String {
        if (currentId != null) return currentId;
        #if sys
        if (directory != null) try {
            var path = Path.join([directory, "user_id"]);
            if (FileSystem.exists(path)) {
                var saved = StringTools.trim(File.getContent(path));
                if (isValidUuid(saved)) return currentId = saved.toLowerCase();
            }
        } catch (_:Dynamic) {}
        #end
        currentId = makeId();
        if (!isValidUuid(currentId)) throw "GameAnalytics UUID generation failed";
        #if sys
        if (directory != null) try {
            FileSystem.createDirectory(directory);
            // Only private directories are written. The old shared identity is deliberately not migrated.
            File.saveContent(Path.join([directory, "user_id"]), currentId);
        } catch (_:Dynamic) {
            trace("[GameAnalytics] Private identity storage unavailable; using a temporary ID.");
        }
        #end
        return currentId;
    }

    public function nextSessionNum():Int {
        #if sys
        if (directory != null) try {
            var path = Path.join([directory, "session_num"]);
            if (FileSystem.exists(path)) {
                var saved = Std.parseInt(StringTools.trim(File.getContent(path)));
                if (saved != null && saved > sessionNum) sessionNum = saved;
            }
        } catch (_:Dynamic) {}
        #end
        sessionNum++;
        #if sys
        if (directory != null) try {
            FileSystem.createDirectory(directory);
            File.saveContent(Path.join([directory, "session_num"]), Std.string(sessionNum));
        } catch (_:Dynamic) {}
        #end
        return sessionNum;
    }

    public static function defaultDirectory():Null<String> {
        #if android
        // Android's no-backup directory is private and cannot be cloned by resource packs or cloud restore.
        try {
            var base = android.content.Context.getNoBackupFilesDir();
            return base == null || base.length == 0 ? null : Path.join([base, "gameanalytics-v3"]);
        } catch (_:Dynamic) { return null; }
        #elseif windows
        #if sys
        var base = Sys.getEnv("LOCALAPPDATA");
        return base == null || base.length == 0 ? null : Path.join([base, "NovaFlareEngine", "gameanalytics-v3"]);
        #else
        return null;
        #end
        #elseif sys
        try {
            var base = lime.system.System.applicationStorageDirectory;
            return base == null || base.length == 0 ? null : Path.join([base, "gameanalytics-v3"]);
        } catch (_:Dynamic) { return null; }
        #else
        return null;
        #end
    }

    public static function generateUuid():String {
        #if (windows && cpp)
        var id = windowsUuid();
        if (isValidUuid(id)) return id;
        throw "GameAnalytics OS UUID generation failed";
        #elseif sys
        // Android, Linux and Apple platforms provide the OS random source.
        var input = File.read("/dev/urandom", true);
        var bytes:Bytes;
        try { bytes = input.read(16); } catch (error:Dynamic) { input.close(); throw error; }
        input.close();
        return uuidFromBytes(bytes);
        #elseif js
        var random:Dynamic = js.Syntax.code("globalThis.crypto.getRandomValues(new Uint8Array(16))");
        var bytes = Bytes.alloc(16);
        for (i in 0...16) bytes.set(i, random[i]);
        return uuidFromBytes(bytes);
        #else
        throw "GameAnalytics requires an OS random source";
        #end
    }

    static function uuidFromBytes(bytes:Bytes):String {
        if (bytes.length != 16) throw "Invalid UUID entropy length";
        bytes.set(6, (bytes.get(6) & 15) | 64);
        bytes.set(8, (bytes.get(8) & 63) | 128);
        var hex = bytes.toHex();
        return hex.substr(0,8)+"-"+hex.substr(8,4)+"-"+hex.substr(12,4)+"-"+hex.substr(16,4)+"-"+hex.substr(20,12);
    }

    #if (windows && cpp)
    @:functionCode('
        GUID id;
        if (FAILED(CoCreateGuid(&id))) return null();
        char text[37];
        std::snprintf(text, sizeof(text), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            static_cast<unsigned long>(id.Data1), static_cast<unsigned int>(id.Data2), static_cast<unsigned int>(id.Data3),
            id.Data4[0], id.Data4[1], id.Data4[2], id.Data4[3], id.Data4[4], id.Data4[5], id.Data4[6], id.Data4[7]);
        return ::String(text);
    ')
    static function windowsUuid():String { return null; }
    #end
}
