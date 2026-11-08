package codename.funkin.backend.utils.native;

#if (android && cpp)
@:cppFileCode('#include <unistd.h>')
@:dox(hide)
final class Android
{
	@:functionCode('
		const long pageCount = sysconf(_SC_PHYS_PAGES);
		const long pageSize = sysconf(_SC_PAGESIZE);
		if (pageCount <= 0 || pageSize <= 0) return -1;
		return ((double)pageCount * (double)pageSize) / (1024.0 * 1024.0);
	')
	public static function getTotalRam():Float
	{
		return 0;
	}
}
#end
