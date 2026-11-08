import haxe.io.Path;
import haxe.io.Eof;
import haxe.crypto.Sha256;
import sys.FileSystem;
import sys.io.File;
import sys.thread.Lock;
import sys.thread.Mutex;
import sys.thread.Thread;

class HxcppZgcBuild {
    static var activeDefines:Map<String, String>;
    static var haxelibRoots:Map<String, String> = [];

    static function fail(message:String):Dynamic {
        Sys.stderr().writeString('hxcpp-zgc: $message\n');
        Sys.exit(1);
        throw message;
    }

    static function quote(value:String):String {
        return '"' + value.split('"').join('\\"') + '"';
    }

    static function resolveHaxelib(name:String):String {
        var cached = haxelibRoots.get(name);
        if (cached != null)
            return cached;
        var cursor = FileSystem.fullPath(Sys.getCwd());
        var local:String = null;
        while (true) {
            var candidate = Path.join([cursor, '.haxelib', name]);
            if (FileSystem.exists(candidate)) {
                local = FileSystem.fullPath(candidate);
                break;
            }
            var parent = Path.directory(cursor);
            if (parent == cursor || parent.length == 0)
                break;
            cursor = parent;
        }
        if (local == null)
            fail('local haxelib is missing: $name');
        var dev = Path.join([local, '.dev']);
        var selected:String = null;
        if (FileSystem.exists(dev)) {
            selected = StringTools.trim(File.getContent(dev));
        } else {
            var current = Path.join([local, '.current']);
            if (FileSystem.exists(current)) {
                var version = StringTools.trim(File.getContent(current));
                selected = Path.join([local, version.split('.').join(',')]);
            }
        }
        if (selected == null || !FileSystem.exists(selected))
            fail('cannot resolve local haxelib: $name');
        selected = FileSystem.fullPath(selected);
        haxelibRoots.set(name, selected);
        return selected;
    }

