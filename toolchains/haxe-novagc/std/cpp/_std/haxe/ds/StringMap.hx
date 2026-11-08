/*
 * Copyright (C)2005-2019 Haxe Foundation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

package haxe.ds;

#if hxcpp_zgc
@:headerClassCode("
  inline bool get_bool(String key) { return get(key).Cast<bool>(); }
  inline int get_int(String key) { return get(key).Cast<int>(); }
  inline Float get_float(String key) { return get(key).Cast<double>(); }
  inline String get_string(String key) { return get(key).Cast<String>(); }
  inline cpp::Int64 get_int64(String key) { return get(key).Cast<cpp::Int64>(); }
")
#end

#if !hxcpp_zgc
@:headerClassCode("
  inline void set(String key, ::null value) { __string_hash_set(HX_MAP_THIS,key,value); }
  inline void set(String key, bool value) { __string_hash_set(HX_MAP_THIS,key,value); }
  inline void set(String key, char value) { __string_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(String key, unsigned char value) { __string_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(String key, signed char value) { __string_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(String key, short value) { __string_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(String key, unsigned short value) { __string_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(String key, int value) { __string_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(String key, unsigned int value) { __string_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(String key, float value) { __string_hash_set_float(HX_MAP_THIS,key,value); }
  inline void set(String key, double value) { __string_hash_set_float(HX_MAP_THIS,key,value); }
  inline void set(String key, ::String value) { __string_hash_set_string(HX_MAP_THIS,key,value); }
  inline void set(String key, cpp::Int64 value) { __string_hash_set_int64(HX_MAP_THIS,key,value); }

  template<typename V, typename H>
  inline void set(String key, const ::cpp::Struct<V,H> &value) {__string_hash_set(HX_MAP_THIS,key,value); }
  template<typename V>
  inline void set(String key, const ::cpp::Function<V> &value) {__string_hash_set(HX_MAP_THIS,key,(Dynamic)value ); }
  template<typename V>
  inline void set(String key, const ::cpp::Pointer<V> &value) {__string_hash_set(HX_MAP_THIS,key,(Dynamic)value ); }

  template<typename VALUE>
  inline void set(Dynamic &key, const VALUE &value) { set( (String)key, value ); }

  inline bool get_bool(String key) { return __string_hash_get_bool(h,key); }
  inline int get_int(String key) { return __string_hash_get_int(h,key); }
  inline Float get_float(String key) { return __string_hash_get_float(h,key); }
  inline String get_string(String key) { return __string_hash_get_string(h,key); }
  inline cpp::Int64 get_int64(String key) { return __string_hash_get_int64(h,key); }
")
#end
@:coreApi class StringMap<T> implements haxe.Constraints.IMap<String, T> {
	#if hxcpp_zgc
	private static inline var INITIAL_CAPACITY:Int = 16;
	private var novaKeys:Array<String>;
	private var novaValues:Array<T>;
	private var novaStates:Array<Int>;
	private var novaSize:Int;
	private var novaRemoved:Int;

	public function new():Void {
		novaKeys = [];
		novaValues = [];
		novaStates = [];
		novaSize = 0;
		novaRemoved = 0;
		initialize(INITIAL_CAPACITY);
	}

	private function initialize(capacity:Int):Void {
		for (_ in 0...capacity) {
			novaKeys.push(null);
			novaValues.push(cast null);
			novaStates.push(0);
		}
	}

	private static inline function hashKey(key:String):Int {
		return key == null ? 0 : untyped __cpp__("{0}.hashCode()", key);
	}

	private function locate(key:String):Int {
		var mask = novaStates.length - 1;
		var index = hashKey(key) & mask;
		var scanned = 0;
		while (scanned < novaStates.length) {
			var state = novaStates[index];
			if (state == 0)
				return -1;
			if (state == 1 && novaKeys[index] == key)
				return index;
			index = (index + 1) & mask;
			scanned++;
		}
		return -1;
	}

	private function insertionSlot(key:String):Int {
		var mask = novaStates.length - 1;
		var index = hashKey(key) & mask;
		var firstRemoved = -1;
		var scanned = 0;
		while (scanned < novaStates.length) {
			var state = novaStates[index];
			if (state == 0)
				return firstRemoved >= 0 ? firstRemoved : index;
			if (state == 2 && firstRemoved < 0)
				firstRemoved = index;
			else if (state == 1 && novaKeys[index] == key)
				return index;
			index = (index + 1) & mask;
			scanned++;
		}
		return firstRemoved;
	}

	private function rebuild(capacity:Int):Void {
		var oldKeys = novaKeys;
		var oldValues = novaValues;
		var oldStates = novaStates;
		novaKeys = [];
		novaValues = [];
		novaStates = [];
		novaSize = 0;
		novaRemoved = 0;
		initialize(capacity);
		for (index in 0...oldStates.length) {
			if (oldStates[index] == 1)
				set(oldKeys[index], oldValues[index]);
		}
	}

	public function set(key:String, value:T):Void {
		if ((novaSize + novaRemoved + 1) * 4 >= novaStates.length * 3)
			rebuild(novaStates.length << 1);
		var index = insertionSlot(key);
		if (index < 0) {
			rebuild(novaStates.length << 1);
			index = insertionSlot(key);
		}
		if (novaStates[index] != 1) {
			if (novaStates[index] == 2)
				novaRemoved--;
			novaStates[index] = 1;
			novaKeys[index] = key;
			novaSize++;
		}
		novaValues[index] = value;
	}

	public function get(key:String):Null<T> {
		var index = locate(key);
		return index < 0 ? null : novaValues[index];
	}

	public function exists(key:String):Bool {
		return locate(key) >= 0;
	}

	public function remove(key:String):Bool {
		var index = locate(key);
		if (index < 0)
			return false;
		novaStates[index] = 2;
		novaKeys[index] = null;
		novaValues[index] = cast null;
		novaSize--;
		novaRemoved++;
		if (novaRemoved > novaSize && novaStates.length > INITIAL_CAPACITY)
			rebuild(novaStates.length);
		return true;
	}

	public function keys():Iterator<String> {
		var result:Array<String> = [];
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1)
				result.push(novaKeys[index]);
		return result.iterator();
	}

	public function iterator():Iterator<T> {
		var result:Array<T> = [];
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1)
				result.push(novaValues[index]);
		return result.iterator();
	}

	@:runtime public inline function keyValueIterator():KeyValueIterator<String, T> {
		return new haxe.iterators.MapKeyValueIterator(this);
	}

	public function copy():StringMap<T> {
		var copied = new StringMap<T>();
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1)
				copied.set(novaKeys[index], novaValues[index]);
		return copied;
	}

	public function toString():String {
		var result:Array<String> = [];
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1)
				result.push(Std.string(novaKeys[index]) + " => " + Std.string(novaValues[index]));
		return "[" + result.join(", ") + "]";
	}

	public function clear():Void {
		novaKeys = [];
		novaValues = [];
		novaStates = [];
		novaSize = 0;
		novaRemoved = 0;
		initialize(INITIAL_CAPACITY);
	}
	#else
	@:ifFeature("haxe.ds.StringMap.*")
	private var h:Dynamic;

	public function new():Void {}

	public function set(key:String, value:T):Void {
		untyped __global__.__string_hash_set(__cpp__("HX_MAP_THIS"), key, value);
	}

	public function get(key:String):Null<T> {
		return untyped __global__.__string_hash_get(h, key);
	}

	public function exists(key:String):Bool {
		return untyped __global__.__string_hash_exists(h, key);
	}

	public function remove(key:String):Bool {
		return untyped __global__.__string_hash_remove(h, key);
	}

	public function keys():Iterator<String> {
		var a:Array<String> = untyped __global__.__string_hash_keys(h);
		return a.iterator();
	}

	public function iterator():Iterator<T> {
		var a:Array<Dynamic> = untyped __global__.__string_hash_values(h);
		return a.iterator();
	}

	@:runtime public inline function keyValueIterator():KeyValueIterator<String, T> {
		return new haxe.iterators.MapKeyValueIterator(this);
	}

	public function copy():StringMap<T> {
		var copied = new StringMap();
		for (key in keys())
			copied.set(key, get(key));
		return copied;
	}

	public function toString():String {
		return untyped __global__.__string_hash_to_string(h);
	}

	public function clear():Void {
		#if (hxcpp_api_level >= 400)
		return untyped __global__.__string_hash_clear(h);
		#else
		h = null;
		#end
	}

	#if (scriptable)
	private function setString(key:String, val:String):Void {
		untyped __string_hash_set_string(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setInt(key:String, val:Int):Void {
		untyped __string_hash_set_int(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setBool(key:String, val:Bool):Void {
		untyped __string_hash_set_int(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setFloat(key:String, val:Float):Void {
		untyped __string_hash_set_float(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setInt64(key:String, val:haxe.Int64):Void {
		untyped __string_hash_set_int64(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function getString(key:String):String {
		return untyped __string_hash_get_string(h, key);
	}

	private function getInt(key:String):Int {
		return untyped __string_hash_get_int(h, key);
	}

	private function getBool(key:String):Bool {
		return untyped __string_hash_get_bool(h, key);
	}

	private function getFloat(key:String):Float {
		return untyped __string_hash_get_float(h, key);
	}

	private function getInt64(key:String):haxe.Int64 {
		return untyped __string_hash_get_int64(h, key);
	}
	#end
	#end
}
