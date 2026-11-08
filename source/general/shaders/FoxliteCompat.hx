package general.shaders;

import foxlite.renderer.FoxRenderer;
import lime.graphics.opengl.GL;
import openfl.display3D.Context3D;
import openfl.display3D.Program3D;
import openfl.display3D.Context3DBlendFactor;

@:access(openfl.display3D.Context3D)
@:access(openfl.display3D.Program3D)
@:access(openfl.display3D._internal.Context3DState)
@:access(foxlite.renderer.FoxRenderer)
class FoxliteCompat
{
	static var attributeContext:Context3D;
	static var attributeCount:Int = 0;
	public static function upload(program:Program3D, vertexSource:String, fragmentSource:String):Void
	{
		program.__deleteShaders();
		// FoxLite supplies its own ES compatibility macros and supports multiple
		// render targets. Preserve those sources; NF's 2D converter targets one.
		try
		{
			program.__uploadFromGLSL(vertexSource, fragmentSource);
		}
		catch (error:Dynamic)
		{
			ErrorHandledShader.crashSave("foxlite", error, null, vertexSource, fragmentSource);
		}
	}

	public static function begin(context:Context3D):Void
	{
		// OpenFL may have changed GL state since the preceding FoxLite draw.
		FoxRenderer.__blendMode = -1;
		targetChanged(context);
	}

	public static function targetChanged(context:Context3D):Void
	{
		// Flushing an FBO enables its depth/stencil tests through OpenFL. FoxLite's
		// independent cache must reflect those enables before selecting a material.
		FoxRenderer.__depthTest = context.__contextState.__enableGLDepthTest;
		FoxRenderer.__stencilTest = context.__contextState.__enableGLStencilTest;
	}

	public static function backToFlixel(context:Context3D):Void
	{
		var gl = context.gl;
		var cache = context.__contextState;
		context.__flushGLFramebuffer();
		context.__flushGLViewport();

		// FoxLite binds programs, attributes and blend/test state directly. Restore
		// both the GPU and OpenFL's cache, otherwise 2D drawing can skip a rebind.
		gl.disable(gl.DEPTH_TEST);
		gl.disable(gl.STENCIL_TEST);
		gl.disable(gl.SCISSOR_TEST);
		cache.__enableGLDepthTest = false;
		cache.__enableGLStencilTest = false;
		cache.__enableGLScissorTest = false;
		cache.scissorEnabled = false;
		gl.enable(gl.BLEND);
		gl.blendEquation(gl.FUNC_ADD);
		gl.blendFunc(gl.ONE, gl.ONE_MINUS_SRC_ALPHA);
		context.setBlendFactors(ONE, ONE_MINUS_SOURCE_ALPHA);
		cache.__enableGLBlend = true;
		cache.__glBlendEquation = gl.FUNC_ADD;
		cache.blendSourceRGBFactor = cache.blendSourceAlphaFactor = ONE;
		cache.blendDestinationRGBFactor = cache.blendDestinationAlphaFactor = ONE_MINUS_SOURCE_ALPHA;
		gl.colorMask(true, true, true, true);
		cache.colorMaskRed = cache.colorMaskGreen = cache.colorMaskBlue = cache.colorMaskAlpha = true;

		gl.useProgram(null);
		cache.shader = null;
		cache.program = null;
		if (attributeContext != context)
		{
			attributeContext = context;
			attributeCount = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
		}
		for (index in 0...attributeCount)
		{
			gl.disableVertexAttribArray(index);
			GL.vertexAttribDivisor(index, 0);
			cache.__vertexAttribArrays[index] = false;
			cache.__vertexBuffers[index] = null;
		}
		gl.bindBuffer(gl.ARRAY_BUFFER, null);
		cache.__currentGLArrayBuffer = null;
		gl.bindBuffer(gl.ELEMENT_ARRAY_BUFFER, FoxRenderer.__indexBuffer);
		cache.__currentGLElementArrayBuffer = FoxRenderer.__indexBuffer;
		gl.activeTexture(gl.TEXTURE0);
		GL.drawBuffers([cache.__currentGLFramebuffer == null ? gl.BACK : gl.COLOR_ATTACHMENT0]);
		FoxRenderer.__depthTest = FoxRenderer.__stencilTest = FoxRenderer.__scissorTest = false;
	}
}
