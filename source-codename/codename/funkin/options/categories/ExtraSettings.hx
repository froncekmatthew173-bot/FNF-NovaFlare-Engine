package codename.funkin.options.categories;

class ExtraSettings extends TreeMenuScreen
{
	public function new()
	{
		super('Extra Setting', 'Extra settings for the Codename state chain.');

		add(new NumOption('Watermark Size', 'Changes the NovaFlare watermark size in Codename.',
			0, 5, 0.1, 'watermarkScale', value ->
				codenamechain.CodeNameOverlaySettings.applyWatermarkScale(value)));

		add(new Checkbox('Mouse Effects',
			'Controls pointer trails and click effects outside PlayState.', 'mouseEffects',
			() -> codenamechain.CodeNameOverlaySettings.applyMouseEffects(Options.mouseEffects)));

		#if mobile
		add(new Checkbox('Shader Conversion',
			'Automatically converts desktop OpenFL shaders for OpenGL ES 2.0 and 3.0+.',
			'autoShaderConversion',
			() -> general.shaders.MobileShaderConverter.setEnabled(Options.autoShaderConversion)));
		#end

		add(new Checkbox('Skip Title Video',
			'Skips the NovaFlare startup video when entering Codename.', 'skipTitleVideo'));
	}
}
