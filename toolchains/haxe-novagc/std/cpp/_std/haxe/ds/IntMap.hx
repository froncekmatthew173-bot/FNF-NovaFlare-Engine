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
  inline bool get_bool(int key) { return get(key).Cast<bool>(); }
  inline int get_int(int key) { return get(key).Cast<int>(); }
  inline Float get_float(int key) { return get(key).Cast<double>(); }
  inline String get_string(int key) { return get(key).Cast<String>(); }
  inline cpp::Int64 get_int64(int key) { return get(key).Cast<cpp::Int64>(); }
")
#end

#if !hxcpp_zgc
@:headerClassCode("
  inline void set(int key, ::null value) { __int_hash_set(HX_MAP_THIS,key,value); }
  inline void set(int key, bool value) { __int_hash_set(HX_MAP_THIS,key,value); }
  inline void set(int key, char value) { __int_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(int key, unsigned char value) { __int_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(int key, signed char value) { __int_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(int key, short value) { __int_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(int key, unsigned short value) { __int_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(int key, int value) { __int_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(int key, unsigned int value) { __int_hash_set_int(HX_MAP_THIS,key,value); }
  inline void set(int key, float value) { __int_hash_set_float(HX_MAP_THIS,key,value); }
  inline void set(int key, double value) { __int_hash_set_float(HX_MAP_THIS,key,value); }
  inline void set(int key, ::String value) { __int_hash_set_string(HX_MAP_THIS,key,value); }
  inline void set(int key, cpp::Int64 value) { __int_hash_set_int64(HX_MAP_THIS,key,value); }

  template<typename V, typename H>
  inline void set(int key, const ::cpp::Struct<V,H> &value) {__int_hash_set(HX_MAP_THIS,key,value); }
  template<typename F>
  inline void set(int key, const ::cpp::Function<F> &value) {__int_hash_set(HX_MAP_THIS,key,value); }
  template<typename V>
  inline void set(int key, const ::cpp::Pointer<V> &value) {__int_hash_set(HX_MAP_THIS,key,(Dynamic)value ); }

  template<typename VALUE>
  inline void set(Dynamic &key, const VALUE &value) { set( (int)key, value ); }

  inline bool get_bool(int key) { return __int_hash_get_bool(h,key); }
  inline int get_int(int key) { return __int_hash_get_int(h,key); }
  inline Float get_float(int key) { return __int_hash_get_float(h,key); }
  inline String get_string(int key) { return __int_hash_get_string(h,key); }
  inline cpp::Int64 get_int64(int key) { return __int_hash_get_int64(h,key); }
")
#end
@:coreApi class IntMap<T> implements haxe.Constraints.IMap<Int, T> {
	#if hxcpp_zgc
	private static inline var INITIAL_CAPACITY:Int = 16;
	private var novaKeys:Array<Int>;
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
			novaKeys.push(0);
			novaValues.push(cast null);
			novaStates.push(0);
		}
	}

	private static inline function hashKey(key:Int):Int {
		return (key ^ (key >>> 16)) & 0x7fffffff;
	}

	private function locate(key:Int):Int {
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

	private function insertionSlot(key:Int):Int {
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

	public function set(key:Int, value:T):Void {
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

	public function get(key:Int):Null<T> {
		var index = locate(key);
		return index < 0 ? null : novaValues[index];
	}

	public function exists(key:Int):Bool return locate(key) >= 0;

	public function remove(key:Int):Bool {
		var index = locate(key);
		if (index < 0) return false;
		novaStates[index] = 2;
		novaKeys[index] = 0;
		novaValues[index] = cast null;
		novaSize--;
		novaRemoved++;
		if (novaRemoved > novaSize && novaStates.length > INITIAL_CAPACITY)
			rebuild(novaStates.length);
		return true;
	}

	public function keys():Iterator<Int> {
		var result:Array<Int> = [];
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1) result.push(novaKeys[index]);
		return result.iterator();
	}

	public function iterator():Iterator<T> {
		var result:Array<T> = [];
		for (index in 0...novaStates.length)
			if (novaStates[index] == 1) result.push(novaValues[index]);
		return result.iterator();
	}

	@:runtime public inline function keyValueIterator():KeyValueIterator<Int, T>
		return new haxe.iterators.MapKeyValueIterator(this);

	public function copy():IntMap<T> {
		var copied = new IntMap<T>();
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
	@:ifFeature("haxe.ds.IntMap.*")
	private var h:Dynamic;

	public function new():Void {}

	public function set(key:Int, value:T):Void {
		untyped __global__.__int_hash_set(__cpp__("HX_MAP_THIS"), key, value);
	}

	public function get(key:Int):Null<T> {
		return untyped __global__.__int_hash_get(h, key);
	}

	public function exists(key:Int):Bool {
		return untyped __global__.__int_hash_exists(h, key);
	}

	public function remove(key:Int):Bool {
		return untyped __global__.__int_hash_remove(h, key);
	}

	public function keys():Iterator<Int> {
		var a:Array<Int> = untyped __global__.__int_hash_keys(h);
		return a.iterator();
	}

	public function iterator():Iterator<T> {
		var a:Array<Dynamic> = untyped __global__.__int_hash_values(h);
		return a.iterator();
	}

	@:runtime public inline function keyValueIterator():KeyValueIterator<Int, T> {
		return new haxe.iterators.MapKeyValueIterator(this);
	}

	public function copy():IntMap<T> {
		var copied = new IntMap();
		for (key in keys())
			copied.set(key, get(key));
		return copied;
	}

	public function toString():String {
		return untyped __global__.__int_hash_to_string(h);
	}

	public function clear():Void {
		#if (hxcpp_api_level >= 400)
		return untyped __global__.__int_hash_clear(h);
		#else
		h = null;
		#end
	}

	#if (scriptable)
	private function setString(key:Int, val:String):Void {
		untyped __int_hash_set_string(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setInt(key:Int, val:Int):Void {
		untyped __int_hash_set_int(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setBool(key:Int, val:Bool):Void {
		untyped __int_hash_set_int(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setFloat(key:Int, val:Float):Void {
		untyped __int_hash_set_float(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function setInt64(key:Int, val:haxe.Int64):Void {
		untyped __int_hash_set_int64(__cpp__("HX_MAP_THIS"), key, val);
	}

	private function getString(key:Int):String {
		return untyped __int_hash_get_string(h, key);
	}

	private function getInt(key:Int):Int {
		return untyped __int_hash_get_int(h, key);
	}

	private function getBool(key:Int):Bool {
		return untyped __int_hash_get_bool(h, key);
	}

	private function getFloat(key:Int):Float {
		return untyped __int_hash_get_float(h, key);
	}

	private function getInt64(key:Int):haxe.Int64 {
		return untyped __int_hash_get_int64(h, key);
	}
	#end
	#end
}
