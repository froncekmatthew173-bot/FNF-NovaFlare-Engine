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
  inline bool get_bool(Dynamic key) { return get(key).Cast<bool>(); }
  inline int get_int(Dynamic key) { return get(key).Cast<int>(); }
  inline Float get_float(Dynamic key) { return get(key).Cast<double>(); }
  inline String get_string(Dynamic key) { return get(key).Cast<String>(); }
  inline cpp::Int64 get_int64(Dynamic key) { return get(key).Cast<cpp::Int64>(); }
")
#end

#if !hxcpp_zgc
@:headerClassCode("
  inline void set(Dynamic key, ::null value) { __object_hash_set(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, bool value) { __object_hash_set(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, char value) { __object_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, unsigned char value) { __object_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, signed char value) { __object_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, short value) { __object_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, unsigned short value) { __object_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, int value) { __object_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, unsigned int value) { __object_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, float value) { __object_hash_set_float(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, double value) { __object_hash_set_float(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, ::String value) { __object_hash_set_string(HX_MAP_THIS,key,value); }
  inline void set(Dynamic key, cpp::Int64 value) { __object_hash_set_int64(HX_MAP_THIS,key,value); }


  template<typename V, typename H>
  inline void set(Dynamic key, const ::cpp::Struct<V,H> &value) {__object_hash_set(HX_MAP_THIS,key,value); }
  template<typename V>
  inline void set(Dynamic key, const ::cpp::Function<V> &value) {__object_hash_set(HX_MAP_THIS,key,(Dynamic)value ); }
  template<typename V>
  inline void set(Dynamic key, const ::cpp::Pointer<V> &value) {__object_hash_set(HX_MAP_THIS,key,(Dynamic)value ); }

  inline bool get_bool(Dynamic key) { return __object_hash_get_bool(h,key); }
  inline int get_int(Dynamic key) { return __object_hash_get_int(h,key); }
  inline Float get_float(Dynamic key) { return __object_hash_get_float(h,key); }
  inline String get_string(Dynamic key) { return __object_hash_get_string(h,key); }
  inline cpp::Int64 get_int64(Dynamic key) { return __object_hash_get_int64(h,key); }
")
#end
@:coreApi
class ObjectMap<K:{}, V> implements haxe.Constraints.IMap<K, V> {
	#if hxcpp_zgc
	private static inline var INITIAL_CAPACITY:Int = 16;
	private var novaKeys:Array<K>;
	private var novaValues:Array<V>;
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
			novaKeys.push(cast null);
			novaValues.push(cast null);
			novaStates.push(0);
		}
	}

	private inline function hashKey(key:K):Int {
		return key == null ? 0 : untyped __cpp__("::hx::ObjectHash({0})", key);
	}

	private function locate(key:K):Int {
		var mask = novaStates.length - 1;
		var index = hashKey(key) & mask;
		var scanned = 0;
		while (scanned < novaStates.length) {
			var state = novaStates[index];
			if (state == 0) return -1;
			if (state == 1 && novaKeys[index] == key) return index;
			index = (index + 1) & mask;
			scanned++;
		}
		return -1;
	}

	private function insertionSlot(key:K):Int {
		var mask = novaStates.length - 1;
		var index = hashKey(key) & mask;
		var firstRemoved = -1;
		var scanned = 0;
		while (scanned < novaStates.length) {
			var state = novaStates[index];
			if (state == 0) return firstRemoved >= 0 ? firstRemoved : index;
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
		for (index in 0...oldStates.length)
			if (oldStates[index] == 1)
				set(oldKeys[index], oldValues[index]);
	}

	public function set(key:K, value:V):Void {
		if ((novaSize + novaRemoved + 1) * 4 >= novaStates.length * 3)
			rebuild(novaStates.length << 1);
		var index = insertionSlot(key);
		if (index < 0) {
			rebuild(novaStates.length << 1);
			index = insertionSlot(key);
		}
		if (novaStates[index] != 1) {
			if (novaStates[index] == 2) novaRemoved--;
			novaStates[index] = 1;
			novaKeys[index] = key;
			novaSize++;
		}
		novaValues[index] = value;
	}

	public function get(key:K):Null<V> {
		var index = locate(key);
		return index < 0 ? null : novaValues[index];
	}

	public function exists(key:K):Bool return locate(key) >= 0;

	public function remove(key:K):Bool {
		var index = locate(key);
		if (index < 0) return false;
		novaStates[index] = 2;
		novaKeys[index] = cast null;
		novaValues[index] = cast null;
		novaSize--;
		novaRemoved++;
		if (novaRemoved > novaSize && novaStates.length > INITIAL_CAPACITY)
			rebuild(novaStates.length);
		return true;
	}

	public function keys():Iterator<K> {
		var result:Array<K> = [];
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1) result.push(novaKeys[index]);
		return result.iterator();
	}

	public function iterator():Iterator<V> {
		var result:Array<V> = [];
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1) result.push(novaValues[index]);
		return result.iterator();
	}

	@:runtime public inline function keyValueIterator():KeyValueIterator<K, V>
		return new haxe.iterators.MapKeyValueIterator(this);

	public function copy():ObjectMap<K, V> {
		var copied = new ObjectMap<K, V>();
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1) copied.set(novaKeys[index], novaValues[index]);
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
	@:ifFeature("haxe.ds.ObjectMap.*")
	private var h:Dynamic;

	public function new():Void {}

	public function set(key:K, value:V):Void {
		untyped __global__.__object_hash_set(__cpp__("HX_MAP_THIS"), key, value);
	}

	public function get(key:K):Null<V> {
		return untyped __global__.__object_hash_get(h, key);
	}

	public function exists(key:K):Bool {
		return untyped __global__.__object_hash_exists(h, key);
	}

	public function remove(key:K):Bool {
		return untyped __global__.__object_hash_remove(h, key);
	}

	public function keys():Iterator<K> {
		var a:Array<K> = untyped __global__.__object_hash_keys(h);
		return a.iterator();
	}

	public function iterator():Iterator<V> {
		var a:Array<Dynamic> = untyped __global__.__object_hash_values(h);
		return a.iterator();
	}

	@:runtime public inline function keyValueIterator():KeyValueIterator<K, V> {
		return new haxe.iterators.MapKeyValueIterator(this);
	}

	public function copy():ObjectMap<K, V> {
		var copied = new ObjectMap();
		for (key in keys())
			copied.set(key, get(key));
		return copied;
	}

	public function toString():String {
		return untyped __global__.__object_hash_to_string(h);
	}

	public function clear():Void {
		#if (hxcpp_api_level >= 400)
		return untyped __global__.__object_hash_clear(h);
		#else
		h = null;
		#end
	}

	#if (scriptable)
	private function setString(key:Dynamic, val:String):Void {
		untyped __object_hash_set_string(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setInt(key:Dynamic, val:Int):Void {
		untyped __object_hash_set_int(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setBool(key:Dynamic, val:Bool):Void {
		untyped __object_hash_set_int(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setFloat(key:Dynamic, val:Float):Void {
		untyped __object_hash_set_float(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setInt64(key:Dynamic, val:haxe.Int64):Void {
		untyped __object_hash_set_int64(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function getString(key:Dynamic):String {
		return untyped __object_hash_get_string(h, key);
	}

	private function getInt(key:Dynamic):Int {
		return untyped __object_hash_get_int(h, key);
	}

	private function getBool(key:Dynamic):Bool {
		return untyped __object_hash_get_bool(h, key);
	}

	private function getFloat(key:Dynamic):Float {
		return untyped __object_hash_get_float(h, key);
	}

	private function getInt64(key:Dynamic):haxe.Int64 {
		return untyped __object_hash_get_int64(h, key);
	}
	#end
	#end
}