    static function expand(value:String, hxcppRoot:String):String {
        var result = value.split('$' + '{HXCPP}').join(hxcppRoot);
        var haxelibExpression = ~/\$\{haxelib:([^}]+)\}/g;
        result = haxelibExpression.map(result, function(m) {
            return resolveHaxelib(m.matched(1));
        });
        var expression = ~/\$\{([A-Za-z_][A-Za-z0-9_]*)\}/g;
        return expression.map(result, function(m) {
            if (activeDefines != null) {
                var defined = activeDefines.get(m.matched(1));
                if (defined != null)
                    return defined;
            }
            var environment = Sys.getEnv(m.matched(1));
            return environment == null ? m.matched(0) : environment;
        });
    }

    static function condition(expression:String,
                              defines:Map<String, String>):Bool {
        for (alternative in expression.split('||')) {
            var matches = true;
            for (term in alternative.split('&&')) {
                var name = StringTools.trim(term);
                var inverted = StringTools.startsWith(name, '!');
                if (inverted)
                    name = StringTools.trim(name.substr(1));
                var present = defines.exists(name);
                if (inverted ? present : !present) {
                    matches = false;
                    break;
                }
            }
            if (matches)
                return true;
        }
        return false;
    }

    static function enabled(node:Xml, defines:Map<String, String>):Bool {
        if (node.exists('if') && !condition(node.get('if'), defines))
            return false;
        if (node.exists('unless') && condition(node.get('unless'), defines))
            return false;
        return true;
    }

    static function absolute(base:String, path:String):String {
        return FileSystem.fullPath(Path.isAbsolute(path) ? path : Path.join([base, path]));
    }

    static function addUnique(values:Array<String>, value:String):Void {
        if (values.indexOf(value) < 0)
            values.push(value);
    }

    static function isCppSource(path:String):Bool {
        var extension = Path.extension(path).toLowerCase();
        return extension == 'cpp' || extension == 'cc' ||
            extension == 'cxx';
    }

    static function isTextuallyIncluded(candidate:String,
                                         sources:Array<String>):Bool {
        var fileName = Path.withoutDirectory(candidate);
        var quoted = '#include "' + fileName + '"';
        var angled = '#include <' + fileName + '>';
        for (source in sources) {
            if (source == candidate || !isCppSource(source))
                continue;
            var input = File.read(source, false);
            var included = false;
            try {
                // Generated Haxe includes are emitted at the beginning of the
                // translation unit.  Keeping this scan bounded avoids loading
                // multi-megabyte generated sources merely to detect a native
                // implementation that would otherwise be linked twice.
                for (_ in 0...128) {
                    var line = StringTools.trim(input.readLine());
                    if (line == quoted || line == angled) {
                        included = true;
                        break;
                    }
                }
            } catch (_:Eof) {}
            input.close();
            if (included)
                return true;
        }
        return false;
    }

    static function removeTextuallyIncludedNativeSources(
        sources:Array<String>):Void {
        for (candidate in sources.copy()) {
            if (Path.extension(candidate).toLowerCase() != 'c')
                continue;
            if (isTextuallyIncluded(candidate, sources)) {
                sources.remove(candidate);
                Sys.println('hxcpp-zgc: native source is already included by ' +
                    'a generated C++ unit, skipping standalone compile: ' +
                    candidate);
            }
        }
    }

    static function removeMissingStockRuntimeSources(
        sources:Array<String>, hxcppRoot:String):Void {
        var stockRoot = FileSystem.fullPath(
            Path.join([hxcppRoot, 'src/hx'])).split('\\').join('/');
        for (candidate in sources.copy()) {
            if (FileSystem.exists(candidate))
                continue;
            var normalized = FileSystem.fullPath(candidate)
                .split('\\').join('/');
            if (StringTools.startsWith(normalized, stockRoot + '/'))
                sources.remove(candidate);
        }
    }

    static function ensureDirectory(path:String):Void {
        if (FileSystem.exists(path))
            return;
        var parent = Path.directory(path);
        if (parent != path && parent.length > 0)
            ensureDirectory(parent);
        FileSystem.createDirectory(path);
    }

    static function dependencyDelimiter(content:String):Int {
        // Skip the drive-letter colon in Windows paths and locate the colon
        // separating the make target from its prerequisites.
        for (index in 2...content.length - 1) {
            if (content.charAt(index) == ':') {
                var next = content.charCodeAt(index + 1);
                if (next == 32 || next == 9 || next == 10 || next == 13)
                    return index;
            }
        }
        return -1;
    }

    static function objectIsCurrent(source:String, object:String,
                                    dependencies:String):Bool {
        if (!FileSystem.exists(object) || !FileSystem.exists(dependencies))
            return false;
        var objectTime = FileSystem.stat(object).mtime.getTime();
        if (FileSystem.stat(source).mtime.getTime() > objectTime)
            return false;
        var content = File.getContent(dependencies)
            .split('\\\r\n').join(' ')
            .split('\\\n').join(' ');
        // -MP appends an empty phony rule for every header. Only the first
        // logical make rule describes this object's prerequisites.
        content = content.split('\n')[0];
        var delimiter = dependencyDelimiter(content);
        if (delimiter < 0)
            return false;
        var whitespace = ~/[\t\r\n ]+/g;
        for (dependency in whitespace.split(content.substr(delimiter + 1))) {
            dependency = StringTools.trim(dependency);
            if (dependency.length == 0 || dependency == '\\')
                continue;
            dependency = dependency.split('\\ ').join(' ');
            if (!FileSystem.exists(dependency) ||
                FileSystem.stat(dependency).mtime.getTime() > objectTime)
                return false;
        }
        return true;
    }

    static function compileJobs():Int {
        var configured = Sys.getEnv('HXCPP_COMPILE_JOBS');
        var jobs = configured == null ? null : Std.parseInt(configured);
        if (jobs == null || jobs < 1) {
            jobs = Std.parseInt(Sys.getEnv('NUMBER_OF_PROCESSORS'));
            if (jobs == null || jobs < 1)
                jobs = 4;
        }
        // Large generated units consume substantial memory. Eight workers
        // keeps the default bounded even on machines with many logical CPUs.
        return Std.int(Math.max(1, Math.min(jobs, 8)));
    }

    static function normalizeCompilerFlag(flag:String, base:String):String {
        if (StringTools.startsWith(flag, '-I') && flag.length > 2) {
            var include = flag.substr(2);
            if (!Path.isAbsolute(include))
                include = absolute(base, include);
            return '-I' + include;
        }
        return flag;
    }

    static function normalizeLibrary(value:String, base:String):String {
        if (StringTools.endsWith(value.toLowerCase(), '.lib') &&
            value.indexOf('/') < 0 && value.indexOf('\\') < 0)
            return '-l' + Path.withoutExtension(value);
        if (!StringTools.startsWith(value, '-') && !Path.isAbsolute(value))
            return absolute(base, value);
        return value;
    }

    static function collectNode(node:Xml, base:String,
                                groups:Map<String, Xml>, hxcppRoot:String,
                                defines:Map<String, String>,
                                sources:Array<String>,
                                compilerFlags:Array<String>,
                                linkerFlags:Array<String>,
                                activeGroups:Map<String, Bool>):Void {
        if (!enabled(node, defines))
            return;
        switch (node.nodeName) {
            case 'compilerflag':
                var value = node.get('value');
                if (value != null)
                    addUnique(compilerFlags, normalizeCompilerFlag(
                        expand(value, hxcppRoot), base));
            case 'file':
                var name = node.get('name');
                if (name != null) {
                    name = expand(name, hxcppRoot);
                    addUnique(sources, absolute(base, name));
                }
            case 'lib':
                var name = node.get('name');
                if (name != null)
                    addUnique(linkerFlags, normalizeLibrary(
                        expand(name, hxcppRoot), base));
            case 'flag', 'linkerflag':
                var value = node.get('value');
                if (value != null)
                    addUnique(linkerFlags, expand(value, hxcppRoot));
            case 'files':
                var groupBase = base;
                var directory = node.get('dir');
                if (directory != null)
                    groupBase = absolute(base, expand(directory, hxcppRoot));
                var hasChildren = node.elements().hasNext();
                if (hasChildren) {
                    for (child in node.elements())
                        collectNode(child, groupBase, groups, hxcppRoot,
                            defines, sources, compilerFlags, linkerFlags,
                            activeGroups);
                } else {
                    var id = node.get('id');
                    var referenced = id == null ? null : groups.get(id);
                    if (referenced != null && !activeGroups.exists(id)) {
                        activeGroups.set(id, true);
                        collectNode(referenced, base, groups, hxcppRoot,
                            defines, sources, compilerFlags, linkerFlags,
                            activeGroups);
                        activeGroups.remove(id);
                    }
                }
            default:
                for (child in node.elements())
                    collectNode(child, base, groups, hxcppRoot, defines,
                        sources, compilerFlags, linkerFlags, activeGroups);
        }
    }

    static function processBuild(file:String, hxcppRoot:String,
                                 defines:Map<String, String>,
                                 sources:Array<String>,
                                 compilerFlags:Array<String>,
                                 linkerFlags:Array<String>,
                                 visited:Map<String, Bool>):Void {
        file = FileSystem.fullPath(file);
        if (visited.exists(file))
            return;
        if (!FileSystem.exists(file)) {
            var normalized = file.split('\\').join('/').toLowerCase();
            if (normalized.indexOf('/hxcpp/src/hx/libs/') >= 0 ||
                normalized.indexOf('/hxcpp/build-tool/') >= 0)
                return;
            fail('included Build.xml is missing: $file');
        }
        visited.set(file, true);
        var base = Path.directory(file);
        var document = Xml.parse(File.getContent(file));
        var root = document.firstElement();
        if (root == null)
            fail('Build.xml has no root element: $file');

        var previousThisDir = defines.get('this_dir');
        defines.set('this_dir', base);
        for (node in root.elementsNamed('set')) {
            if (enabled(node, defines)) {
                var name = node.get('name');
                var value = node.get('value');
                if (name != null && value != null)
                    defines.set(name, expand(value, hxcppRoot));
            }
        }

        var groups:Map<String, Xml> = [];
        for (group in root.elementsNamed('files')) {
            var id = group.get('id');
            if (id != null)
                groups.set(id, group);
        }
        var activeGroups:Map<String, Bool> = [];
        for (id in ['haxe', '__main__', '__resources__']) {
            var group = groups.get(id);
            if (group != null)
                collectNode(group, base, groups, hxcppRoot, defines,
                    sources, compilerFlags, linkerFlags, activeGroups);
        }
        for (target in root.elementsNamed('target')) {
            if (target.get('id') == 'haxe')
                collectNode(target, base, groups, hxcppRoot, defines,
                    sources, compilerFlags, linkerFlags, activeGroups);
        }
        for (include in root.elementsNamed('include')) {
            if (!enabled(include, defines))
                continue;
            var name = include.get('name');
            if (name != null)
                processBuild(absolute(base, expand(name, hxcppRoot)),
                    hxcppRoot, defines, sources, compilerFlags,
                    linkerFlags, visited);
        }
        if (previousThisDir == null)
            defines.remove('this_dir');
        else
            defines.set('this_dir', previousThisDir);
    }

    static function main():Void {
        var arguments = Sys.args();
        if (arguments.length == 0)
            fail('expected Build.xml and target name');

        // haxelib runs scripts from the library directory and appends the
        // caller's original working directory as the final argument.
        var lastArgument = arguments[arguments.length - 1];
        if (FileSystem.exists(lastArgument) && FileSystem.isDirectory(lastArgument)) {
            arguments.pop();
            Sys.setCwd(lastArgument);
        }

        var buildFile = arguments.shift();
        if (buildFile == null)
            fail('missing Build.xml');
        buildFile = FileSystem.fullPath(buildFile);
        if (!FileSystem.exists(buildFile))
            fail('Build.xml does not exist: $buildFile');

        var defines:Map<String, String> = [];
        defines.set('windows', '1');
        defines.set('mingw', '1');
        defines.set('HXCPP_MINGW', '1');
        defines.set('HXCPP_M64', '1');
        defines.set('HXCPP_X86_64', '1');
        defines.set('HXCPP_ZGC_ABI', '1');
        var optionsFile = Path.join([Path.directory(buildFile), 'Options.txt']);
        if (FileSystem.exists(optionsFile)) {
            for (line in File.getContent(optionsFile).split('\n')) {
                line = StringTools.trim(line);
                if (line.length == 0 || StringTools.startsWith(line, '#'))
                    continue;
                var separator = line.indexOf('=');
                if (separator > 0)
                    defines.set(StringTools.trim(line.substr(0, separator)),
                        StringTools.trim(line.substr(separator + 1)));
                else
                    defines.set(line, '1');
            }
        }
        for (argument in arguments) {
            var normalized = argument;
            if (StringTools.startsWith(normalized, '-D'))
                normalized = normalized.substr(2);
            var equals = normalized.indexOf('=');
            if (equals > 0)
                defines.set(normalized.substr(0, equals), normalized.substr(equals + 1));
            else if (normalized != 'haxe')
                defines.set(normalized, '1');
        }
        activeDefines = defines;

        var document = Xml.parse(File.getContent(buildFile));
        var root = document.firstElement();
        if (root == null)
            fail('Build.xml has no root element');

        var zgcMarker = false;
        var outputName:String = null;
        for (node in root.elementsNamed('set')) {
            if (!enabled(node, defines))
                continue;
            var name = node.get('name');
            var value = node.get('value');
            if (name == 'HXCPP_ZGC_ABI' && value == '1')
                zgcMarker = true;
            if (name == 'HAXE_OUTPUT')
                outputName = value;
            if (name != null && value != null)
                defines.set(name, value);
        }
        // Lime's precompiled command-line tool can embed an older BuildMain
        // template even when the project-selected Lime sources are newer. In
        // that case the generated root XML still contains stock BuildCommon,
        // but Options.txt is generated from the current Haxe invocation and
        // carries both NovaGC code-generation contracts. Accept that exact
        // pair as the marker; never accept a generic unmarked hxcpp build.
        var zgcCodegen = defines.get('hxcpp_zgc') == '1' &&
            defines.get('novagc_precise') == '1';
        if (!zgcMarker && !zgcCodegen)
            fail('refusing Build.xml without HXCPP_ZGC_ABI=1 or the ' +
                'hxcpp_zgc + novagc_precise codegen contract');
        if (outputName == null || outputName.length == 0)
            fail('Build.xml does not define HAXE_OUTPUT');

        var hxcppRoot = FileSystem.fullPath(Path.directory(Sys.programPath()));
        var buildRoot = Path.directory(buildFile);
        var sources:Array<String> = [];
        var compilerFlags:Array<String> = [];
        var linkerFlags:Array<String> = [];
        processBuild(buildFile, hxcppRoot, defines, sources,
            compilerFlags, linkerFlags, []);

        removeTextuallyIncludedNativeSources(sources);
        // A precompiled Lime CLI can still emit stock BuildCommon.xml, whose
        // haxe group contains src/hx/NoFiles.cpp. NovaGC replaces that entire
        // runtime tree below, so ignore only missing files inside the selected
        // hxcpp's stock src/hx directory. Missing project or haxelib sources
        // remain fatal in the compilation loop.
        removeMissingStockRuntimeSources(sources, hxcppRoot);

        sources.push(Path.join([hxcppRoot, 'runtime/gc/runtime.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/runtime_facade.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/string.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/thread.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/cffi.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/class.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/array.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/dynamic.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/stack_trace.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/diagnostics.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/compatibility.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/memory.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/native_sys.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/native_zlib.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/resources.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/weak_map.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/native_date.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/native_regex.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/native_lib.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/native_ssl.cpp']));
        sources.push(Path.join([hxcppRoot, 'runtime/core/executable_main.cpp']));

        var output = expand(outputName, hxcppRoot);
        if (!Path.isAbsolute(output))
            output = Path.join([buildRoot, output]);
        if (Path.extension(output).toLowerCase() != 'exe')
            output += '.exe';
        output = FileSystem.fullPath(output);
        var outputDirectory = Path.directory(output);
        if (!FileSystem.exists(outputDirectory))
            FileSystem.createDirectory(outputDirectory);

        var objectDirectory = Path.join([buildRoot, 'obj', 'hxcpp-zgc']);
        ensureDirectory(objectDirectory);

        var cxx = Sys.getEnv('CXX');
        var mingwBin = 'C:\\msys64\\mingw64\\bin';
        if (cxx == null || cxx.length == 0) {
            var bundled = Path.join([mingwBin, 'g++.exe']);
            cxx = FileSystem.exists(bundled) ? bundled : 'g++';
        }
        if (FileSystem.exists(mingwBin)) {
            var path = Sys.getEnv('PATH');
            if (path == null)
                path = '';
            Sys.putEnv('PATH', mingwBin + ';' + path);
        }

        var releaseBuild = !defines.exists('debug');
        var optimizationFlag = releaseBuild ? '-Os' : '-O2';
        var compileOptions = [
            // Hot load/store wrappers carry explicit always_inline markers,
            // so Os keeps those targeted call-boundary wins while avoiding
            // the code growth caused by O2 on thousands of generated methods.
            // The smaller working set also improves instruction-cache locality.
            // DWARF remains disabled because it otherwise inflates the Windows
            // image by hundreds of megabytes.
            '-std=c++20', optimizationFlag, '-g0',
            '-Wall', '-Wextra', '-Wpedantic',
            '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-result',
            '-fdiagnostics-color=never',
            // These are C/C++ preprocessor contracts in the original hxcpp
            // toolchain, not merely Build.xml condition names.  Native Haxe
            // injections and runtime headers are allowed to branch on them.
            '-DHX_WINDOWS', '-DHXCPP_MINGW', '-DHXCPP_M64',
            '-DHXCPP_X86_64', '-DHXCPP_ZGC_ABI=1',
            '-I' + Path.join([hxcppRoot, 'include']),
            '-I' + Path.join([buildRoot, 'include'])
        ];
        if (releaseBuild) {
            // Give the PE linker per-function/data granularity. Combined with
            // --gc-sections this drops code retained only because it shared a
            // generated translation unit with a live registration function.
            compileOptions.push('-DNDEBUG');
            compileOptions.push('-ffunction-sections');
            compileOptions.push('-fdata-sections');
        }
        for (flag in compilerFlags)
            compileOptions.push(flag);

        // GCC accepts an existing hxcpp.h.gch without noticing changes in the
        // GC headers included by hxcpp.h. Rebuild the PCH before calculating
        // the object-cache key so public barrier/runtime inline changes cannot
        // silently compile against an older ABI.
        var pchPath = Path.join([hxcppRoot, 'include', 'hxcpp.h.gch']);
        var pchSource = Path.join([hxcppRoot, 'include', 'hxcpp.h']);
        var pchInputs = [
            pchSource,
            Path.join([hxcppRoot, 'include', 'hx/gc/abi.hpp']),
            Path.join([hxcppRoot, 'include', 'hx/gc/runtime.hpp']),
            Path.join([hxcppRoot, 'include', 'hx/gc/internal.hpp']),
            // hxcpp.h includes these runtime surfaces directly near its end.
            // GCC's .gch validation does not reliably invalidate the PCH when
            // one of them changes, as the Array<UInt8> startup fix exposed.
            Path.join([hxcppRoot, 'include', 'hx/array.hpp']),
            Path.join([hxcppRoot, 'include', 'hx/enum.hpp']),
            Path.join([hxcppRoot, 'include', 'hx/function.hpp']),
            Path.join([hxcppRoot, 'include', 'hx/class.hpp']),
            Path.join([hxcppRoot, 'include', 'hx/native_sys.hpp'])
        ];
        var rebuildPch = !FileSystem.exists(pchPath);
        if (!rebuildPch) {
            var pchTime = FileSystem.stat(pchPath).mtime.getTime();
            for (input in pchInputs) {
                if (FileSystem.exists(input) &&
                    FileSystem.stat(input).mtime.getTime() > pchTime) {
                    rebuildPch = true;
                    break;
                }
            }
        }
        if (rebuildPch) {
            var temporaryPch = pchPath + '.building';
            if (FileSystem.exists(temporaryPch))
                FileSystem.deleteFile(temporaryPch);
            var pchOptions = compileOptions.copy();
            pchOptions.push('-x');
            pchOptions.push('c++-header');
            pchOptions.push(pchSource);
            pchOptions.push('-o');
            pchOptions.push(temporaryPch);
            Sys.println('hxcpp-zgc: rebuilding precompiled runtime header');
            var pchResult = Sys.command(cxx, pchOptions);
            if (pchResult != 0) {
                if (FileSystem.exists(temporaryPch))
                    FileSystem.deleteFile(temporaryPch);
                fail('C++ precompiled-header build failed with exit code ' +
                    pchResult);
            }
            if (FileSystem.exists(pchPath))
                FileSystem.deleteFile(pchPath);
            FileSystem.rename(temporaryPch, pchPath);
        }

        // GCC omits headers consumed through hxcpp.h.gch from -MMD output.
        // Put the PCH identity in the object-cache key so a regenerated PCH
        // always receives a coherent object set.
        var pchStamp = 'none';
        if (FileSystem.exists(pchPath)) {
            var pchStat = FileSystem.stat(pchPath);
            pchStamp = Std.string(pchStat.size) + ':' +
                Std.string(pchStat.mtime.getTime());
        }
        var compileKey = Sha256.encode(
            compileOptions.join('\n') + '\npch=' + pchStamp).substr(0, 16);
        var compileDirectory = Path.join([objectDirectory, compileKey]);
        ensureDirectory(compileDirectory);
        var objects:Array<String> = [];
        var work:Array<{source:String, object:String, dependency:String,
                        response:String}> = [];
        // GCC 16's MinGW libstdc++ does not export the C4 std::string move
        // constructor that -Os may leave out-of-line in native STL code. Keep
        // cold Haxe-generated libraries size-optimized, but compile external
        // native sources and frame/gameplay packages with O2. Besides avoiding
        // the missing constructor this preserves renderer and update throughput.
        function needsRuntimeO2(source:String):Bool {
            if (!releaseBuild)
                return false;
            var normalizedSource = FileSystem.fullPath(source)
                .split('\\').join('/').toLowerCase();
            var generatedRoot = FileSystem.fullPath(
                Path.join([buildRoot, 'src']))
                .split('\\').join('/').toLowerCase();
            if (!StringTools.startsWith(normalizedSource,
                    generatedRoot + '/'))
                return true;
            var relative = normalizedSource.substr(generatedRoot.length + 1);
            var hotPrefixes = [
                'openfl/', 'lime/', 'flixel/', 'flxanimate/', 'games/',
                'general/', 'states/', 'substates/', 'scripts/', 'hxvlc/',
                'modchart/', 'funkin/', 'grig/', 'mobile/', 'native/'
            ];
            for (prefix in hotPrefixes) {
                if (StringTools.startsWith(relative, prefix))
                    return true;
            }
            return relative == 'main.cpp' ||
                relative == 'applicationmain.cpp';
        }
        var sourceIndex = 0;
        for (source in sources) {
            if (!FileSystem.exists(source))
                fail('source listed by Build.xml is missing: $source');
            var identitySalt = needsRuntimeO2(source) ? '\nruntime-o2' : '';
            var identity = Sha256.encode(source + identitySalt).substr(0, 20);
            var stem = Path.withoutExtension(Path.withoutDirectory(source));
            var object = Path.join([compileDirectory,
                '${sourceIndex}_${stem}_${identity}.o']);
            var dependency = object + '.d';
            var response = object + '.rsp';
            objects.push(object);
            if (!objectIsCurrent(source, object, dependency))
                work.push({source: source, object: object,
                    dependency: dependency, response: response});
            sourceIndex++;
        }

        var jobs = compileJobs();
        Sys.println('hxcpp-zgc: ${sources.length} translation units, ' +
            '${work.length} require compilation, $jobs workers');
        var stateMutex = new Mutex();
        var completion = new Lock();
        var next = 0;
        var completed = 0;
        var failures:Array<String> = [];
        var workerCount = Std.int(Math.min(jobs, Math.max(work.length, 1)));
        for (_ in 0...workerCount) {
            Thread.create(function() {
                while (true) {
                    stateMutex.acquire();
                    var index = next++;
                    stateMutex.release();
                    if (index >= work.length)
                        break;
                    var unit = work[index];
                    var temporaryObject = unit.object + '.building';
                    var temporaryDependency = unit.dependency + '.building';
                    var options = compileOptions.copy();
                    if (needsRuntimeO2(unit.source))
                        options.push('-O2');
                    options.push('-MMD');
                    options.push('-MP');
                    options.push('-MF');
                    options.push(temporaryDependency);
                    options.push('-c');
                    options.push(unit.source);
                    options.push('-o');
                    options.push(temporaryObject);
                    File.saveContent(unit.response,
                        options.map(quote).join('\n') + '\n');
                    var result = Sys.command(cxx, ['@' + unit.response]);
                    if (result == 0) {
                        if (FileSystem.exists(unit.object))
                            FileSystem.deleteFile(unit.object);
                        if (FileSystem.exists(unit.dependency))
                            FileSystem.deleteFile(unit.dependency);
                        FileSystem.rename(temporaryObject, unit.object);
                        FileSystem.rename(temporaryDependency,
                            unit.dependency);
                    } else {
                        if (FileSystem.exists(temporaryObject))
                            FileSystem.deleteFile(temporaryObject);
                        if (FileSystem.exists(temporaryDependency))
                            FileSystem.deleteFile(temporaryDependency);
                    }
                    stateMutex.acquire();
                    completed++;
                    if (result != 0)
                        failures.push('${unit.source} (exit $result)');
                    if (completed % 50 == 0 || completed == work.length)
                        Sys.println('hxcpp-zgc: compiled $completed/${work.length}');
                    stateMutex.release();
                }
                completion.release();
            });
        }
        for (_ in 0...workerCount)
            completion.wait();
        if (failures.length > 0)
            fail('C++ compilation failed for ${failures.length} unit(s):\n' +
                failures.join('\n'));

        var responseFile = Path.join([objectDirectory, 'link.rsp']);
        var options = ['-static', '-static-libgcc', '-static-libstdc++'];
        if (releaseBuild) {
            options.push('-Wl,--gc-sections');
            // DWARF is already disabled above. Strip the remaining COFF symbol
            // table from shipped builds; runtime Haxe stacks use generated
            // metadata and do not depend on linker symbols.
            options.push('-Wl,--strip-all');
        }
        for (object in objects)
            options.push(object);
        for (flag in linkerFlags)
            options.push(flag);
        options.push('-lz');
        options.push('-lpcre2-16');
        options.push('-lmbedtls');
        options.push('-lmbedx509');
        options.push('-lmbedcrypto');
        if (Sys.systemName() == 'Windows') {
            options.push('-lws2_32');
            options.push('-lcrypt32');
            options.push('-lbcrypt');
        }
        options.push('-o');
        options.push(output);

        File.saveContent(responseFile,
            options.map(quote).join('\n') + '\n');
        Sys.println('hxcpp-zgc: linking ${objects.length} objects -> $output');
        var result = Sys.command(cxx, ['@' + responseFile]);
        if (result != 0)
            fail('C++ linker failed with exit code $result');
        Sys.println('hxcpp-zgc: build succeeded');
    }
}
