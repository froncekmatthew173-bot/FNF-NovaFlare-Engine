package states.freeplayState.shader;

import flixel.addons.display.FlxRuntimeShader;
import openfl.filters.ShaderFilter;

/**
 * A single-pass Gaussian blur for menu backgrounds.
 *
 * The previous implementation attached four full-screen camera filters and
 * used up to 64 texture reads in its first pass.  A static 1280x720 menu then
 * spent several complete frame budgets only producing intermediate textures.
 * This version retains the blurred background while requiring one pass and
	* 9 Gaussian-weighted texture reads per pixel. Unlike sparse wide-offset
	* sampling, this produces one continuous blurred image instead of visible
	* copies of the source artwork. Every tap uses flixel_texture2D so sprite
	* tint and cross-fade alpha remain correct. Freeplay applies it directly
 * to its background sprites so the camera does not need an extra full-screen
 * render target and composite pass.
 */
class BlurFilter
{
	public var blurShader_reduce:FlxRuntimeShader;

	// Compatibility aliases for code that used the old four-stage object.
	public var blurShader_blur_1(get, never):FlxRuntimeShader;
	public var blurShader_blur_2(get, never):FlxRuntimeShader;
	public var blurShader_amplification(get, never):FlxRuntimeShader;

	public var textureScale(default, null):Float = 1;
	public var blurRadius(default, null):Float = 0;

	public function new(?value:Float)
	{
		blurShader_reduce = new FlxRuntimeShader("
			#pragma header

			uniform float blurRadius;

			void main()
			{
				if (blurRadius <= 0.0)
				{
					gl_FragColor = flixel_texture2D(bitmap, openfl_TextureCoordv);
					return;
				}

				vec2 stepUV = vec2(blurRadius * 0.25) / openfl_TextureSize;
				vec2 uv = openfl_TextureCoordv;
				vec4 color = vec4(0.0);
				color += flixel_texture2D(bitmap, uv + stepUV * vec2(-1.0, -1.0));
				color += flixel_texture2D(bitmap, uv + stepUV * vec2( 0.0, -1.0)) * 2.0;
				color += flixel_texture2D(bitmap, uv + stepUV * vec2( 1.0, -1.0));
				color += flixel_texture2D(bitmap, uv + stepUV * vec2(-1.0,  0.0)) * 2.0;
				color += flixel_texture2D(bitmap, uv) * 4.0;
				color += flixel_texture2D(bitmap, uv + stepUV * vec2( 1.0,  0.0)) * 2.0;
				color += flixel_texture2D(bitmap, uv + stepUV * vec2(-1.0,  1.0));
				color += flixel_texture2D(bitmap, uv + stepUV * vec2( 0.0,  1.0)) * 2.0;
				color += flixel_texture2D(bitmap, uv + stepUV * vec2( 1.0,  1.0));
				gl_FragColor = color / 16.0;
			}
		");
		set(value == null ? 0 : value);
	}

	public function apply(camera:FlxCamera):Void
	{
		addShader(camera, blurShader_reduce);
	}

	public function remove(camera:FlxCamera):Void
	{
		removeShader(camera, blurShader_reduce);
	}

	public function set(value:Float):Void
	{
		blurRadius = Math.max(0, value);
		// Preserve the old public intensity value for callers that display it.
		textureScale = blurRadius * 0.2666666666666667 + 1;
		blurShader_reduce.setFloat('blurRadius', blurRadius);
	}

	public function setModifier(value:Float):Void
	{
		// Kept as a source-compatible entry point. Quality no longer changes the
		// number of passes or texture samples, so intensity is the only parameter.
		set(value);
	}

	inline function get_blurShader_blur_1():FlxRuntimeShader
		return blurShader_reduce;

	inline function get_blurShader_blur_2():FlxRuntimeShader
		return blurShader_reduce;

	inline function get_blurShader_amplification():FlxRuntimeShader
		return blurShader_reduce;

	@:privateAccess public function addShader(camera:FlxCamera, shader:FlxRuntimeShader):ShaderFilter
	{
		if (camera.filters == null)
			camera.filters = [];
		var filter = new ShaderFilter(shader);
		camera.filters.push(filter);
		return filter;
	}

	@:privateAccess public function removeShader(camera:FlxCamera, shader:FlxRuntimeShader):Bool
	{
		if (camera.filters == null)
			return false;
		for (filter in camera.filters)
		{
			if (filter is ShaderFilter)
			{
				var shaderFilter:ShaderFilter = cast filter;
				if (shaderFilter.shader == shader)
				{
					camera.filters.remove(filter);
					return true;
				}
			}
		}
		return false;
	}
}
