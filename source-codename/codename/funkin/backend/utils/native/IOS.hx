package codename.funkin.backend.utils.native;

#if (ios && cpp)
@:cppFileCode('#include <sys/sysctl.h>')
@:dox(hide)
final class IOS
{
	@:functionCode('
		int mib[] = { CTL_HW, HW_MEMSIZE };
		int64_t value = 0;
		size_t length = sizeof(value);

		if (sysctl(mib, 2, &value, &length, NULL, 0) == -1) return -1;
		return value / 1024 / 1024;
	')
	public static function getTotalRam():Float
	{
		return 0;
	}
}
#end
