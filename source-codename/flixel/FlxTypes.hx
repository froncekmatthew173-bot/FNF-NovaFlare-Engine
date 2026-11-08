package flixel;

/**
 * Integer aliases retained by Codename Engine's public script API.
 *
 * They were removed from newer HaxeFlixel revisions, so the compatibility
 * chain provides them without replacing NovaFlare's Flixel package.
 */
typedef ByteInt = #if cpp cpp.Int8 #else Int #end;
typedef ByteUInt = #if cpp cpp.UInt8 #else UInt #end;
typedef ShortInt = #if cpp cpp.Int16 #else Int #end;
typedef ShortUInt = #if cpp cpp.UInt16 #else UInt #end;
