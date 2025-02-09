// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/CompileAndAsyncManager.h"
#include "../../Assets/AssetUtils.h"
#include "../../ConsoleRig/GlobalServices.h"	// (for LibVersionDesc)
#include "../../Utility/MemoryUtils.h"
#include "../../Core/Types.h"
#include <memory>
#include <functional>
#include <regex>
#include <vector>

namespace Assets
{
	class ICompileOperation;

    class GeneralCompiler : public IAssetCompiler, public std::enable_shared_from_this<GeneralCompiler>
    {
    public:
        std::shared_ptr<IArtifactCompileMarker> Prepare(
            uint64 typeCode, 
            const StringSection<ResChar> initializers[], unsigned initializerCount);
		std::vector<uint64_t> GetTypesForAsset(const StringSection<ResChar> initializers[], unsigned initializerCount);
		std::vector<std::pair<std::string, std::string>> GetExtensionsForType(uint64_t typeCode);
        void StallOnPendingOperations(bool cancelAll);
		
		using CompileOperationDelegate = std::function<std::shared_ptr<ICompileOperation>(StringSection<>)>;

		struct ExtensionAndDelegate
		{
			std::vector<uint64_t> _assetTypes;
			std::regex _regexFilter;
			std::string _name;
			std::string _extensionsForOpenDlg;
			ConsoleRig::LibVersionDesc _srcVersion;
			CompileOperationDelegate _delegate;
		};
		
		GeneralCompiler(
			IteratorRange<const ExtensionAndDelegate*> delegates,
			const std::shared_ptr<IntermediateAssets::Store>& store);
        ~GeneralCompiler();
    protected:
        class Pimpl;
        std::shared_ptr<Pimpl> _pimpl;

        class Marker;
    };

	DirectorySearchRules DefaultLibrarySearchDirectories();

	std::vector<GeneralCompiler::ExtensionAndDelegate> DiscoverCompileOperations(
		StringSection<> librarySearch,
		const DirectorySearchRules& searchRules = DefaultLibrarySearchDirectories());
}

